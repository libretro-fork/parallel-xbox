/*
 * Postcopy migration for RAM
 *
 * Copyright 2013-2015 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Dave Gilbert  <dgilbert@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/*
 * Postcopy is a migration technique where the execution flips from the
 * source to the destination before all the data has been copied.
 */

#include "qemu/osdep.h"
#include "qemu/madvise.h"
#include "exec/target_page.h"
#include "migration.h"
#include "qemu-file.h"
#include "savevm.h"
#include "postcopy-ram.h"
#include "ram.h"
#include "qapi/error.h"
#include "qemu/notify.h"
#include "qemu/rcu.h"
#include "system/system.h"
#include "qemu/error-report.h"
#include "trace.h"
#include "hw/boards.h"
#include "system/ramblock.h"
#include "yank_functions.h"
#include "qemu/userfaultfd.h"
#include "qemu/mmap-alloc.h"
#include "options.h"

/* Arbitrary limit on size of each discard command,
 * keeps them around ~200 bytes
 */
#define MAX_DISCARDS_PER_COMMAND 12

typedef struct PostcopyDiscardState {
    const char *ramblock_name;
    uint16_t cur_entry;
    /*
     * Start and length of a discard range (bytes)
     */
    uint64_t start_list[MAX_DISCARDS_PER_COMMAND];
    uint64_t length_list[MAX_DISCARDS_PER_COMMAND];
    unsigned int nsentwords;
    unsigned int nsentcmds;
} PostcopyDiscardState;

static NotifierWithReturnList postcopy_notifier_list;

void postcopy_infrastructure_init(void)
{
    notifier_with_return_list_init(&postcopy_notifier_list);
}

int postcopy_notify(enum PostcopyNotifyReason reason, Error **errp)
{
    struct PostcopyNotifyData pnd;
    pnd.reason = reason;

    return notifier_with_return_list_notify(&postcopy_notifier_list,
                                            &pnd, errp);
}

/*
 * NOTE: this routine is not thread safe, we can't call it concurrently. But it
 * should be good enough for migration's purposes.
 */
void postcopy_thread_create(MigrationIncomingState *mis,
                            QemuThread *thread, const char *name,
                            void *(*fn)(void *), int joinable)
{
    qemu_event_init(&mis->thread_sync_event, false);
    qemu_thread_create(thread, name, fn, mis, joinable);
    qemu_event_wait(&mis->thread_sync_event);
    qemu_event_destroy(&mis->thread_sync_event);
}

/* Postcopy needs to detect accesses to pages that haven't yet been copied
 * across, and efficiently map new pages in, the techniques for doing this
 * are target OS specific.
 */
#if defined(__linux__) && !defined(__ANDROID__)
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#endif

#if defined(__linux__) && !defined(__ANDROID__) && defined(__NR_userfaultfd) && defined(CONFIG_EVENTFD)
#include <sys/eventfd.h>
#include <linux/userfaultfd.h>

/*
 * Here we use 24 buckets, which means the last bucket will cover [2^24 us,
 * 2^25 us) ~= [16, 32) seconds.  It should be far enough to record even
 * extreme (perf-wise broken) 1G pages moving over, which can sometimes
 * take a few seconds due to various reasons.  Anything more than that
 * might be unsensible to account anymore.
 */
#define  BLOCKTIME_LATENCY_BUCKET_N  (24)

/* All the time records are in unit of nanoseconds */
typedef struct PostcopyBlocktimeContext {
    /* blocktime per vCPU */
    uint64_t *vcpu_blocktime_total;
    /* count of faults per vCPU */
    uint64_t *vcpu_faults_count;
    /*
     * count of currently blocked faults per vCPU.
     *
     * NOTE: Normally there should only be one fault in-progress per vCPU
     * thread, so logically it _seems_ vcpu_faults_count[] for any vCPU
     * should be either zero or one.  However, there can be reasons we see
     * >1 faults on the same vCPU thread.
     *
     * CASE (1): since the process to resolve faults (ioctl(UFFDIO_COPY),
     * for example) is done before taking the mutex that protects the
     * blocktime context, it can happen that we read more than one faulted
     * addresses per vCPU.
     *
     * One example when we can see >1 faulted addresses for one vCPU:
     *
     *  vcpu1 thread       fault thread         resolve thread
     *  ============       ============         ==============
     *
     *  faulted on addr1
     *                     read uffd msg (addr1)
     *                     MUTEX_LOCK
     *                     add entry (cpu1, addr1)
     *                     MUTEX_UNLOCK
     *                     request remote fault (addr1)
     *                                          resolve fault (addr1)
     *  addr1 resolved, continue..
     *  faulted on addr2
     *                     read uffd msg (addr2)
     *                     MUTEX_LOCK
     *                     add entry (cpu1, addr2) <--------------- [A]
     *                     MUTEX_UNLOCK
     *                                          MUTEX_LOCK
     *                                          remove entry (cpu1, addr1)
     *                                          MUTEX_UNLOCK
     *
     * In above case, we may see (cpu1, addr1) and (cpu1, addr2) entries to
     * appear together at [A], when it gets the lock before the resolve
     * thread.  Use this counter to maintain such case, and only when it
     * reaches zero we know the vCPU is not blocked anymore.
     *
     * CASE (2): theoretically (the author admit to not have verified
     * this..), one vCPU thread can also generate more than one userfaultfd
     * message on the same address. It can happen e.g. for whatever reason
     * the fault got retried before a resolution arrives. In that extremely
     * rare case, we could also see two (cpu1, addr1) entries.
     *
     * In all cases, be prepared with such re-entrancies with this array.
     *
     * Using uint8_t should be far enough for now.  For example, when
     * there're only one resolve thread (postcopy ram listening thread),
     * the max (concurrent fault entries) should be two.
     */
    uint8_t *vcpu_faults_current;
    /*
     * The hash that contains addr1->[(cpu1,ts1),(cpu2,ts2) ...] mappings.
     * Each of the entry is a tuple of (CPU index, fault timestamp) showing
     * that a fault was requested.
     */
    GHashTable *vcpu_addr_hash;
    /*
     * Each bucket stores the count of faults that were resolved within the
     * bucket window [2^N us, 2^(N+1) us).
     */
    uint64_t latency_buckets[BLOCKTIME_LATENCY_BUCKET_N];
    /* total blocktime when all vCPUs are stopped */
    uint64_t total_blocktime;
    /* point in time when last page fault was initiated */
    uint64_t last_begin;
    /* number of vCPU are suspended */
    int smp_cpus_down;

    /*
     * Fast path for looking up vcpu_index from tid.  NOTE: this result
     * only reflects the vcpu setup when postcopy is running.  It may not
     * always match with the current vcpu setup because vcpus can be hot
     * attached/detached after migration completes.  However this should be
     * stable when blocktime is using the structure.
     */
    GHashTable *tid_to_vcpu_hash;
    /* Count of non-vCPU faults.  This is only for debugging purpose. */
    uint64_t non_vcpu_faults;
    /* total blocktime when a non-vCPU thread is stopped */
    uint64_t non_vcpu_blocktime_total;

    /*
     * Handler for exit event, necessary for
     * releasing whole blocktime_ctx
     */
    Notifier exit_notifier;
} PostcopyBlocktimeContext;

typedef struct {
    /* The time the fault was triggered */
    uint64_t fault_time;
    /*
     * The vCPU index that was blocked, when cpu==-1, it means it's a
     * fault from non-vCPU threads.
     */
    int cpu;
} BlocktimeVCPUEntry;

/**
 * receive_ufd_features: check userfault fd features, to request only supported
 * features in the future.
 *
 * Returns: true on success
 *
 * __NR_userfaultfd - should be checked before
 *  @features: out parameter will contain uffdio_api.features provided by kernel
 *              in case of success
 */
static bool receive_ufd_features(uint64_t *features)
{
    struct uffdio_api api_struct = {0};
    int ufd;
    bool ret = true;

    ufd = uffd_open(O_CLOEXEC);
    if (ufd == -1) {
        error_report("%s: uffd_open() failed: %s", __func__, strerror(errno));
        return false;
    }

    /* ask features */
    api_struct.api = UFFD_API;
    api_struct.features = 0;
    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        error_report("%s: UFFDIO_API failed: %s", __func__,
                     strerror(errno));
        ret = false;
        goto release_ufd;
    }

    *features = api_struct.features;

release_ufd:
    close(ufd);
    return ret;
}

/**
 * request_ufd_features: this function should be called only once on a newly
 * opened ufd, subsequent calls will lead to error.
 *
 * Returns: true on success
 *
 * @ufd: fd obtained from userfaultfd syscall
 * @features: bit mask see UFFD_API_FEATURES
 */
static bool request_ufd_features(int ufd, uint64_t features)
{
    struct uffdio_api api_struct = {0};
    uint64_t ioctl_mask;

    api_struct.api = UFFD_API;
    api_struct.features = features;
    if (ioctl(ufd, UFFDIO_API, &api_struct)) {
        error_report("%s failed: UFFDIO_API failed: %s", __func__,
                     strerror(errno));
        return false;
    }

    ioctl_mask = 1ULL << _UFFDIO_REGISTER |
                 1ULL << _UFFDIO_UNREGISTER;
    if ((api_struct.ioctls & ioctl_mask) != ioctl_mask) {
        error_report("Missing userfault features: %" PRIx64,
                     (uint64_t)(~api_struct.ioctls & ioctl_mask));
        return false;
    }

    return true;
}

static bool ufd_check_and_apply(int ufd, MigrationIncomingState *mis,
                                Error **errp)
{
    ERRP_GUARD();
    uint64_t asked_features = 0;
    static uint64_t supported_features;

    /*
     * it's not possible to
     * request UFFD_API twice per one fd
     * userfault fd features is persistent
     */
    if (!supported_features) {
        if (!receive_ufd_features(&supported_features)) {
            error_setg(errp, "Userfault feature detection failed");
            return false;
        }
    }

#ifdef UFFD_FEATURE_THREAD_ID
    /*
     * Postcopy blocktime conditionally needs THREAD_ID feature (introduced
     * to Linux in 2017). Always try to enable it when QEMU is compiled
     * with such environment.
     */
    if (UFFD_FEATURE_THREAD_ID & supported_features) {
        asked_features |= UFFD_FEATURE_THREAD_ID;
    }
#endif

    /*
     * request features, even if asked_features is 0, due to
     * kernel expects UFFD_API before UFFDIO_REGISTER, per
     * userfault file descriptor
     */
    if (!request_ufd_features(ufd, asked_features)) {
        error_setg(errp, "Failed features %" PRIu64, asked_features);
        return false;
    }

    if (qemu_real_host_page_size() != ram_pagesize_summary()) {
        bool have_hp = false;
        /* We've got a huge page */
#ifdef UFFD_FEATURE_MISSING_HUGETLBFS
        have_hp = supported_features & UFFD_FEATURE_MISSING_HUGETLBFS;
#endif
        if (!have_hp) {
            error_setg(errp,
                       "Userfault on this host does not support huge pages");
            return false;
        }
    }
    return true;
}

/* Callback from postcopy_ram_supported_by_host block iterator.
 */
static int test_ramblock_postcopiable(RAMBlock *rb, Error **errp)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    ram_addr_t length = qemu_ram_get_used_length(rb);
    size_t pagesize = qemu_ram_pagesize(rb);
    QemuFsType fs;

    if (length % pagesize) {
        error_setg(errp,
                   "Postcopy requires RAM blocks to be a page size multiple,"
                   " block %s is 0x" RAM_ADDR_FMT " bytes with a "
                   "page size of 0x%zx", block_name, length, pagesize);
        return 1;
    }

    if (rb->fd >= 0) {
        fs = qemu_fd_getfs(rb->fd);
        if (fs != QEMU_FS_TYPE_TMPFS && fs != QEMU_FS_TYPE_HUGETLBFS) {
            error_setg(errp,
                       "Host backend files need to be TMPFS or HUGETLBFS only");
            return 1;
        }
    }

    return 0;
}

/*
 * Note: This has the side effect of munlock'ing all of RAM, that's
 * normally fine since if the postcopy succeeds it gets turned back on at the
 * end.
 */
bool postcopy_ram_supported_by_host(MigrationIncomingState *mis, Error **errp)
{
    ERRP_GUARD();
    long pagesize = qemu_real_host_page_size();
    int ufd = -1;
    bool ret = false; /* Error unless we change it */
    void *testarea = NULL;
    struct uffdio_register reg_struct;
    struct uffdio_range range_struct;
    uint64_t feature_mask;
    RAMBlock *block;

    if (qemu_target_page_size() > pagesize) {
        error_setg(errp, "Target page size bigger than host page size");
        goto out;
    }

    ufd = uffd_open(O_CLOEXEC);
    if (ufd == -1) {
        error_setg(errp, "Userfaultfd not available: %s", strerror(errno));
        goto out;
    }

    /* Give devices a chance to object */
    if (postcopy_notify(POSTCOPY_NOTIFY_PROBE, errp)) {
        goto out;
    }

    /* Version and features check */
    if (!ufd_check_and_apply(ufd, mis, errp)) {
        goto out;
    }

    /*
     * We don't support postcopy with some type of ramblocks.
     *
     * NOTE: we explicitly ignored migrate_ram_is_ignored() instead we checked
     * all possible ramblocks.  This is because this function can be called
     * when creating the migration object, during the phase RAM_MIGRATABLE
     * is not even properly set for all the ramblocks.
     *
     * A side effect of this is we'll also check against RAM_SHARED
     * ramblocks even if migrate_ignore_shared() is set (in which case
     * we'll never migrate RAM_SHARED at all), but normally this shouldn't
     * affect in reality, or we can revisit.
     */
    RAMBLOCK_FOREACH(block) {
        if (test_ramblock_postcopiable(block, errp)) {
            goto out;
        }
    }

    /*
     * userfault and mlock don't go together; we'll put it back later if
     * it was enabled.
     */
    if (munlockall()) {
        error_setg(errp, "munlockall() failed: %s", strerror(errno));
        goto out;
    }

    /*
     *  We need to check that the ops we need are supported on anon memory
     *  To do that we need to register a chunk and see the flags that
     *  are returned.
     */
    testarea = mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_PRIVATE |
                                    MAP_ANONYMOUS, -1, 0);
    if (testarea == MAP_FAILED) {
        error_setg(errp, "Failed to map test area: %s", strerror(errno));
        goto out;
    }
    g_assert(QEMU_PTR_IS_ALIGNED(testarea, pagesize));

    reg_struct.range.start = (uintptr_t)testarea;
    reg_struct.range.len = pagesize;
    reg_struct.mode = UFFDIO_REGISTER_MODE_MISSING;

    if (ioctl(ufd, UFFDIO_REGISTER, &reg_struct)) {
        error_setg(errp, "UFFDIO_REGISTER failed: %s", strerror(errno));
        goto out;
    }

    range_struct.start = (uintptr_t)testarea;
    range_struct.len = pagesize;
    if (ioctl(ufd, UFFDIO_UNREGISTER, &range_struct)) {
        error_setg(errp, "UFFDIO_UNREGISTER failed: %s", strerror(errno));
        goto out;
    }

    feature_mask = 1ULL << _UFFDIO_WAKE |
                   1ULL << _UFFDIO_COPY |
                   1ULL << _UFFDIO_ZEROPAGE;
    if ((reg_struct.ioctls & feature_mask) != feature_mask) {
        error_setg(errp, "Missing userfault map features: %" PRIx64,
                   (uint64_t)(~reg_struct.ioctls & feature_mask));
        goto out;
    }

    /* Success! */
    ret = true;
out:
    if (testarea) {
        munmap(testarea, pagesize);
    }
    if (ufd != -1) {
        close(ufd);
    }
    return ret;
}

/*
 * Disable huge pages on an area
 */
static int nhp_range(RAMBlock *rb, void *opaque)
{
    const char *block_name = qemu_ram_get_idstr(rb);
    void *host_addr = qemu_ram_get_host_addr(rb);
    ram_addr_t offset = qemu_ram_get_offset(rb);
    ram_addr_t length = rb->postcopy_length;
    trace_postcopy_nhp_range(block_name, host_addr, offset, length);

    /*
     * Before we do discards we need to ensure those discards really
     * do delete areas of the page, even if THP thinks a hugepage would
     * be a good idea, so force hugepages off.
     */
    qemu_madvise(host_addr, length, QEMU_MADV_NOHUGEPAGE);

    return 0;
}

/*
 * Userfault requires us to mark RAM as NOHUGEPAGE prior to discard
 * however leaving it until after precopy means that most of the precopy
 * data is still THPd
 */
int postcopy_ram_prepare_discard(MigrationIncomingState *mis)
{
    if (foreach_not_ignored_block(nhp_range, mis)) {
        return -1;
    }

    postcopy_state_set(POSTCOPY_INCOMING_DISCARD);

    return 0;
}

static uint64_t get_current_ns(void)
{
    return (uint64_t)qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
}

static void blocktime_latency_account(PostcopyBlocktimeContext *ctx,
                                      uint64_t time_us)
{
    /*
     * Convert time (in us) to bucket index it belongs.  Take extra caution
     * of time_us==0 even if normally rare - when happens put into bucket 0.
     */
    int index = time_us ? (63 - clz64(time_us)) : 0;

    assert(index >= 0);

    /* If it's too large, put into top bucket */
    if (index >= BLOCKTIME_LATENCY_BUCKET_N) {
        index = BLOCKTIME_LATENCY_BUCKET_N - 1;
    }

    ctx->latency_buckets[index]++;
}

typedef struct {
    PostcopyBlocktimeContext *ctx;
    uint64_t current;
    int affected_cpus;
    int affected_non_cpus;
} BlockTimeVCPUIter;

static void blocktime_cpu_list_iter_fn(gpointer data, gpointer user_data)
{
    BlockTimeVCPUIter *iter = user_data;
    PostcopyBlocktimeContext *ctx = iter->ctx;
    BlocktimeVCPUEntry *entry = data;
    uint64_t time_passed;
    int cpu = entry->cpu;

    /*
     * Time should never go back.. so when the fault is resolved it must be
     * later than when it was faulted.
     */
    assert(iter->current >= entry->fault_time);
    time_passed = iter->current - entry->fault_time;

    /* Latency buckets are in microseconds */
    blocktime_latency_account(ctx, time_passed / SCALE_US);

    if (cpu >= 0) {
        /*
         * If we resolved all pending faults on one vCPU due to this page
         * resolution, take a note.
         */
        if (--ctx->vcpu_faults_current[cpu] == 0) {
            ctx->vcpu_blocktime_total[cpu] += time_passed;
            iter->affected_cpus += 1;
        }
        trace_postcopy_blocktime_end_one(cpu, ctx->vcpu_faults_current[cpu]);
    } else {
        iter->affected_non_cpus++;
        ctx->non_vcpu_blocktime_total += time_passed;
        /*
         * We do not maintain how many pending non-vCPU faults because we
         * do not care about blocktime, only latency.
         */
        trace_postcopy_blocktime_end_one(-1, 0);
    }
}

/*
 * This function just provide calculated blocktime per cpu and trace it.
 * Total blocktime is calculated in mark_postcopy_blocktime_end.  It's
 * protected by @page_request_mutex.
 *
 * Assume we have 3 CPU
 *
 *      S1        E1           S1               E1
 * -----***********------------xxx***************------------------------> CPU1
 *
 *             S2                E2
 * ------------****************xxx---------------------------------------> CPU2
 *
 *                         S3            E3
 * ------------------------****xxx********-------------------------------> CPU3
 *
 * We have sequence S1,S2,E1,S3,S1,E2,E3,E1
 * S2,E1 - doesn't match condition due to sequence S1,S2,E1 doesn't include CPU3
 * S3,S1,E2 - sequence includes all CPUs, in this case overlap will be S1,E2 -
 *            it's a part of total blocktime.
 * S1 - here is last_begin
 * Legend of the picture is following:
 *              * - means blocktime per vCPU
 *              x - means overlapped blocktime (total blocktime)
 *
 * @addr: host virtual address
 */
static void mark_postcopy_blocktime_end(uintptr_t addr)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyBlocktimeContext *dc = mis->blocktime_ctx;
    MachineState *ms = MACHINE(qdev_get_machine());
    unsigned int smp_cpus = ms->smp.cpus;
    BlockTimeVCPUIter iter = {
        .current = get_current_ns(),
        .affected_cpus = 0,
        .affected_non_cpus = 0,
        .ctx = dc,
    };
    gpointer key = (gpointer)addr;
    GHashTable *table;
    GList *list;

    if (!dc) {
        return;
    }

    table = dc->vcpu_addr_hash;
    /* the address wasn't tracked at all? */
    list = g_hash_table_lookup(table, key);
    if (!list) {
        return;
    }

    /*
     * Loop over the set of vCPUs that got blocked on this addr, do the
     * blocktime accounting.  After that, remove the whole list.
     */
    g_list_foreach(list, blocktime_cpu_list_iter_fn, &iter);
    g_hash_table_remove(table, key);

    /*
     * If all vCPUs used to be down, and copying this page would free some
     * vCPUs, then the system-level blocktime ends here.
     */
    if (dc->smp_cpus_down == smp_cpus && iter.affected_cpus) {
        dc->total_blocktime += iter.current - dc->last_begin;
    }
    dc->smp_cpus_down -= iter.affected_cpus;

    trace_postcopy_blocktime_end(addr, iter.current, iter.affected_cpus,
                                 iter.affected_non_cpus);
}

static int qemu_ufd_copy_ioctl(MigrationIncomingState *mis, void *host_addr,
                               void *from_addr, uint64_t pagesize, RAMBlock *rb)
{
    int userfault_fd = mis->userfault_fd;
    int ret;

    if (from_addr) {
        ret = uffd_copy_page(userfault_fd, host_addr, from_addr, pagesize,
                             false);
    } else {
        ret = uffd_zero_page(userfault_fd, host_addr, pagesize, false);
    }
    if (!ret) {
        qemu_mutex_lock(&mis->page_request_mutex);
        ramblock_recv_bitmap_set_range(rb, host_addr,
                                       pagesize / qemu_target_page_size());
        /*
         * If this page resolves a page fault for a previous recorded faulted
         * address, take a special note to maintain the requested page list.
         */
        if (g_tree_lookup(mis->page_requested, host_addr)) {
            g_tree_remove(mis->page_requested, host_addr);
            int left_pages = qatomic_dec_fetch(&mis->page_requested_count);

            trace_postcopy_page_req_del(host_addr, mis->page_requested_count);
            /* Order the update of count and read of preempt status */
            smp_mb();
            if (mis->preempt_thread_status == PREEMPT_THREAD_QUIT &&
                left_pages == 0) {
                /*
                 * This probably means the main thread is waiting for us.
                 * Notify that we've finished receiving the last requested
                 * page.
                 */
                qemu_cond_signal(&mis->page_request_cond);
            }
        }
        mark_postcopy_blocktime_end((uintptr_t)host_addr);
        qemu_mutex_unlock(&mis->page_request_mutex);
    }
    return ret;
}

int postcopy_notify_shared_wake(RAMBlock *rb, uint64_t offset)
{
    int i;
    MigrationIncomingState *mis = migration_incoming_get_current();
    GArray *pcrfds = mis->postcopy_remote_fds;

    for (i = 0; i < pcrfds->len; i++) {
        struct PostCopyFD *cur = &g_array_index(pcrfds, struct PostCopyFD, i);
        int ret = cur->waker(cur, rb, offset);
        if (ret) {
            return ret;
        }
    }
    return 0;
}

/*
 * Place a host page (from) at (host) atomically
 * returns 0 on success
 */
int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        RAMBlock *rb)
{
    size_t pagesize = qemu_ram_pagesize(rb);
    int e;

    /* copy also acks to the kernel waking the stalled thread up
     * TODO: We can inhibit that ack and only do it if it was requested
     * which would be slightly cheaper, but we'd have to be careful
     * of the order of updating our page state.
     */
    e = qemu_ufd_copy_ioctl(mis, host, from, pagesize, rb);
    if (e) {
        return e;
    }

    trace_postcopy_place_page(host);
    return postcopy_notify_shared_wake(rb,
                                       qemu_ram_block_host_offset(rb, host));
}

/*
 * Place a zero page at (host) atomically
 * returns 0 on success
 */
int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                             RAMBlock *rb)
{
    size_t pagesize = qemu_ram_pagesize(rb);
    trace_postcopy_place_page_zero(host);

    /* Normal RAMBlocks can zero a page using UFFDIO_ZEROPAGE
     * but it's not available for everything (e.g. hugetlbpages)
     */
    if (qemu_ram_is_uf_zeroable(rb)) {
        int e;
        e = qemu_ufd_copy_ioctl(mis, host, NULL, pagesize, rb);
        if (e) {
            return e;
        }
        return postcopy_notify_shared_wake(rb,
                                           qemu_ram_block_host_offset(rb,
                                                                      host));
    } else {
        return postcopy_place_page(mis, host, mis->postcopy_tmp_zero_page, rb);
    }
}

#else
/* No target OS support, stubs just fail */
void fill_destination_postcopy_migration_info(MigrationInfo *info)
{
}

bool postcopy_ram_supported_by_host(MigrationIncomingState *mis, Error **errp)
{
    error_report("%s: No OS support", __func__);
    return false;
}

int postcopy_ram_incoming_init(MigrationIncomingState *mis, Error **errp)
{
    error_report("postcopy_ram_incoming_init: No OS support");
    return -1;
}

int postcopy_ram_incoming_cleanup(MigrationIncomingState *mis)
{
    g_assert_not_reached();
}

int postcopy_ram_prepare_discard(MigrationIncomingState *mis)
{
    g_assert_not_reached();
}

int postcopy_request_shared_page(struct PostCopyFD *pcfd, RAMBlock *rb,
                                 uint64_t client_addr, uint64_t rb_offset)
{
    g_assert_not_reached();
}

int postcopy_ram_incoming_setup(MigrationIncomingState *mis)
{
    g_assert_not_reached();
}

int postcopy_place_page(MigrationIncomingState *mis, void *host, void *from,
                        RAMBlock *rb)
{
    g_assert_not_reached();
}

int postcopy_place_page_zero(MigrationIncomingState *mis, void *host,
                        RAMBlock *rb)
{
    g_assert_not_reached();
}

int postcopy_wake_shared(struct PostCopyFD *pcfd,
                         uint64_t client_addr,
                         RAMBlock *rb)
{
    g_assert_not_reached();
}

void mark_postcopy_blocktime_begin(uintptr_t addr, uint32_t ptid,
                                   RAMBlock *rb)
{
}
#endif

/* ------------------------------------------------------------------------- */
void postcopy_temp_page_reset(PostcopyTmpPage *tmp_page)
{
    tmp_page->target_pages = 0;
    tmp_page->host_addr = NULL;
    /*
     * This is set to true when reset, and cleared as long as we received any
     * of the non-zero small page within this huge page.
     */
    tmp_page->all_zero = true;
}

/**
 * postcopy_discard_send_init: Called at the start of each RAMBlock before
 *   asking to discard individual ranges.
 *
 * @ms: The current migration state.
 * @offset: the bitmap offset of the named RAMBlock in the migration bitmap.
 * @name: RAMBlock that discards will operate on.
 */

/*
 * Current state of incoming postcopy; note this is not part of
 * MigrationIncomingState since it's state is used during cleanup
 * at the end as MIS is being freed.
 */
static PostcopyState incoming_postcopy_state;

PostcopyState  postcopy_state_get(void)
{
    return qatomic_load_acquire(&incoming_postcopy_state);
}

/* Set the state and return the old state */
PostcopyState postcopy_state_set(PostcopyState new_state)
{
    return qatomic_xchg(&incoming_postcopy_state, new_state);
}

static void postcopy_listen_thread_bh(void *opaque)
{
    MigrationState *s = migrate_get_current();
    MigrationIncomingState *mis = migration_incoming_get_current();

    migration_incoming_state_destroy();

    if (mis->state == MIGRATION_STATUS_FAILED && mis->exit_on_error) {
        WITH_QEMU_LOCK_GUARD(&s->error_mutex) {
            error_report_err(s->error);
            s->error = NULL;
        }
        /*
         * If something went wrong then we have a bad state so exit;
         * we only could have gotten here if something failed before
         * POSTCOPY_INCOMING_RUNNING (for example device load), otherwise
         * postcopy migration would pause inside qemu_loadvm_state_main().
         * Failing dirty-bitmaps won't fail the whole migration.
         */
        exit(1);
    }
}

/*
 * Triggered by a postcopy_listen command; this thread takes over reading
 * the input stream, leaving the main thread free to carry on loading the rest
 * of the device state (from RAM).
 */
static void *postcopy_listen_thread(void *opaque)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    QEMUFile *f = mis->from_src_file;
    int load_res;
    MigrationState *migr = migrate_get_current();
    Error *local_err = NULL;

    object_ref(OBJECT(migr));

    migrate_set_state(&mis->state, MIGRATION_STATUS_ACTIVE,
                      mis->to_src_file ? MIGRATION_STATUS_POSTCOPY_DEVICE :
                                         MIGRATION_STATUS_POSTCOPY_ACTIVE);
    qemu_event_set(&mis->thread_sync_event);
    trace_postcopy_ram_listen_thread_start();

    rcu_register_thread();
    /*
     * Because we're a thread and not a coroutine we can't yield
     * in qemu_file, and thus we must be blocking now.
     */
    qemu_file_set_blocking(f, true, &error_fatal);

    /* TODO: sanity check that only postcopiable data will be loaded here */
    load_res = qemu_loadvm_state_main(f, mis, &local_err);

    /*
     * This is tricky, but, mis->from_src_file can change after it
     * returns, when postcopy recovery happened. In the future, we may
     * want a wrapper for the QEMUFile handle.
     */
    f = mis->from_src_file;

    /* And non-blocking again so we don't block in any cleanup */
    qemu_file_set_blocking(f, false, &error_fatal);

    trace_postcopy_ram_listen_thread_exit();
    if (load_res < 0) {
        qemu_file_set_error(f, load_res);
        dirty_bitmap_mig_cancel_incoming();
        error_prepend(&local_err,
                      "loadvm failed during postcopy: %d: ", load_res);
        if (postcopy_state_get() == POSTCOPY_INCOMING_RUNNING &&
            !migrate_postcopy_ram() && migrate_dirty_bitmaps())
        {
            error_append_hint(&local_err,
                              "All state is migrated except dirty bitmaps."
                              " Some dirty bitmaps may be lost, but any"
                              " migrated dirty bitmaps are valid.");
            error_report_err(local_err);
        } else {
            /*
             * Something went fatally wrong and we have a bad state, QEMU will
             * exit depending on if postcopy-exit-on-error is true, but the
             * migration cannot be recovered.
             */
            migrate_set_error(migr, local_err);
            error_report_err(local_err);
            migrate_set_state(&mis->state, mis->state, MIGRATION_STATUS_FAILED);
            goto out;
        }
    }
    /*
     * This looks good, but it's possible that the device loading in the
     * main thread hasn't finished yet, and so we might not be in 'RUN'
     * state yet; wait for the end of the main thread.
     */
    qemu_event_wait(&mis->main_thread_load_event);

    /*
     * Device load in the main thread has finished, we should be in
     * POSTCOPY_ACTIVE now.
     */
    migrate_set_state(&mis->state, MIGRATION_STATUS_POSTCOPY_ACTIVE,
                                   MIGRATION_STATUS_COMPLETED);

out:
    rcu_unregister_thread();
    postcopy_state_set(POSTCOPY_INCOMING_END);

    migration_bh_schedule(postcopy_listen_thread_bh, NULL);

    object_unref(OBJECT(migr));

    return NULL;
}

int postcopy_incoming_setup(MigrationIncomingState *mis, Error **errp)
{
    /*
     * Sensitise RAM - can now generate requests for blocks that don't exist
     * However, at this point the CPU shouldn't be running, and the IO
     * shouldn't be doing anything yet so don't actually expect requests
     */
    if (migrate_postcopy_ram()) {
        if (postcopy_ram_incoming_setup(mis)) {
            postcopy_ram_incoming_cleanup(mis);
            error_setg(errp, "Failed to setup incoming postcopy RAM blocks");
            return -1;
        }
    }

    trace_loadvm_postcopy_handle_listen("after uffd");

    if (postcopy_notify(POSTCOPY_NOTIFY_INBOUND_LISTEN, errp)) {
        return -1;
    }

    mis->have_listen_thread = true;
    postcopy_thread_create(mis, &mis->listen_thread,
                           MIGRATION_THREAD_DST_LISTEN,
                           postcopy_listen_thread, QEMU_THREAD_JOINABLE);

    return 0;
}

int postcopy_incoming_cleanup(MigrationIncomingState *mis)
{
    int rc = 0;

    if (mis->have_listen_thread) {
        qemu_thread_join(&mis->listen_thread);
        mis->have_listen_thread = false;
    }

    if (migrate_postcopy_ram()) {
        rc = postcopy_ram_incoming_cleanup(mis);
    }

    return rc;
}
