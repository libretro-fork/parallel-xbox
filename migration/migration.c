/*
 * QEMU live migration
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "migration/blocker.h"
#include "system/runstate.h"
#include "system/system.h"
#include "system/cpu-throttle.h"
#include "rdma.h"
#include "ram.h"
#include "migration/cpr.h"
#include "migration/global_state.h"
#include "migration/misc.h"
#include "migration.h"
#include "migration-stats.h"
#include "savevm.h"
#include "qemu-file.h"
#include "migration/vmstate.h"
#include "block/block.h"
#include "qapi/error.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-migration.h"
#include "qapi/qapi-visit-sockets.h"
#include "qapi/qapi-commands-migration.h"
#include "qapi/qapi-events-migration.h"
#include "qapi/qmp/qerror.h"
#include "qobject/qnull.h"
#include "qemu/rcu.h"
#include "postcopy-ram.h"
#include "qemu/thread.h"
#include "trace.h"
#include "exec/target_page.h"
#include "io/channel-buffer.h"
#include "io/channel-tls.h"
#include "migration/colo.h"
#include "hw/boards.h"
#include "monitor/monitor.h"
#include "net/announce.h"
#include "qemu/queue.h"
#include "multifd.h"
#include "qemu/yank.h"
#include "system/cpus.h"
#include "yank_functions.h"
#include "system/qtest.h"
#include "options.h"
#include "qemu/sockets.h"
#include "system/kvm.h"

#define NOTIFIER_ELEM_INIT(array, elem)    \
    [elem] = NOTIFIER_WITH_RETURN_LIST_INITIALIZER((array)[elem])

#define INMIGRATE_DEFAULT_EXIT_ON_ERROR true

static GSList *migration_state_notifiers[MIG_MODE__MAX];

/* Messages sent on the return path from destination to source */
enum mig_rp_message_type {
    MIG_RP_MSG_INVALID = 0,  /* Must be 0 */
    MIG_RP_MSG_SHUT,         /* sibling will not send any more RP messages */
    MIG_RP_MSG_PONG,         /* Response to a PING; data (seq: be32 ) */

    MIG_RP_MSG_REQ_PAGES_ID, /* data (start: be64, len: be32, id: string) */
    MIG_RP_MSG_REQ_PAGES,    /* data (start: be64, len: be32) */
    MIG_RP_MSG_RECV_BITMAP,  /* send recved_bitmap back to source */
    MIG_RP_MSG_RESUME_ACK,   /* tell source that we are ready to resume */
    MIG_RP_MSG_SWITCHOVER_ACK, /* Tell source it's OK to do switchover */

    MIG_RP_MSG_MAX
};

/* Migration channel types */
enum { CH_MAIN, CH_MULTIFD, CH_POSTCOPY };

/* When we add fault tolerance, we could have several
   migrations at once.  For now we don't need to add
   dynamic creation of migration */

static MigrationState *current_migration;
static MigrationIncomingState *current_incoming;

static GSList *migration_blockers[MIG_MODE__MAX];

static bool migration_object_check(MigrationState *ms, Error **errp);
static void migrate_hup_delete(MigrationState *s);

static gint page_request_addr_cmp(gconstpointer ap, gconstpointer bp)
{
    uintptr_t a = (uintptr_t) ap, b = (uintptr_t) bp;

    return (a > b) - (a < b);
}

void migration_object_init(void)
{
    /* This can only be called once. */
    assert(!current_migration);
    current_migration = MIGRATION_OBJ(object_new(TYPE_MIGRATION));

    /*
     * Init the migrate incoming object as well no matter whether
     * we'll use it or not.
     */
    assert(!current_incoming);
    current_incoming = g_new0(MigrationIncomingState, 1);
    current_incoming->state = MIGRATION_STATUS_NONE;
    current_incoming->postcopy_remote_fds =
        g_array_new(FALSE, TRUE, sizeof(struct PostCopyFD));
    qemu_mutex_init(&current_incoming->rp_mutex);
    qemu_mutex_init(&current_incoming->postcopy_prio_thread_mutex);
    qemu_event_init(&current_incoming->main_thread_load_event, false);
    qemu_sem_init(&current_incoming->postcopy_pause_sem_dst, 0);
    qemu_sem_init(&current_incoming->postcopy_pause_sem_fault, 0);
    qemu_sem_init(&current_incoming->postcopy_pause_sem_fast_load, 0);
    qemu_sem_init(&current_incoming->postcopy_qemufile_dst_done, 0);

    qemu_mutex_init(&current_incoming->page_request_mutex);
    qemu_cond_init(&current_incoming->page_request_cond);
    current_incoming->page_requested = g_tree_new(page_request_addr_cmp);

    current_incoming->exit_on_error = INMIGRATE_DEFAULT_EXIT_ON_ERROR;

    migration_object_check(current_migration, &error_fatal);

    ram_mig_init();
    dirty_bitmap_mig_init();
    cpr_exec_init();

    /* Initialize cpu throttle timers */
    cpu_throttle_init();
}

typedef struct {
    QEMUBH *bh;
    QEMUBHFunc *cb;
    void *opaque;
} MigrationBH;

static void migration_bh_dispatch_bh(void *opaque)
{
    MigrationState *s = migrate_get_current();
    MigrationBH *migbh = opaque;

    /* cleanup this BH */
    qemu_bh_delete(migbh->bh);
    migbh->bh = NULL;

    /* dispatch the other one */
    migbh->cb(migbh->opaque);
    object_unref(OBJECT(s));

    g_free(migbh);
}

void migration_bh_schedule(QEMUBHFunc *cb, void *opaque)
{
    MigrationState *s = migrate_get_current();
    MigrationBH *migbh = g_new0(MigrationBH, 1);
    QEMUBH *bh = qemu_bh_new(migration_bh_dispatch_bh, migbh);

    /* Store these to dispatch when the BH runs */
    migbh->bh = bh;
    migbh->cb = cb;
    migbh->opaque = opaque;

    /*
     * Ref the state for bh, because it may be called when
     * there're already no other refs
     */
    object_ref(OBJECT(s));
    qemu_bh_schedule(bh);
}

/* For outgoing */
MigrationState *migrate_get_current(void)
{
    /* This can only be called after the object created. */
    assert(current_migration);
    return current_migration;
}

MigrationIncomingState *migration_incoming_get_current(void)
{
    assert(current_incoming);
    return current_incoming;
}

void migration_incoming_transport_cleanup(MigrationIncomingState *mis)
{
    if (mis->socket_address_list) {
        qapi_free_SocketAddressList(mis->socket_address_list);
        mis->socket_address_list = NULL;
    }

    if (mis->transport_cleanup) {
        mis->transport_cleanup(mis->transport_data);
        mis->transport_data = mis->transport_cleanup = NULL;
    }
}

void migration_incoming_state_destroy(void)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    PostcopyState ps = postcopy_state_get();

    if (ps != POSTCOPY_INCOMING_NONE) {
        postcopy_incoming_cleanup(mis);
    }

    /*
     * RAM state cleanup needs to happen after multifd cleanup, because
     * multifd threads can use some of its states (receivedmap).
     * The VFIO load_cleanup() implementation is BQL-sensitive. It requires
     * BQL must NOT be taken when recycling load threads, so that it won't
     * block the load threads from making progress on address space
     * modification operations.
     *
     * To make it work, we could try to not take BQL for all load_cleanup(),
     * or conditionally unlock BQL only if bql_locked() in VFIO.
     *
     * Since most existing call sites take BQL for load_cleanup(), make
     * it simple by taking BQL always as the rule, so that VFIO can unlock
     * BQL and retake unconditionally.
     */
    assert(bql_locked());
    qemu_loadvm_state_cleanup(mis);

    if (mis->to_src_file) {
        /* Tell source that we are done */
        migrate_send_rp_shut(mis, qemu_file_get_error(mis->from_src_file) != 0);
        qemu_fclose(mis->to_src_file);
        mis->to_src_file = NULL;
    }

    if (mis->from_src_file) {
        migration_ioc_unregister_yank_from_file(mis->from_src_file);
        qemu_fclose(mis->from_src_file);
        mis->from_src_file = NULL;
    }
    if (mis->postcopy_remote_fds) {
        g_array_free(mis->postcopy_remote_fds, TRUE);
        mis->postcopy_remote_fds = NULL;
    }

    migration_incoming_transport_cleanup(mis);
    qemu_event_reset(&mis->main_thread_load_event);

    if (mis->page_requested) {
        g_tree_destroy(mis->page_requested);
        mis->page_requested = NULL;
    }

    if (mis->postcopy_qemufile_dst) {
        migration_ioc_unregister_yank_from_file(mis->postcopy_qemufile_dst);
        qemu_fclose(mis->postcopy_qemufile_dst);
        mis->postcopy_qemufile_dst = NULL;
    }

    cpr_set_incoming_mode(MIG_MODE_NONE);
    yank_unregister_instance(MIGRATION_YANK_INSTANCE);
}

static void migrate_generate_event(MigrationStatus new_state)
{
    if (migrate_events()) {
        qapi_event_send_migration(new_state);
    }
}

/*
 * Send a message on the return channel back to the source
 * of the migration.
 */
static int migrate_send_rp_message(MigrationIncomingState *mis,
                                   enum mig_rp_message_type message_type,
                                   uint16_t len, void *data)
{
    int ret = 0;

    trace_migrate_send_rp_message((int)message_type, len);
    QEMU_LOCK_GUARD(&mis->rp_mutex);

    /*
     * It's possible that the file handle got lost due to network
     * failures.
     */
    if (!mis->to_src_file) {
        ret = -EIO;
        return ret;
    }

    qemu_put_be16(mis->to_src_file, (unsigned int)message_type);
    qemu_put_be16(mis->to_src_file, len);
    qemu_put_buffer(mis->to_src_file, data, len);
    return qemu_fflush(mis->to_src_file);
}

/* Request one page from the source VM at the given start address.
 *   rb: the RAMBlock to request the page in
 *   Start: Address offset within the RB
 *   Len: Length in bytes required - must be a multiple of pagesize
 */
int migrate_send_rp_message_req_pages(MigrationIncomingState *mis,
                                      RAMBlock *rb, ram_addr_t start)
{
    uint8_t bufc[12 + 1 + 255]; /* start (8), len (4), rbname up to 256 */
    size_t msglen = 12; /* start + len */
    size_t len = qemu_ram_pagesize(rb);
    enum mig_rp_message_type msg_type;
    const char *rbname;
    int rbname_len;

    *(uint64_t *)bufc = cpu_to_be64((uint64_t)start);
    *(uint32_t *)(bufc + 8) = cpu_to_be32((uint32_t)len);

    /*
     * We maintain the last ramblock that we requested for page.  Note that we
     * don't need locking because this function will only be called within the
     * postcopy ram fault thread.
     */
    if (rb != mis->last_rb) {
        mis->last_rb = rb;

        rbname = qemu_ram_get_idstr(rb);
        rbname_len = strlen(rbname);

        assert(rbname_len < 256);

        bufc[msglen++] = rbname_len;
        memcpy(bufc + msglen, rbname, rbname_len);
        msglen += rbname_len;
        msg_type = MIG_RP_MSG_REQ_PAGES_ID;
    } else {
        msg_type = MIG_RP_MSG_REQ_PAGES;
    }

    return migrate_send_rp_message(mis, msg_type, msglen, bufc);
}

static bool migration_colo_enabled;
bool migration_incoming_colo_enabled(void)
{
    return migration_colo_enabled;
}

void migration_incoming_disable_colo(void)
{
    ram_block_discard_disable(false);
    migration_colo_enabled = false;
}

int migration_incoming_enable_colo(Error **errp)
{
#ifndef CONFIG_REPLICATION
    error_setg(errp, "ENABLE_COLO command come in migration stream, but the "
               "replication module is not built in");
    return -ENOTSUP;
#endif

    if (!migrate_colo()) {
        error_setg(errp, "ENABLE_COLO command come in migration stream"
                   ", but x-colo capability is not set");
        return -EINVAL;
    }

    if (ram_block_discard_disable(true)) {
        error_setg(errp, "COLO: cannot disable RAM discard");
        return -EBUSY;
    }
    migration_colo_enabled = true;
    return 0;
}

/*
 * Send a 'SHUT' message on the return channel with the given value
 * to indicate that we've finished with the RP.  Non-0 value indicates
 * error.
 */
void migrate_send_rp_shut(MigrationIncomingState *mis,
                          uint32_t value)
{
    uint32_t buf;

    buf = cpu_to_be32(value);
    migrate_send_rp_message(mis, MIG_RP_MSG_SHUT, sizeof(buf), &buf);
}

/*
 * Send a 'PONG' message on the return channel with the given value
 * (normally in response to a 'PING')
 */
void migrate_send_rp_pong(MigrationIncomingState *mis,
                          uint32_t value)
{
    uint32_t buf;

    buf = cpu_to_be32(value);
    migrate_send_rp_message(mis, MIG_RP_MSG_PONG, sizeof(buf), &buf);
}

void migrate_send_rp_recv_bitmap(MigrationIncomingState *mis,
                                 char *block_name)
{
    char buf[512];
    int len;
    int64_t res;

    /*
     * First, we send the header part. It contains only the len of
     * idstr, and the idstr itself.
     */
    len = strlen(block_name);
    buf[0] = len;
    memcpy(buf + 1, block_name, len);

    if (mis->state != MIGRATION_STATUS_POSTCOPY_RECOVER) {
        error_report("%s: MSG_RP_RECV_BITMAP only used for recovery",
                     __func__);
        return;
    }

    migrate_send_rp_message(mis, MIG_RP_MSG_RECV_BITMAP, len + 1, buf);

    /*
     * Next, we dump the received bitmap to the stream.
     *
     * TODO: currently we are safe since we are the only one that is
     * using the to_src_file handle (fault thread is still paused),
     * and it's ok even not taking the mutex. However the best way is
     * to take the lock before sending the message header, and release
     * the lock after sending the bitmap.
     */
    qemu_mutex_lock(&mis->rp_mutex);
    res = ramblock_recv_bitmap_send(mis->to_src_file, block_name);
    qemu_mutex_unlock(&mis->rp_mutex);

    trace_migrate_send_rp_recv_bitmap(block_name, res);
}

void migrate_send_rp_resume_ack(MigrationIncomingState *mis, uint32_t value)
{
    uint32_t buf;

    buf = cpu_to_be32(value);
    migrate_send_rp_message(mis, MIG_RP_MSG_RESUME_ACK, sizeof(buf), &buf);
}

bool migration_is_running(void)
{
    MigrationState *s = current_migration;

    if (!s) {
        return false;
    }

    switch (s->state) {
    case MIGRATION_STATUS_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_DEVICE:
    case MIGRATION_STATUS_POSTCOPY_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_PAUSED:
    case MIGRATION_STATUS_POSTCOPY_RECOVER_SETUP:
    case MIGRATION_STATUS_POSTCOPY_RECOVER:
    case MIGRATION_STATUS_SETUP:
    case MIGRATION_STATUS_PRE_SWITCHOVER:
    case MIGRATION_STATUS_DEVICE:
    case MIGRATION_STATUS_WAIT_UNPLUG:
    case MIGRATION_STATUS_CANCELLING:
    case MIGRATION_STATUS_COLO:
        return true;
    default:
        return false;
    }
}

/* shared migration helpers */

void migrate_set_state(MigrationStatus *state, MigrationStatus old_state,
                       MigrationStatus new_state)
{
    assert(new_state < MIGRATION_STATUS__MAX);
    if (qatomic_cmpxchg(state, old_state, new_state) == old_state) {
        trace_migrate_set_state(MigrationStatus_str(new_state));
        migrate_generate_event(new_state);
    }
}

void migrate_set_error(MigrationState *s, const Error *error)
{
    QEMU_LOCK_GUARD(&s->error_mutex);

    trace_migrate_error(error_get_pretty(error));

    if (!s->error) {
        s->error = error_copy(error);
    }
}

bool migrate_has_error(MigrationState *s)
{
    /* The lock is not helpful here, but still follow the rule */
    QEMU_LOCK_GUARD(&s->error_mutex);
    return qatomic_read(&s->error);
}

void migration_cancel(void)
{
    MigrationState *s = migrate_get_current();
    int old_state ;
    bool setup = (s->state == MIGRATION_STATUS_SETUP);

    trace_migration_cancel();

    WITH_QEMU_LOCK_GUARD(&s->qemu_file_lock) {
        if (s->rp_state.from_dst_file) {
            /* shutdown the rp socket, so causing the rp thread to shutdown */
            qemu_file_shutdown(s->rp_state.from_dst_file);
        }
    }

    do {
        old_state = s->state;
        if (!migration_is_running()) {
            break;
        }
        /* If the migration is paused, kick it out of the pause */
        if (old_state == MIGRATION_STATUS_PRE_SWITCHOVER) {
            qemu_event_set(&s->pause_event);
        }
        migrate_set_state(&s->state, old_state, MIGRATION_STATUS_CANCELLING);
    } while (s->state != MIGRATION_STATUS_CANCELLING);

    /*
     * If we're unlucky the migration code might be stuck somewhere in a
     * send/write while the network has failed and is waiting to timeout;
     * if we've got shutdown(2) available then we can force it to quit.
     */
    if (s->state == MIGRATION_STATUS_CANCELLING) {
        WITH_QEMU_LOCK_GUARD(&s->qemu_file_lock) {
            if (s->to_dst_file) {
                qemu_file_shutdown(s->to_dst_file);
            }
        }
    }

    /*
     * If qmp_migrate_finish has not been called, then there is no path that
     * will complete the cancellation.  Do it now.
     */
    if (setup && !s->to_dst_file) {
        migrate_set_state(&s->state, MIGRATION_STATUS_CANCELLING,
                          MIGRATION_STATUS_CANCELLED);
        cpr_state_close();
        migrate_hup_delete(s);
    }
}

static void add_notifiers(NotifierWithReturn *notify, unsigned modes)
{
    for (MigMode mode = 0; mode < MIG_MODE__MAX; mode++) {
        if (modes & BIT(mode)) {
            migration_state_notifiers[mode] =
                g_slist_prepend(migration_state_notifiers[mode], notify);
        }
    }
}

void migration_add_notifier_modes(NotifierWithReturn *notify,
                                  MigrationNotifyFunc func, unsigned modes)
{
    notify->notify = (NotifierWithReturnFunc)func;
    add_notifiers(notify, modes);
}

void migration_add_notifier_mode(NotifierWithReturn *notify,
                                 MigrationNotifyFunc func, MigMode mode)
{
    migration_add_notifier_modes(notify, func, BIT(mode));
}

int migration_call_notifiers(MigrationState *s, MigrationEventType type,
                             Error **errp)
{
    MigMode mode = s->parameters.mode;
    MigrationEvent e;
    NotifierWithReturn *notifier;
    GSList *elem, *next;
    int ret;

    e.type = type;

    for (elem = migration_state_notifiers[mode]; elem; elem = next) {
        next = elem->next;
        notifier = (NotifierWithReturn *)elem->data;
        ret = notifier->notify(notifier, &e, errp);
        if (ret) {
            assert(type == MIG_EVENT_PRECOPY_SETUP);
            return ret;
        }
    }

    return 0;
}

bool migration_in_postcopy(void)
{
    MigrationState *s = migrate_get_current();

    switch (s->state) {
    case MIGRATION_STATUS_POSTCOPY_DEVICE:
    case MIGRATION_STATUS_POSTCOPY_ACTIVE:
    case MIGRATION_STATUS_POSTCOPY_PAUSED:
    case MIGRATION_STATUS_POSTCOPY_RECOVER_SETUP:
    case MIGRATION_STATUS_POSTCOPY_RECOVER:
        return true;
    default:
        return false;
    }
}

bool migration_incoming_postcopy_advised(void)
{
    PostcopyState ps = postcopy_state_get();

    return ps >= POSTCOPY_INCOMING_ADVISE && ps < POSTCOPY_INCOMING_END;
}

int migrate_init(MigrationState *s, Error **errp)
{
    int ret;

    ret = qemu_savevm_state_prepare(errp);
    if (ret) {
        return ret;
    }

    /*
     * Reinitialise all migration state, except
     * parameters/capabilities that the user set, and
     * locks.
     */
    s->to_dst_file = NULL;
    s->state = MIGRATION_STATUS_NONE;
    s->rp_state.from_dst_file = NULL;
    s->mbps = 0.0;
    s->pages_per_second = 0.0;
    s->downtime = 0;
    s->expected_downtime = 0;
    s->setup_time = 0;
    s->start_postcopy = false;
    s->migration_thread_running = false;
    error_free(s->error);
    s->error = NULL;

    if (should_send_vmdesc()) {
        s->vmdesc = json_writer_new(false);
    }

    migrate_set_state(&s->state, MIGRATION_STATUS_NONE, MIGRATION_STATUS_SETUP);

    s->start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    s->total_time = 0;
    s->vm_old_state = -1;
    s->iteration_initial_bytes = 0;
    s->threshold_size = 0;
    s->switchover_acked = false;
    s->rdma_migration = false;
    /*
     * set mig_stats memory to zero for a new migration
     */
    memset(&mig_stats, 0, sizeof(mig_stats));
    migration_reset_vfio_bytes_transferred();

    s->postcopy_package_loaded = false;
    qemu_event_reset(&s->postcopy_package_loaded_event);

    return 0;
}

static bool is_busy(Error **reasonp, Error **errp)
{
    ERRP_GUARD();

    /* Snapshots are similar to migrations, so check RUN_STATE_SAVE_VM too. */
    if (runstate_check(RUN_STATE_SAVE_VM) || migration_is_running()) {
        error_propagate_prepend(errp, *reasonp,
                                "disallowing migration blocker "
                                "(migration/snapshot in progress) for: ");
        *reasonp = NULL;
        return true;
    }
    return false;
}

static bool is_only_migratable(Error **reasonp, unsigned modes, Error **errp)
{
    ERRP_GUARD();

    if (only_migratable && (modes & BIT(MIG_MODE_NORMAL))) {
        error_propagate_prepend(errp, *reasonp,
                                "disallowing migration blocker "
                                "(--only-migratable) for: ");
        *reasonp = NULL;
        return true;
    }
    return false;
}

static int add_blockers(Error **reasonp, unsigned modes, Error **errp)
{
    for (MigMode mode = 0; mode < MIG_MODE__MAX; mode++) {
        if (modes & BIT(mode)) {
            migration_blockers[mode] = g_slist_prepend(migration_blockers[mode],
                                                       *reasonp);
        }
    }
    return 0;
}

int migrate_add_blocker_modes(Error **reasonp, unsigned modes, Error **errp)
{
    if (is_only_migratable(reasonp, modes, errp)) {
        return -EACCES;
    } else if (is_busy(reasonp, errp)) {
        return -EBUSY;
    }
    return add_blockers(reasonp, modes, errp);
}

void migrate_del_blocker(Error **reasonp)
{
    if (*reasonp) {
        for (MigMode mode = 0; mode < MIG_MODE__MAX; mode++) {
            migration_blockers[mode] = g_slist_remove(migration_blockers[mode],
                                                      *reasonp);
        }
        error_free(*reasonp);
        *reasonp = NULL;
    }
}


static void migrate_hup_delete(MigrationState *s)
{
    if (s->hup_source) {
        g_source_destroy(s->hup_source);
        g_source_unref(s->hup_source);
        s->hup_source = NULL;
    }
}

int migration_rp_wait(MigrationState *s)
{
    /* If migration has failure already, ignore the wait */
    if (migrate_has_error(s)) {
        return -1;
    }

    qemu_sem_wait(&s->rp_state.rp_sem);

    /* After wait, double check that there's no failure */
    if (migrate_has_error(s)) {
        return -1;
    }

    return 0;
}

static inline void
migration_wait_main_channel(MigrationState *ms)
{
    /* Wait until one PONG message received */
    qemu_sem_wait(&ms->rp_state.rp_pong_acks);
}

typedef enum MigThrError {
    /* No error detected */
    MIG_THR_ERR_NONE = 0,
    /* Detected error, but resumed successfully */
    MIG_THR_ERR_RECOVERED = 1,
    /* Detected fatal error, need to exit */
    MIG_THR_ERR_FATAL = 2,
} MigThrError;

static void update_iteration_initial_status(MigrationState *s)
{
    /*
     * Update these three fields at the same time to avoid mismatch info lead
     * wrong speed calculation.
     */
    s->iteration_start_time = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    s->iteration_initial_bytes = migration_transferred_bytes();
    s->iteration_initial_pages = ram_get_total_transferred_pages();
}

static void migration_update_counters(MigrationState *s,
                                      int64_t current_time)
{
    uint64_t transferred, transferred_pages, time_spent;
    uint64_t current_bytes; /* bytes transferred since the beginning */
    uint64_t switchover_bw;
    /* Expected bandwidth when switching over to destination QEMU */
    double expected_bw_per_ms;
    double bandwidth;

    if (current_time < s->iteration_start_time + BUFFER_DELAY) {
        return;
    }

    switchover_bw = migrate_avail_switchover_bandwidth();
    current_bytes = migration_transferred_bytes();
    transferred = current_bytes - s->iteration_initial_bytes;
    time_spent = current_time - s->iteration_start_time;
    bandwidth = (double)transferred / time_spent;

    if (switchover_bw) {
        /*
         * If the user specified a switchover bandwidth, let's trust the
         * user so that can be more accurate than what we estimated.
         */
        expected_bw_per_ms = switchover_bw / 1000;
    } else {
        /* If the user doesn't specify bandwidth, we use the estimated */
        expected_bw_per_ms = bandwidth;
    }

    s->threshold_size = expected_bw_per_ms * migrate_downtime_limit();

    s->mbps = (((double) transferred * 8.0) /
               ((double) time_spent / 1000.0)) / 1000.0 / 1000.0;

    transferred_pages = ram_get_total_transferred_pages() -
                            s->iteration_initial_pages;
    s->pages_per_second = (double) transferred_pages /
                             (((double) time_spent / 1000.0));

    /*
     * if we haven't sent anything, we don't want to
     * recalculate. 10000 is a small enough number for our purposes
     */
    if (stat64_get(&mig_stats.dirty_pages_rate) &&
        transferred > 10000) {
        s->expected_downtime =
            stat64_get(&mig_stats.dirty_bytes_last_sync) / expected_bw_per_ms;
    }

    migration_rate_reset();

    update_iteration_initial_status(s);

    trace_migrate_transferred(transferred, time_spent,
                              /* Both in unit bytes/ms */
                              bandwidth, switchover_bw / 1000,
                              s->threshold_size);
}

/* Migration thread iteration status */
typedef enum {
    MIG_ITERATE_RESUME,         /* Resume current iteration */
    MIG_ITERATE_SKIP,           /* Skip current iteration */
    MIG_ITERATE_BREAK,          /* Break the loop */
} MigIterateState;

void migration_consume_urgent_request(void)
{
    qemu_sem_wait(&migrate_get_current()->rate_limit_sem);
}

/* Returns true if the rate limiting was broken by an urgent request */
bool migration_rate_limit(void)
{
    int64_t now = qemu_clock_get_ms(QEMU_CLOCK_REALTIME);
    MigrationState *s = migrate_get_current();

    bool urgent = false;
    migration_update_counters(s, now);
    if (migration_rate_exceeded(s->to_dst_file)) {

        if (qemu_file_get_error(s->to_dst_file)) {
            return false;
        }
        /*
         * Wait for a delay to do rate limiting OR
         * something urgent to post the semaphore.
         */
        int ms = s->iteration_start_time + BUFFER_DELAY - now;
        trace_migration_rate_limit_pre(ms);
        if (qemu_sem_timedwait(&s->rate_limit_sem, ms) == 0) {
            /*
             * We were woken by one or more urgent things but
             * the timedwait will have consumed one of them.
             * The service routine for the urgent wake will dec
             * the semaphore itself for each item it consumes,
             * so add this one we just eat back.
             */
            qemu_sem_post(&s->rate_limit_sem);
            urgent = true;
        }
        trace_migration_rate_limit_post(urgent);
    }
    return urgent;
}

static void migration_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->user_creatable = false;
    device_class_set_props_n(dc, migration_properties,
                             migration_properties_count);
}

static void migration_instance_finalize(Object *obj)
{
    MigrationState *ms = MIGRATION_OBJ(obj);

    qemu_mutex_destroy(&ms->error_mutex);
    qemu_mutex_destroy(&ms->qemu_file_lock);
    qemu_sem_destroy(&ms->wait_unplug_sem);
    qemu_sem_destroy(&ms->rate_limit_sem);
    qemu_event_destroy(&ms->pause_event);
    qemu_sem_destroy(&ms->postcopy_pause_sem);
    qemu_sem_destroy(&ms->rp_state.rp_sem);
    qemu_sem_destroy(&ms->rp_state.rp_pong_acks);
    qemu_sem_destroy(&ms->postcopy_qemufile_src_sem);
    error_free(ms->error);
    qemu_event_destroy(&ms->postcopy_package_loaded_event);
}

static void migration_instance_init(Object *obj)
{
    MigrationState *ms = MIGRATION_OBJ(obj);

    ms->state = MIGRATION_STATUS_NONE;
    ms->mbps = -1;
    ms->pages_per_second = -1;
    qemu_event_init(&ms->pause_event, false);
    qemu_mutex_init(&ms->error_mutex);

    migrate_params_init(&ms->parameters);

    qemu_sem_init(&ms->postcopy_pause_sem, 0);
    qemu_sem_init(&ms->rp_state.rp_sem, 0);
    qemu_sem_init(&ms->rp_state.rp_pong_acks, 0);
    qemu_sem_init(&ms->rate_limit_sem, 0);
    qemu_sem_init(&ms->wait_unplug_sem, 0);
    qemu_sem_init(&ms->postcopy_qemufile_src_sem, 0);
    qemu_mutex_init(&ms->qemu_file_lock);
    qemu_event_init(&ms->postcopy_package_loaded_event, 0);
}

/*
 * Return true if check pass, false otherwise. Error will be put
 * inside errp if provided.
 */
static bool migration_object_check(MigrationState *ms, Error **errp)
{
    /* Assuming all off */
    bool old_caps[MIGRATION_CAPABILITY__MAX] = { 0 };

    if (!migrate_params_check(&ms->parameters, errp)) {
        return false;
    }

    return migrate_caps_check(old_caps, ms->capabilities, errp);
}

static const TypeInfo migration_type = {
    .name = TYPE_MIGRATION,
    /*
     * NOTE: TYPE_MIGRATION is not really a device, as the object is
     * not created using qdev_new(), it is not attached to the qdev
     * device tree, and it is never realized.
     *
     * TODO: Make this TYPE_OBJECT once QOM provides something like
     * TYPE_DEVICE's "-global" properties.
     */
    .parent = TYPE_DEVICE,
    .class_init = migration_class_init,
    .class_size = sizeof(MigrationClass),
    .instance_size = sizeof(MigrationState),
    .instance_init = migration_instance_init,
    .instance_finalize = migration_instance_finalize,
};

static void register_migration_types(void)
{
    type_register_static(&migration_type);
}

type_init(register_migration_types);
