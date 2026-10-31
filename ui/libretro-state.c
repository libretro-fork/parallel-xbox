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
#include "qemu/cutils.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "exec/cpu-common.h"
#include "exec/target_page.h"
#include "migration/qemu-file.h"
#include "migration/savevm.h"
#include "migration/migration.h"
#include "migration/global_state.h"
#include "system/runstate.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "block/accounting.h"
#include "block/block.h"
#include "block/snapshot.h"
#include "libretro-state.h"

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

/* The frontend thread, under the BQL, the guest between frames */
bool lr_state_save(void *data, size_t size, Error **errp)
{
    LRStateHeader hdr;
    QEMUFile file;
    QEMUFile *f = &file;
    size_t stream_len;
    RunState saved_state = runstate_get();
    int ret, ret2;

    if (size < sizeof(hdr)) {
        error_setg(errp, "no room for the state's header");
        return false;
    }
    if (!lr_session) {
        lr_session = ((uint64_t)getpid() << 32) ^ get_clock_realtime();
    }

    global_state_store();
    /* the GPU halts and puts what it drew back in the guest's RAM */
    vm_stop(RUN_STATE_SAVE_VM);
    bdrv_drain_all_begin();

    qemu_file_init_output(f, (uint8_t *)data + sizeof(hdr),
                          size - sizeof(hdr));
    ret = qemu_savevm_state_libretro(f, errp);
    stream_len = qemu_file_transferred(f);
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
        hdr.stream_len = cpu_to_le64(stream_len);
        hdr.session = cpu_to_le64(lr_session);
        hdr.hdd_writes = cpu_to_le64(lr_state_hdd_writes());
        memcpy(data, &hdr, sizeof(hdr));
        /* the frontend's buffer is as big as the state can get: the rest
         * is zero, not whatever was in it. Only where it is not already:
         * a buffer the frontend just allocated is fresh pages, and writing
         * zeroes over them had the kernel clear each one first (half of
         * the buffer and most of the save's time: a state of 66 MB in a
         * buffer of 146 MB). Reading them maps the shared zero page. */
        uint8_t *tail = (uint8_t *)data + sizeof(hdr) + stream_len;
        size_t left = size - sizeof(hdr) - stream_len;
        while (left) {
            size_t n = MIN(left, (size_t)64 * 1024);
            if (!buffer_is_zero(tail, n)) {
                memset(tail, 0, n);
            }
            tail += n;
            left -= n;
        }
    }
    return ret == 0;
}

/* The frontend thread, under the BQL, the guest between frames. *disk_newer:
 * whether the game has written to the HDD since the state was saved, as far
 * as this process knows (states from an earlier one cannot tell). */
bool lr_state_load(const void *data, size_t size, bool *disk_newer,
                   Error **errp)
{
    MigrationIncomingState *mis = migration_incoming_get_current();
    LRStateHeader hdr;
    QEMUFile file;
    QEMUFile *f = &file;
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

    vm_stop(RUN_STATE_RESTORE_VM);
    bdrv_drain_all_begin();
    qemu_system_reset(SHUTDOWN_CAUSE_SNAPSHOT_LOAD);

    /* read where it lies: the page copied into the guest's RAM is the one
     * copy of a page, and what the loaders take in place is not copied */
    qemu_file_init_input(f, (const uint8_t *)data + sizeof(hdr), len);
    mis->from_src_file = f;
    ret = qemu_loadvm_state(f, errp);
    /* Finish f while the caller-owned cursor and memory remain live. */
    migration_incoming_state_destroy();

    bdrv_drain_all_end();
    if (was_running) {
        vm_start();
    }
    return ret == 0;
}
