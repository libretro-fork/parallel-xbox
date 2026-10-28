/*
 * The core's savestates: the machine's state as QEMU's migration stream
 * (the CPU, every device and all of the guest's RAM, the GPU's pending work
 * flushed into it first), written straight into the frontend's buffer and
 * read straight from it, so RetroArch's state files, slots and rewind hold
 * the whole state.
 *
 * The HDD's contents are not in it: a state would otherwise carry every
 * write the game made to the disk (its cache partitions alone are 2.25 GB).
 * A state loaded after the game has written to the disk since it was saved
 * meets a disk newer than the guest's view of it; the state's header keeps
 * the count of the HDD's writes so that case can be told (libretro.c warns).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "qom/object.h"
#include "io/channel.h"
#include "exec/cpu-common.h"
#include "exec/target_page.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/migration.h"
#include "migration/global_state.h"
#include "qemu/yank.h"
#include "system/runstate.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "block/accounting.h"
#include "block/block.h"
#include "block/snapshot.h"
#include "libretro-state.h"

/*
 * A channel over a buffer of fixed size the caller owns: writes past its end
 * fail instead of growing it (QIOChannelBuffer reallocates its own).
 */
#define TYPE_LR_STATE_CHANNEL "lr-state-channel"
OBJECT_DECLARE_SIMPLE_TYPE(LRStateChannel, LR_STATE_CHANNEL)

struct LRStateChannel {
    QIOChannel parent;
    uint8_t *wbuf;       /* saving: the frontend's buffer */
    const uint8_t *rbuf; /* loading: the state */
    size_t cap;
    size_t off;
};

static ssize_t lr_state_channel_readv(QIOChannel *ioc, const struct iovec *iov,
                                      size_t niov, int **fds, size_t *nfds,
                                      int flags, Error **errp)
{
    LRStateChannel *c = LR_STATE_CHANNEL(ioc);
    ssize_t ret = 0;

    for (size_t i = 0; i < niov && c->off < c->cap; i++) {
        size_t n = MIN(iov[i].iov_len, c->cap - c->off);
        memcpy(iov[i].iov_base, c->rbuf + c->off, n);
        c->off += n;
        ret += n;
    }
    return ret;
}

static ssize_t lr_state_channel_writev(QIOChannel *ioc,
                                       const struct iovec *iov, size_t niov,
                                       int *fds, size_t nfds, int flags,
                                       Error **errp)
{
    LRStateChannel *c = LR_STATE_CHANNEL(ioc);
    size_t total = iov_size(iov, niov);

    if (total > c->cap - c->off) {
        error_setg(errp, "the state does not fit in %zu bytes", c->cap);
        return -1;
    }
    for (size_t i = 0; i < niov; i++) {
        memcpy(c->wbuf + c->off, iov[i].iov_base, iov[i].iov_len);
        c->off += iov[i].iov_len;
    }
    return total;
}

static int lr_state_channel_set_blocking(QIOChannel *ioc, bool enabled,
                                         Error **errp)
{
    return 0;
}

static int lr_state_channel_close(QIOChannel *ioc, Error **errp)
{
    return 0;
}

static void lr_state_channel_class_init(ObjectClass *klass,
                                        const void *class_data)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = lr_state_channel_writev;
    ioc_klass->io_readv = lr_state_channel_readv;
    ioc_klass->io_set_blocking = lr_state_channel_set_blocking;
    ioc_klass->io_close = lr_state_channel_close;
}

static const TypeInfo lr_state_channel_info = {
    .parent = TYPE_QIO_CHANNEL,
    .name = TYPE_LR_STATE_CHANNEL,
    .instance_size = sizeof(LRStateChannel),
    .class_init = lr_state_channel_class_init,
};

static void lr_state_register_types(void)
{
    type_register_static(&lr_state_channel_info);
}

type_init(lr_state_register_types);

/*
 * The state: a header, then the migration stream. Little-endian.
 */
#define LR_STATE_MAGIC   "XBOXSTAT"
#define LR_STATE_VERSION 1

typedef struct LRStateHeader {
    char magic[8];
    uint32_t version;
    uint32_t reserved;
    uint64_t stream_len;
    uint64_t session;    /* the process the state was saved in */
    uint64_t hdd_writes; /* the HDD's writes by then, in that process */
} LRStateHeader;

/* Room for everything but RAM: the devices' sections, 5.5 MB measured
 * (the GPU 4.3 MB, the APU 1.1 MB), nearly all of it arrays of fixed size */
#define LR_STATE_DEVICES_MAX (8 * 1024 * 1024)

static uint64_t lr_session;

static int lr_state_ram_bound(RAMBlock *rb, void *opaque)
{
    size_t *sum = opaque;
    uint64_t pages = DIV_ROUND_UP(qemu_ram_get_used_length(rb),
                                  qemu_target_page_size());

    /* each page as its header and its data, at worst; and the block's id
     * where the stream turns to it */
    *sum += pages * (qemu_target_page_size() + 16) + 512;
    return 0;
}

size_t lr_state_size(void)
{
    size_t sum = sizeof(LRStateHeader) + LR_STATE_DEVICES_MAX;

    foreach_not_ignored_block(lr_state_ram_bound, &sum);
    return sum;
}

static BlockBackend *lr_state_hdd(void)
{
    DriveInfo *dinfo = drive_get(IF_IDE, 0, 0);
    return dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;
}

static uint64_t lr_state_hdd_writes(void)
{
    BlockBackend *blk = lr_state_hdd();
    return blk ? blk_get_stats(blk)->nr_ops[BLOCK_ACCT_WRITE] : 0;
}

/* The emulator thread, under the BQL, the guest between frames */
bool lr_state_save(void *data, size_t size, Error **errp)
{
    LRStateHeader hdr;
    LRStateChannel *c;
    QEMUFile *f;
    RunState saved_state = runstate_get();
    int ret, ret2;

    if (size < sizeof(hdr)) {
        error_setg(errp, "no room for the state's header");
        return false;
    }
    if (!lr_session) {
        lr_session = ((uint64_t)getpid() << 32) ^ get_clock_realtime();
    }

    c = LR_STATE_CHANNEL(object_new(TYPE_LR_STATE_CHANNEL));
    c->wbuf = (uint8_t *)data + sizeof(hdr);
    c->cap = size - sizeof(hdr);

    global_state_store();
    /* the GPU halts and puts what it drew back in the guest's RAM */
    vm_stop(RUN_STATE_SAVE_VM);
    bdrv_drain_all_begin();

    f = qemu_file_new_output(QIO_CHANNEL(c));
    ret = qemu_savevm_state_libretro(f, errp);
    ret2 = qemu_fclose(f);
    if (ret == 0 && ret2 < 0) {
        error_setg_errno(errp, -ret2, "writing the state failed");
        ret = ret2;
    }

    bdrv_drain_all_end();
    vm_resume(saved_state);

    if (ret == 0) {
        memcpy(hdr.magic, LR_STATE_MAGIC, sizeof(hdr.magic));
        hdr.version = cpu_to_le32(LR_STATE_VERSION);
        hdr.reserved = 0;
        hdr.stream_len = cpu_to_le64(c->off);
        hdr.session = cpu_to_le64(lr_session);
        hdr.hdd_writes = cpu_to_le64(lr_state_hdd_writes());
        memcpy(data, &hdr, sizeof(hdr));
        /* the frontend's buffer is as big as the state can get: the rest
         * is zero, not whatever was in it */
        memset((uint8_t *)data + sizeof(hdr) + c->off, 0,
               size - sizeof(hdr) - c->off);
    }
    object_unref(OBJECT(c));
    return ret == 0;
}

/* The emulator thread, under the BQL, the guest between frames. *disk_newer:
 * whether the game has written to the HDD since the state was saved, as far
 * as this process knows (states from an earlier one cannot tell). */
bool lr_state_load(const void *data, size_t size, bool *disk_newer,
                   Error **errp)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    LRStateHeader hdr;
    LRStateChannel *c;
    QEMUFile *f;
    bool was_running = runstate_is_running();
    uint64_t len;
    int ret;

    *disk_newer = false;
    if (size < sizeof(hdr)) {
        error_setg(errp, "not a state of this core");
        return false;
    }
    memcpy(&hdr, data, sizeof(hdr));
    len = le64_to_cpu(hdr.stream_len);
    if (memcmp(hdr.magic, LR_STATE_MAGIC, sizeof(hdr.magic)) != 0) {
        error_setg(errp, "not a state of this core's (the states of earlier "
                   "versions were kept in the HDD image, and are gone)");
        return false;
    }
    if (le32_to_cpu(hdr.version) != LR_STATE_VERSION ||
        len > size - sizeof(hdr)) {
        error_setg(errp, "the state is of another version or cut short");
        return false;
    }
    *disk_newer = lr_session &&
                  le64_to_cpu(hdr.session) == lr_session &&
                  le64_to_cpu(hdr.hdd_writes) != lr_state_hdd_writes();

    c = LR_STATE_CHANNEL(object_new(TYPE_LR_STATE_CHANNEL));
    c->rbuf = (const uint8_t *)data + sizeof(hdr);
    c->cap = len;

    vm_stop(RUN_STATE_RESTORE_VM);
    bdrv_drain_all_begin();
    qemu_system_reset(SHUTDOWN_CAUSE_SNAPSHOT_LOAD);

    f = qemu_file_new_input(QIO_CHANNEL(c));
    mis->from_src_file = f;
    if (!yank_register_instance(MIGRATION_YANK_INSTANCE, errp)) {
        qemu_fclose(f);
        mis->from_src_file = NULL;
        ret = -EINVAL;
    } else {
        ret = qemu_loadvm_state(f, errp);
        /* closes f */
        migration_incoming_state_destroy();
    }

    bdrv_drain_all_end();
    object_unref(OBJECT(c));
    if (was_running) {
        vm_start();
    }
    return ret == 0;
}

/* States from before this file were a snapshot named libretro_save in the
 * HDD image, as big as the guest's RAM, which also kept Trim HDD on Close
 * from running: it goes when content starts. */
void lr_state_drop_legacy(void)
{
    BlockBackend *blk = lr_state_hdd();
    BlockDriverState *bs = blk ? blk_bs(blk) : NULL;
    QEMUSnapshotInfo sn;
    Error *err = NULL;

    if (!bs) {
        return;
    }
    bdrv_drain_all_begin();
    bdrv_graph_rdlock_main_loop();
    if (bdrv_snapshot_find(bs, &sn, "libretro_save") >= 0) {
        bdrv_snapshot_delete(bs, sn.id_str, sn.name, &err);
    }
    bdrv_graph_rdunlock_main_loop();
    bdrv_drain_all_end();
    error_free(err);
}
