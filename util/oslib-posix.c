/*
 * os-posix-lib.c
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2010 Red Hat, Inc.
 *
 * QEMU library functions on POSIX which are shared between QEMU and
 * the QEMU tools.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include <termios.h>

#include <glib/gprintf.h>

#include "system/system.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/madvise.h"
#include "qemu/sockets.h"
#include "qemu/thread.h"
#include <libgen.h>
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qemu/thread-context.h"
#include "qemu/main-loop.h"

#ifdef CONFIG_LINUX
#include <sys/syscall.h>
#endif
#ifdef __ANDROID__
#include <asm/unistd.h>
#endif

#ifdef __FreeBSD__
#include <sys/thr.h>
#include <sys/user.h>
#include <libutil.h>
#endif

#ifdef __NetBSD__
#include <lwp.h>
#endif

#include "qemu/memalign.h"
#include "qemu/mmap-alloc.h"

#define MAX_MEM_PREALLOC_THREAD_COUNT 16

struct MemsetThread;

static QLIST_HEAD(, MemsetContext) memset_contexts =
    QLIST_HEAD_INITIALIZER(memset_contexts);

typedef struct MemsetContext {
    bool all_threads_created;
    bool any_thread_failed;
    struct MemsetThread *threads;
    int num_threads;
    QLIST_ENTRY(MemsetContext) next;
} MemsetContext;

struct MemsetThread {
    char *addr;
    size_t numpages;
    size_t hpagesize;
    QemuThread pgthread;
    sigjmp_buf env;
    MemsetContext *context;
};
typedef struct MemsetThread MemsetThread;

/* used by sigbus_handler() */
static MemsetContext *sigbus_memset_context;
struct sigaction sigbus_oldact;
static QemuMutex sigbus_mutex;

static QemuMutex page_mutex;
static QemuCond page_cond;

int qemu_get_thread_id(void)
{
#if defined(__ANDROID__) && defined(__NR_gettid)
    return syscall(__NR_gettid);
#elif defined(__linux__)
    return syscall(SYS_gettid);
#elif defined(__FreeBSD__)
    /* thread id is up to INT_MAX */
    long tid;
    thr_self(&tid);
    return (int)tid;
#elif defined(__NetBSD__)
    return _lwp_self();
#elif defined(__OpenBSD__)
    return getthrid();
#else
    return getpid();
#endif
}

/* alloc shared memory pages */
void *qemu_anon_ram_alloc(size_t size, uint64_t *alignment, bool shared,
                          bool noreserve)
{
    const uint32_t qemu_map_flags = (shared ? QEMU_MAP_SHARED : 0) |
                                    (noreserve ? QEMU_MAP_NORESERVE : 0);
    size_t align = QEMU_VMALLOC_ALIGN;
#ifndef EMSCRIPTEN
    void *ptr = qemu_ram_mmap(-1, size, align, qemu_map_flags, 0);

    if (ptr == MAP_FAILED) {
        return NULL;
    }
#else
    /*
     * qemu_ram_mmap is not implemented for Emscripten. Use qemu_memalign
     * for the anonymous allocation. noreserve is ignored as there is no swap
     * space on Emscripten, and shared is ignored as there is no other
     * processes on Emscripten.
     */
    void *ptr = qemu_memalign(align, size);
#endif

    if (alignment) {
        *alignment = align;
    }

    trace_qemu_anon_ram_alloc(size, ptr);
    return ptr;
}

void qemu_anon_ram_free(void *ptr, size_t size)
{
    trace_qemu_anon_ram_free(ptr, size);
#ifndef EMSCRIPTEN
    qemu_ram_munmap(-1, ptr, size);
#else
    /*
     * qemu_ram_munmap is not implemented for Emscripten and qemu_memalign
     * was used for the allocation. Use the corresponding freeing function
     * here.
     */
    qemu_vfree(ptr);
#endif
}

bool qemu_set_blocking(int fd, bool block, Error **errp)
{
    g_autoptr(GError) err = NULL;

    if (!g_unix_set_fd_nonblocking(fd, !block, &err)) {
        error_setg_errno(errp, errno,
                         "Can't set file descriptor %d %s: %s", fd,
                         block ? "blocking" : "non-blocking",
                         err->message);
        return false;
    }

    return true;
}

int socket_set_fast_reuse(int fd)
{
    int val = 1, ret;

    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                     (const char *)&val, sizeof(val));

    assert(ret == 0);

    return ret;
}

void qemu_set_cloexec(int fd)
{
    int f;
    f = fcntl(fd, F_GETFD);
    assert(f != -1);
    f = fcntl(fd, F_SETFD, f | FD_CLOEXEC);
    assert(f != -1);
}

void qemu_clear_cloexec(int fd)
{
    int f;
    f = fcntl(fd, F_GETFD);
    assert(f != -1);
    f = fcntl(fd, F_SETFD, f & ~FD_CLOEXEC);
    assert(f != -1);
}

#ifdef CONFIG_LINUX
static void sigbus_handler(int signal, siginfo_t *siginfo, void *ctx)
#else /* CONFIG_LINUX */
static void sigbus_handler(int signal)
#endif /* CONFIG_LINUX */
{
    int i;

    if (sigbus_memset_context) {
        for (i = 0; i < sigbus_memset_context->num_threads; i++) {
            MemsetThread *thread = &sigbus_memset_context->threads[i];

            if (qemu_thread_is_self(&thread->pgthread)) {
                siglongjmp(thread->env, 1);
            }
        }
    }

#ifdef CONFIG_LINUX
    /*
     * We assume that the MCE SIGBUS handler could have been registered. We
     * should never receive BUS_MCEERR_AO on any of our threads, but only on
     * the main thread registered for PR_MCE_KILL_EARLY. Further, we should not
     * receive BUS_MCEERR_AR triggered by action of other threads on one of
     * our threads. So, no need to check for unrelated SIGBUS when seeing one
     * for our threads.
     *
     * We will forward to the MCE handler, which will either handle the SIGBUS
     * or reinstall the default SIGBUS handler and reraise the SIGBUS. The
     * default SIGBUS handler will crash the process, so we don't care.
     */
    if (sigbus_oldact.sa_flags & SA_SIGINFO) {
        sigbus_oldact.sa_sigaction(signal, siginfo, ctx);
        return;
    }
#endif /* CONFIG_LINUX */
    warn_report("qemu_prealloc_mem: unrelated SIGBUS detected and ignored");
}

static void *do_touch_pages(void *arg)
{
    MemsetThread *memset_args = (MemsetThread *)arg;
    sigset_t set, oldset;
    int ret = 0;

    /*
     * On Linux, the page faults from the loop below can cause mmap_sem
     * contention with allocation of the thread stacks.  Do not start
     * clearing until all threads have been created.
     */
    qemu_mutex_lock(&page_mutex);
    while (!memset_args->context->all_threads_created) {
        qemu_cond_wait(&page_cond, &page_mutex);
    }
    qemu_mutex_unlock(&page_mutex);

    /* unblock SIGBUS */
    sigemptyset(&set);
    sigaddset(&set, SIGBUS);
    pthread_sigmask(SIG_UNBLOCK, &set, &oldset);

    if (sigsetjmp(memset_args->env, 1)) {
        ret = -EFAULT;
    } else {
        char *addr = memset_args->addr;
        size_t numpages = memset_args->numpages;
        size_t hpagesize = memset_args->hpagesize;
        size_t i;
        for (i = 0; i < numpages; i++) {
            /*
             * Read & write back the same value, so we don't
             * corrupt existing user/app data that might be
             * stored.
             *
             * 'volatile' to stop compiler optimizing this away
             * to a no-op
             */
            *(volatile char *)addr = *addr;
            addr += hpagesize;
        }
    }
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
    return (void *)(uintptr_t)ret;
}

static void *do_madv_populate_write_pages(void *arg)
{
    MemsetThread *memset_args = (MemsetThread *)arg;
    const size_t size = memset_args->numpages * memset_args->hpagesize;
    char * const addr = memset_args->addr;
    int ret = 0;

    /* See do_touch_pages(). */
    qemu_mutex_lock(&page_mutex);
    while (!memset_args->context->all_threads_created) {
        qemu_cond_wait(&page_cond, &page_mutex);
    }
    qemu_mutex_unlock(&page_mutex);

    if (size && qemu_madvise(addr, size, QEMU_MADV_POPULATE_WRITE)) {
        ret = -errno;
    }
    return (void *)(uintptr_t)ret;
}

static inline int get_memset_num_threads(size_t hpagesize, size_t numpages,
                                         int max_threads)
{
    long host_procs = sysconf(_SC_NPROCESSORS_ONLN);
    int ret = 1;

    if (host_procs > 0) {
        ret = MIN(MIN(host_procs, MAX_MEM_PREALLOC_THREAD_COUNT), max_threads);
    }

    /* Especially with gigantic pages, don't create more threads than pages. */
    ret = MIN(ret, numpages);
    /* Don't start threads to prealloc comparatively little memory. */
    ret = MIN(ret, MAX(1, hpagesize * numpages / (64 * MiB)));

    /* In case sysconf() fails, we fall back to single threaded */
    return ret;
}

static int wait_and_free_mem_prealloc_context(MemsetContext *context)
{
    int i, ret = 0, tmp;

    for (i = 0; i < context->num_threads; i++) {
        tmp = (uintptr_t)qemu_thread_join(&context->threads[i].pgthread);

        if (tmp) {
            ret = tmp;
        }
    }
    g_free(context->threads);
    g_free(context);
    return ret;
}

static int touch_all_pages(char *area, size_t hpagesize, size_t numpages,
                           int max_threads, ThreadContext *tc, bool async,
                           bool use_madv_populate_write)
{
    static gsize initialized = 0;
    MemsetContext *context = g_malloc0(sizeof(MemsetContext));
    size_t numpages_per_thread, leftover;
    void *(*touch_fn)(void *);
    int ret, i = 0;
    char *addr = area;

    /*
     * Asynchronous preallocation is only allowed when using MADV_POPULATE_WRITE
     * and prealloc context for thread placement.
     */
    if (!use_madv_populate_write || !tc) {
        async = false;
    }

    context->num_threads =
        get_memset_num_threads(hpagesize, numpages, max_threads);

    if (g_once_init_enter(&initialized)) {
        qemu_mutex_init(&page_mutex);
        qemu_cond_init(&page_cond);
        g_once_init_leave(&initialized, 1);
    }

    if (use_madv_populate_write) {
        /*
         * Avoid creating a single thread for MADV_POPULATE_WRITE when
         * preallocating synchronously.
         */
        if (context->num_threads == 1 && !async) {
            ret = 0;
            if (qemu_madvise(area, hpagesize * numpages,
                             QEMU_MADV_POPULATE_WRITE)) {
                ret = -errno;
            }
            g_free(context);
            return ret;
        }
        touch_fn = do_madv_populate_write_pages;
    } else {
        touch_fn = do_touch_pages;
    }

    context->threads = g_new0(MemsetThread, context->num_threads);
    numpages_per_thread = numpages / context->num_threads;
    leftover = numpages % context->num_threads;
    for (i = 0; i < context->num_threads; i++) {
        context->threads[i].addr = addr;
        context->threads[i].numpages = numpages_per_thread + (i < leftover);
        context->threads[i].hpagesize = hpagesize;
        context->threads[i].context = context;
        if (tc) {
            thread_context_create_thread(tc, &context->threads[i].pgthread,
                                         "touch_pages",
                                         touch_fn, &context->threads[i],
                                         QEMU_THREAD_JOINABLE);
        } else {
            qemu_thread_create(&context->threads[i].pgthread, "touch_pages",
                               touch_fn, &context->threads[i],
                               QEMU_THREAD_JOINABLE);
        }
        addr += context->threads[i].numpages * hpagesize;
    }

    if (async) {
        /*
         * async requests currently require the BQL. Add it to the list and kick
         * preallocation off during qemu_finish_async_prealloc_mem().
         */
        assert(bql_locked());
        QLIST_INSERT_HEAD(&memset_contexts, context, next);
        return 0;
    }

    if (!use_madv_populate_write) {
        sigbus_memset_context = context;
    }

    qemu_mutex_lock(&page_mutex);
    context->all_threads_created = true;
    qemu_cond_broadcast(&page_cond);
    qemu_mutex_unlock(&page_mutex);

    ret = wait_and_free_mem_prealloc_context(context);

    if (!use_madv_populate_write) {
        sigbus_memset_context = NULL;
    }
    return ret;
}

bool qemu_finish_async_prealloc_mem(Error **errp)
{
    int ret = 0, tmp;
    MemsetContext *context, *next_context;

    /* Waiting for preallocation requires the BQL. */
    assert(bql_locked());
    if (QLIST_EMPTY(&memset_contexts)) {
        return true;
    }

    qemu_mutex_lock(&page_mutex);
    QLIST_FOREACH(context, &memset_contexts, next) {
        context->all_threads_created = true;
    }
    qemu_cond_broadcast(&page_cond);
    qemu_mutex_unlock(&page_mutex);

    QLIST_FOREACH_SAFE(context, &memset_contexts, next, next_context) {
        QLIST_REMOVE(context, next);
        tmp = wait_and_free_mem_prealloc_context(context);
        if (tmp) {
            ret = tmp;
        }
    }

    if (ret) {
        error_setg_errno(errp, -ret,
                         "qemu_prealloc_mem: preallocating memory failed");
        return false;
    }
    return true;
}

static bool madv_populate_write_possible(char *area, size_t pagesize)
{
    return !qemu_madvise(area, pagesize, QEMU_MADV_POPULATE_WRITE) ||
           errno != EINVAL;
}

bool qemu_prealloc_mem(int fd, char *area, size_t sz, int max_threads,
                       ThreadContext *tc, bool async, Error **errp)
{
    static gsize initialized;
    int ret;
#ifndef EMSCRIPTEN
    size_t hpagesize = qemu_fd_getpagesize(fd);
#else
    /*
     * mmap-alloc.c is excluded from Emscripten build, so qemu_fd_getpagesize
     * is unavailable. Fallback to the lower level implementation.
     */
    size_t hpagesize = qemu_real_host_page_size();
#endif
    size_t numpages = DIV_ROUND_UP(sz, hpagesize);
    bool use_madv_populate_write;
    struct sigaction act;
    bool rv = true;

    /*
     * Sense on every invocation, as MADV_POPULATE_WRITE cannot be used for
     * some special mappings, such as mapping /dev/mem.
     */
    use_madv_populate_write = madv_populate_write_possible(area, hpagesize);

    if (!use_madv_populate_write) {
        if (g_once_init_enter(&initialized)) {
            qemu_mutex_init(&sigbus_mutex);
            g_once_init_leave(&initialized, 1);
        }

        qemu_mutex_lock(&sigbus_mutex);
        memset(&act, 0, sizeof(act));
#ifdef CONFIG_LINUX
        act.sa_sigaction = &sigbus_handler;
        act.sa_flags = SA_SIGINFO;
#else /* CONFIG_LINUX */
        act.sa_handler = &sigbus_handler;
        act.sa_flags = 0;
#endif /* CONFIG_LINUX */

        ret = sigaction(SIGBUS, &act, &sigbus_oldact);
        if (ret) {
            qemu_mutex_unlock(&sigbus_mutex);
            error_setg_errno(errp, errno,
                "qemu_prealloc_mem: failed to install signal handler");
            return false;
        }
    }

    /* touch pages simultaneously */
    ret = touch_all_pages(area, hpagesize, numpages, max_threads, tc, async,
                          use_madv_populate_write);
    if (ret) {
        error_setg_errno(errp, -ret,
                         "qemu_prealloc_mem: preallocating memory failed");
        rv = false;
    }

    if (!use_madv_populate_write) {
        ret = sigaction(SIGBUS, &sigbus_oldact, NULL);
        if (ret) {
            /* Terminate QEMU since it can't recover from error */
            perror("qemu_prealloc_mem: failed to reinstall signal handler");
            exit(1);
        }
        qemu_mutex_unlock(&sigbus_mutex);
    }
    return rv;
}



size_t qemu_get_host_physmem(void)
{
#ifdef _SC_PHYS_PAGES
    long pages = sysconf(_SC_PHYS_PAGES);
    if (pages > 0) {
        if (pages > SIZE_MAX / qemu_real_host_page_size()) {
            return SIZE_MAX;
        } else {
            return pages * qemu_real_host_page_size();
        }
    }
#endif
    return 0;
}

int qemu_shm_alloc(size_t size, Error **errp)
{
#ifdef __ANDROID__
    g_autofree char *path = g_strdup_printf("%s/qemu-shm-XXXXXX",
                                            g_get_tmp_dir());
    int fd = mkstemp(path);

    if (fd < 0) {
        error_setg_errno(errp, errno,
                         "failed to create temporary shared memory file");
        return -1;
    }

    unlink(path);

    if (ftruncate(fd, size) == -1) {
        error_setg_errno(errp, errno,
                         "failed to resize temporary shared memory to %zu",
                         size);
        close(fd);
        return -1;
    }

    return fd;
#else
    g_autoptr(GString) shm_name = g_string_new(NULL);
    int fd, oflag, cur_sequence;
    static retro_atomic_int_t sequence;
    mode_t mode;

    cur_sequence = retro_atomic_fetch_add_int(&sequence, 1);

    /*
     * Let's use `mode = 0` because we don't want other processes to open our
     * memory unless we share the file descriptor with them.
     */
    mode = 0;
    oflag = O_RDWR | O_CREAT | O_EXCL;

    /*
     * Some operating systems allow creating anonymous POSIX shared memory
     * objects (e.g. FreeBSD provides the SHM_ANON constant), but this is not
     * defined by POSIX, so let's create a unique name.
     *
     * From Linux's shm_open(3) man-page:
     *   For  portable  use,  a shared  memory  object should be identified
     *   by a name of the form /somename;"
     */
    g_string_printf(shm_name, "/qemu-" FMT_pid "-shm-%d", getpid(),
                    cur_sequence);

    fd = shm_open(shm_name->str, oflag, mode);
    if (fd < 0) {
        error_setg_errno(errp, errno,
                         "failed to create POSIX shared memory");
        return -1;
    }

    /*
     * We have the file descriptor, so we no longer need to expose the
     * POSIX shared memory object. However it will remain allocated as long as
     * there are file descriptors pointing to it.
     */
    shm_unlink(shm_name->str);

    if (ftruncate(fd, size) == -1) {
        error_setg_errno(errp, errno,
                         "failed to resize POSIX shared memory to %zu", size);
        close(fd);
        return -1;
    }

    return fd;
#endif
}
