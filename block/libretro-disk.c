/*
 * The libretro core's disks, in place of QEMU's block layer.
 *
 * Three drives at most: the Xbox's hard disk, its DVD drive (a disc image,
 * read only, from a Redump image's game partition when that is what it
 * is), and the Chihiro media board's interface (an address space). The
 * hard disk is a qcow2 image (xbox_hdd.qcow2, as standalone xemu and
 * earlier builds have it, shared with them as it is: block/libretro-qcow2.c)
 * or a raw one (xbox_hdd.img, sparse where the host's file system allows:
 * what the core makes for a new HDD, ui/libretro.c), told apart by the
 * file's first bytes. What the devices ask of the drives (hw/ide,
 * hw/block, system/dma-helpers.c) is the part of QEMU's BlockBackend API
 * implemented here.
 *
 * A request is done when it is made, on the emulator thread: a read or a
 * write is one positional system call into the guest's own memory (the
 * request's I/O vector), and its completion runs as a bottom half at the
 * next pass of the frame loop, as QEMU's callers expect (never inside the
 * call that made the request). No coroutine, no thread pool, no thread
 * switch, no allocation. What a request takes on the guest's time is
 * therefore fixed: none, as when the guest idled on a read before, and a
 * state taken between frames never has one in flight. The frame loop
 * waits for a completion as for the GPU (lr_disk_pending,
 * ui/libretro.c), so a guest idling on its disk does not skip ahead to its
 * next timer first.
 *
 * A guest's flush (ATA FLUSH CACHE) completes at once on a raw image: a
 * write is in the host's page cache when it returns, which a crash of the
 * frontend does not lose, so the flush would only buy safety against the
 * host itself going down, at a sync each time (each savestate and
 * run-ahead frame, through vm_stop, did one while the guest wrote). On a
 * qcow2 it syncs only when the image's tables changed since the last one.
 * The images are synced when they are closed, and when the content closes.
 * A write error is reported to the guest, as a disk's own would be,
 * and logged once: QEMU's default stops the machine on a full host disk,
 * which in a core is a game frozen for good.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/memalign.h"
#include "qemu/queue.h"
#include "qemu/job.h"
#include "block/aio.h"
#include "block/accounting.h"
#include "block/block.h"
#include "block/block_int-io.h"
#include "block/snapshot.h"
#include "block/libretro-disk.h"
#include "block/libretro-file.h"
#include "block/libretro-vfs-read.h"
#include "block/libretro-qcow2.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "system/memory.h"
#include "migration/misc.h"
#include "migration/migration.h"
#include "migration/channel-block.h"

#include <libretro.h>

/* ui/libretro.c: the frontend's VFS, if it has one */
extern struct retro_vfs_interface *xemu_libretro_vfs;

/* requests whose completion has not run, per drive: the IDE has one in
 * flight at a time (a DMA's chunks follow one another) */
#define LR_DISK_REQS 8

typedef enum {
    LR_MEDIUM_NONE,
    LR_MEDIUM_FILE,     /* a raw image */
    LR_MEDIUM_QCOW2,    /* a qcow2 image, read and written in place */
    LR_MEDIUM_VFS,
    LR_MEDIUM_MEMORY,
} LRMediumKind;

typedef struct LRMedium {
    LRMediumKind kind;
    LRFile *file;
    LRQcow2 *qcow2;
    struct retro_vfs_file_handle *vfs;
    AddressSpace *as;
    uint64_t base;      /* where the drive's data starts in the image */
    int64_t length;     /* from there */
    bool writable;
    int64_t vfs_position;
} LRMedium;

typedef struct LRDiskReq {
    BlockAIOCB common;
    BlockBackend *blk;
    QEMUBH *bh;
    int ret;
    bool busy;          /* reserve the slot until its callback returns */
    bool completing;    /* no longer queued for cancellation/draining */
} LRDiskReq;

struct BlockBackend {
    char *name;
    DriveInfo *dinfo;
    DeviceState *dev;
    const BlockDevOps *dev_ops;
    void *dev_opaque;
    LRMedium m;
    bool wce;
    bool error_logged;
    BlockdevOnError on_read_error, on_write_error;
    int in_flight;
    int refcnt;
    BlockAcctStats stats;
    LRDiskReq req[LR_DISK_REQS];
    QTAILQ_ENTRY(BlockBackend) link;
};

static const AIOCBInfo lr_disk_aiocb_info = {
    .aiocb_size = sizeof(LRDiskReq),
};

static QTAILQ_HEAD(, BlockBackend) lr_disks =
    QTAILQ_HEAD_INITIALIZER(lr_disks);

int lr_disk_pending;

const uint8_t *lr_disk_view(BlockBackend *blk, int64_t offset, size_t size)
{
    if (!blk || blk->m.kind != LR_MEDIUM_FILE || blk->m.writable ||
        offset < 0 || offset > blk->m.length ||
        size > (uint64_t)(blk->m.length - offset) ||
        blk->m.base > INT64_MAX ||
        (uint64_t)offset > (uint64_t)INT64_MAX - blk->m.base) {
        return NULL;
    }
    return lr_file_view(blk->m.file, blk->m.base + offset, size);
}

QEMU_BUILD_BUG_ON(sizeof(struct iovec) != sizeof(LRFileIOV));

/* ---- the media ---------------------------------------------------------- */

static bool lr_disk_is_uri(const char *path)
{
    return g_str_has_prefix(path, "saf://") ||
           g_str_has_prefix(path, "content://");
}

static void lr_medium_close(LRMedium *m)
{
    if (m->qcow2) {
        lr_qcow2_flush(m->qcow2);
        lr_qcow2_close(m->qcow2);
    }
    if (m->file) {
        lr_file_close(m->file);
    }
    if (m->vfs) {
        xemu_libretro_vfs->close(m->vfs);
    }
    memset(m, 0, sizeof(*m));
}

static bool lr_medium_open(LRMedium *m, const char *path, uint64_t offset,
                           bool writable, Error **errp)
{
    int64_t size;

    memset(m, 0, sizeof(*m));
    m->vfs_position = -1;
    if (lr_disk_is_uri(path)) {
        if (!xemu_libretro_vfs) {
            error_setg(errp, "%s can only be read through the frontend's "
                       "VFS, which it does not offer", path);
            return false;
        }
        if (writable) {
            error_setg(errp, "%s is read only here", path);
            return false;
        }
        m->vfs = xemu_libretro_vfs->open(path, RETRO_VFS_FILE_ACCESS_READ,
                                         RETRO_VFS_FILE_ACCESS_HINT_NONE);
        if (!m->vfs) {
            error_setg(errp, "the frontend cannot open %s", path);
            return false;
        }
        size = xemu_libretro_vfs->size(m->vfs);
        m->kind = LR_MEDIUM_VFS;
    } else {
        m->file = lr_file_open(path, writable, false);
        if (!m->file) {
            error_setg_errno(errp, errno, "cannot open %s", path);
            return false;
        }
        size = lr_file_size(m->file);
        m->kind = LR_MEDIUM_FILE;
        uint8_t head[4];
        if (size >= 4 && lr_file_pread(m->file, 0, head, 4) == 0 &&
            head[0] == 'Q' && head[1] == 'F' && head[2] == 'I' &&
            head[3] == 0xfb) {
            char why[256];
            m->qcow2 = lr_qcow2_open(m->file, writable, why, sizeof(why));
            if (!m->qcow2) {
                error_setg(errp, "%s: %s", path, why);
                lr_medium_close(m);
                return false;
            }
            if (offset) {
                error_setg(errp, "%s is a qcow2 image: no offset into it",
                           path);
                lr_medium_close(m);
                return false;
            }
            size = lr_qcow2_size(m->qcow2);
            m->kind = LR_MEDIUM_QCOW2;
        }
    }
    if (size < 0 || (uint64_t)size < offset) {
        error_setg(errp, "%s is shorter than its offset 0x%" PRIx64, path,
                   offset);
        lr_medium_close(m);
        return false;
    }
    m->base = offset;
    m->length = size - (int64_t)offset;
    m->writable = writable;
    return true;
}

/* The request whole, or -errno. The guest's own memory is the I/O vector
 * (dma-helpers maps it), so a DMA goes straight between it and the file. */
static int lr_disk_rw(BlockBackend *blk, int64_t offset, QEMUIOVector *qiov,
                      bool write)
{
    LRMedium *m = &blk->m;
    int64_t bytes = qiov->size;
    int ret = 0;

    if (m->kind == LR_MEDIUM_NONE) {
        return -ENOMEDIUM;
    }
    if (offset < 0 || offset > m->length || bytes > m->length - offset) {
        return -EIO;
    }
    if (write && !m->writable) {
        return -EPERM;
    }
    switch (m->kind) {
    case LR_MEDIUM_FILE:
        ret = write ? lr_file_pwritev(m->file, m->base + offset,
                                      (const LRFileIOV *)qiov->iov,
                                      qiov->niov)
                    : lr_file_preadv(m->file, m->base + offset,
                                     (const LRFileIOV *)qiov->iov,
                                     qiov->niov);
        break;
    case LR_MEDIUM_QCOW2:
        ret = write ? lr_qcow2_pwritev(m->qcow2, offset,
                                       (const LRFileIOV *)qiov->iov,
                                       qiov->niov)
                    : lr_qcow2_preadv(m->qcow2, offset,
                                      (const LRFileIOV *)qiov->iov,
                                      qiov->niov);
        break;
    case LR_MEDIUM_VFS:
        ret = lr_vfs_readv(xemu_libretro_vfs, m->vfs, &m->vfs_position,
                           m->base + offset,
                           (const LRFileIOV *)qiov->iov, qiov->niov);
        break;
    case LR_MEDIUM_MEMORY:
        for (int i = 0; i < qiov->niov; i++) {
            if (write) {
                address_space_write(m->as, offset, MEMTXATTRS_UNSPECIFIED,
                                    qiov->iov[i].iov_base,
                                    qiov->iov[i].iov_len);
            } else {
                address_space_read(m->as, offset, MEMTXATTRS_UNSPECIFIED,
                                   qiov->iov[i].iov_base,
                                   qiov->iov[i].iov_len);
            }
            offset += qiov->iov[i].iov_len;
        }
        break;
    default:
        abort();
    }
    return ret;
}

int lr_disk_zero(BlockBackend *blk, int64_t offset, int64_t bytes)
{
    LRMedium *m = &blk->m;

    if (m->kind == LR_MEDIUM_NONE) {
        return -ENOMEDIUM;
    }
    if (offset < 0 || bytes < 0 || offset > m->length ||
        bytes > m->length - offset) {
        return -EIO;
    }
    if (!m->writable) {
        return -EPERM;
    }
    if (m->kind == LR_MEDIUM_FILE) {
        return lr_file_zero(m->file, m->base + offset, bytes);
    }
    if (m->kind == LR_MEDIUM_QCOW2) {
        return lr_qcow2_zero(m->qcow2, offset, bytes);
    }
    while (bytes > 0) {
        static const uint8_t zero[4096];
        int64_t n = MIN(bytes, (int64_t)sizeof(zero));
        address_space_write(m->as, offset, MEMTXATTRS_UNSPECIFIED, zero, n);
        offset += n;
        bytes -= n;
    }
    return 0;
}

int64_t lr_disk_allocated(BlockBackend *blk)
{
    return blk->m.file ? lr_file_allocated(blk->m.file) : -1;
}

/* ---- the drives --------------------------------------------------------- */

static void lr_disk_req_done(void *opaque);

static BlockBackend *lr_disk_alloc(void)
{
    BlockBackend *blk = g_new0(BlockBackend, 1);

    blk->refcnt = 1;
    blk->wce = true;
    blk->on_read_error = BLOCKDEV_ON_ERROR_REPORT;
    blk->on_write_error = BLOCKDEV_ON_ERROR_ENOSPC;
    block_acct_init(&blk->stats);
    for (int i = 0; i < LR_DISK_REQS; i++) {
        blk->req[i].blk = blk;
        blk->req[i].common.aiocb_info = &lr_disk_aiocb_info;
        blk->req[i].bh = qemu_bh_new(lr_disk_req_done, &blk->req[i]);
    }
    QTAILQ_INSERT_TAIL(&lr_disks, blk, link);
    return blk;
}

BlockBackend *lr_disk_new(const char *name, BlockInterfaceType type,
                          int bus, int unit, bool cdrom, bool locked)
{
    BlockBackend *blk = lr_disk_alloc();
    DriveInfo *dinfo = g_new0(DriveInfo, 1);

    blk->name = g_strdup(name);
    dinfo->type = type;
    dinfo->bus = bus;
    dinfo->unit = unit;
    dinfo->media_cd = cdrom;
    dinfo->locked = locked;
    blk->dinfo = dinfo;
    return blk;
}

bool lr_disk_open(BlockBackend *blk, const char *path, uint64_t offset,
                  bool writable, Error **errp)
{
    LRMedium m;

    if (!lr_medium_open(&m, path, offset, writable, errp)) {
        return false;
    }
    lr_medium_close(&blk->m);
    blk->m = m;
    blk->error_logged = false;
    return true;
}

void lr_disk_open_memory(BlockBackend *blk, AddressSpace *as, uint64_t size)
{
    lr_medium_close(&blk->m);
    blk->m.kind = LR_MEDIUM_MEMORY;
    blk->m.as = as;
    blk->m.length = (int64_t)size;
    blk->m.writable = true;
}

static bool lr_disk_has_tray(BlockBackend *blk)
{
    return blk->dev_ops && blk->dev_ops->is_tray_open;
}

bool lr_disk_change(BlockBackend *blk, const char *path, uint64_t offset,
                    Error **errp)
{
    LRMedium m = { 0 };

    /* the new disc first: a path that does not open leaves the drive as
     * it was */
    if (path && path[0] && !lr_medium_open(&m, path, offset, false, errp)) {
        return false;
    }
    /* the tray opens, by force when the guest locked it (it is asked to
     * let go first) */
    if (lr_disk_has_tray(blk) && !blk_dev_is_tray_open(blk)) {
        if (blk->dev_ops->is_medium_locked &&
            blk->dev_ops->is_medium_locked(blk->dev_opaque) &&
            blk->dev_ops->eject_request_cb) {
            blk->dev_ops->eject_request_cb(blk->dev_opaque, true);
        }
        blk->dev_ops->change_media_cb(blk->dev_opaque, false, &error_abort);
    }
    lr_medium_close(&blk->m);
    if (m.kind == LR_MEDIUM_NONE) {
        return true;
    }
    blk->m = m;
    blk->error_logged = false;
    if (lr_disk_has_tray(blk) && blk_dev_is_tray_open(blk)) {
        Error *err = NULL;
        blk->dev_ops->change_media_cb(blk->dev_opaque, true, &err);
        if (err) {
            error_propagate(errp, err);
            return false;
        }
    }
    return true;
}

/* ---- requests ----------------------------------------------------------- */

static void lr_disk_req_done(void *opaque)
{
    LRDiskReq *r = opaque;
    BlockBackend *blk = r->blk;

    assert(r->busy && !r->completing);
    r->completing = true;
    lr_disk_pending--;
    r->common.cb(r->common.opaque, r->ret);
    r->completing = false;
    r->busy = false;
    blk->in_flight--;
}

static BlockAIOCB *lr_disk_submit(BlockBackend *blk, int ret,
                                  BlockCompletionFunc *cb, void *opaque)
{
    for (int i = 0; i < LR_DISK_REQS; i++) {
        LRDiskReq *r = &blk->req[i];
        if (!r->busy) {
            r->busy = true;
            r->ret = ret;
            r->common.cb = cb;
            r->common.opaque = opaque;
            r->common.refcnt = 1;
            blk->in_flight++;
            lr_disk_pending++;
            qemu_bh_schedule(r->bh);
            return &r->common;
        }
    }
    error_report("%s: more than %d requests in flight", blk_name(blk),
                 LR_DISK_REQS);
    abort();
}

/* The completions of every request done, now: what a cancel or a drain
 * waits for. Whether there were any. */
static bool lr_disk_run_pending(void)
{
    bool ran = false;
    BlockBackend *blk;

    QTAILQ_FOREACH(blk, &lr_disks, link) {
        for (int i = 0; i < LR_DISK_REQS; i++) {
            if (blk->req[i].busy && !blk->req[i].completing) {
                qemu_bh_cancel(blk->req[i].bh);
                lr_disk_req_done(&blk->req[i]);
                ran = true;
            }
        }
    }
    return ran;
}

/* what a drain or a cancel waits on that is not one of these requests (a
 * DMA's bounce buffer, the IDE's TRIM) is a bottom half of its own */
static bool lr_disk_progress(void)
{
    return lr_disk_run_pending() || aio_bh_poll(qemu_get_aio_context()) > 0;
}

BlockAIOCB *blk_aio_preadv(BlockBackend *blk, int64_t offset,
                           QEMUIOVector *qiov, BdrvRequestFlags flags,
                           BlockCompletionFunc *cb, void *opaque)
{
    return lr_disk_submit(blk, lr_disk_rw(blk, offset, qiov, false), cb,
                          opaque);
}

BlockAIOCB *blk_aio_pwritev(BlockBackend *blk, int64_t offset,
                            QEMUIOVector *qiov, BdrvRequestFlags flags,
                            BlockCompletionFunc *cb, void *opaque)
{
    return lr_disk_submit(blk, lr_disk_rw(blk, offset, qiov, true), cb,
                          opaque);
}

BlockAIOCB *blk_aio_flush(BlockBackend *blk, BlockCompletionFunc *cb,
                          void *opaque)
{
    int ret = blk->m.kind == LR_MEDIUM_NONE ? -ENOMEDIUM : 0;

    /* a qcow2's tables to the disk when they changed (a cluster made, one
     * given back): what the guest wrote stays findable on the host going
     * down; the data itself is the page cache's, as on a raw image */
    if (blk->m.kind == LR_MEDIUM_QCOW2) {
        ret = lr_qcow2_flush(blk->m.qcow2);
    }
    return lr_disk_submit(blk, ret, cb, opaque);
}

BlockAIOCB *blk_aio_pdiscard(BlockBackend *blk, int64_t offset, int64_t bytes,
                             BlockCompletionFunc *cb, void *opaque)
{
    return lr_disk_submit(blk, lr_disk_zero(blk, offset, bytes), cb, opaque);
}

BlockAIOCB *blk_abort_aio_request(BlockBackend *blk, BlockCompletionFunc *cb,
                                  void *opaque, int ret)
{
    return lr_disk_submit(blk, ret, cb, opaque);
}

void *blk_aio_get(const AIOCBInfo *aiocb_info, BlockBackend *blk,
                  BlockCompletionFunc *cb, void *opaque)
{
    return qemu_aio_get(aiocb_info, NULL, cb, opaque);
}

void blk_aio_cancel_async(BlockAIOCB *acb)
{
    /* one of these is done already; its completion comes as it would */
    if (acb->aiocb_info->cancel_async) {
        acb->aiocb_info->cancel_async(acb);
    }
}

void blk_aio_cancel(BlockAIOCB *acb)
{
    if (acb->aiocb_info == &lr_disk_aiocb_info) {
        LRDiskReq *r = container_of(acb, LRDiskReq, common);
        if (r->busy && !r->completing) {
            qemu_bh_cancel(r->bh);
            lr_disk_req_done(r);
        }
        return;
    }
    /* a DMA (dma-helpers' own AIOCB) over these: its requests' completions
     * until it is done */
    qemu_aio_ref(acb);
    blk_aio_cancel_async(acb);
    while (acb->refcnt > 1 && lr_disk_progress()) {
    }
    qemu_aio_unref(acb);
}

void blk_inc_in_flight(BlockBackend *blk)
{
    blk->in_flight++;
}

void blk_dec_in_flight(BlockBackend *blk)
{
    blk->in_flight--;
}

void blk_drain(BlockBackend *blk)
{
    while (blk->in_flight > 0 && lr_disk_progress()) {
    }
}

static int lr_disk_in_flight(void)
{
    BlockBackend *blk;
    int n = 0;

    QTAILQ_FOREACH(blk, &lr_disks, link) {
        n += blk->in_flight;
    }
    return n;
}

void bdrv_drain_all(void)
{
    while (lr_disk_in_flight() > 0 && lr_disk_progress()) {
    }
}

void bdrv_drain_all_begin(void)
{
    bdrv_drain_all();
}

void bdrv_drain_all_end(void)
{
}

int bdrv_flush_all(void)
{
    return 0;
}

void bdrv_close_all(void)
{
    BlockBackend *blk;

    bdrv_drain_all();
    QTAILQ_FOREACH(blk, &lr_disks, link) {
        lr_medium_close(&blk->m);
    }
}

void lr_disk_sync_all(void)
{
    BlockBackend *blk;

    QTAILQ_FOREACH(blk, &lr_disks, link) {
        if (blk->m.qcow2 && blk->m.writable) {
            lr_qcow2_flush(blk->m.qcow2);
        }
        if (blk->m.file && blk->m.writable) {
            lr_file_sync(blk->m.file);
        }
    }
}

void bdrv_init_with_whitelist(void)
{
}

/* the whole request now, as the devices' set-up and the core ask */
int blk_pread(BlockBackend *blk, int64_t offset, int64_t bytes, void *buf,
              BdrvRequestFlags flags)
{
    QEMUIOVector qiov;

    qemu_iovec_init_buf(&qiov, buf, bytes);
    return lr_disk_rw(blk, offset, &qiov, false);
}

int blk_pwrite(BlockBackend *blk, int64_t offset, int64_t bytes,
               const void *buf, BdrvRequestFlags flags)
{
    QEMUIOVector qiov;

    qemu_iovec_init_buf(&qiov, (void *)buf, bytes);
    return lr_disk_rw(blk, offset, &qiov, true);
}

int blk_pwrite_zeroes(BlockBackend *blk, int64_t offset, int64_t bytes,
                      BdrvRequestFlags flags)
{
    return lr_disk_zero(blk, offset, bytes);
}

int blk_flush(BlockBackend *blk)
{
    return 0;
}

void *blk_blockalign(BlockBackend *blk, size_t size)
{
    return qemu_memalign(4096, size);
}

/* ---- what the devices ask of a drive ------------------------------------ */

const char *blk_name(const BlockBackend *blk)
{
    return blk->name ? blk->name : "";
}

BlockBackend *blk_by_name(const char *name)
{
    BlockBackend *blk;

    QTAILQ_FOREACH(blk, &lr_disks, link) {
        if (blk->name && !strcmp(blk->name, name)) {
            return blk;
        }
    }
    return NULL;
}

BlockBackend *blk_by_dev(void *dev)
{
    BlockBackend *blk;

    QTAILQ_FOREACH(blk, &lr_disks, link) {
        if (blk->dev == dev) {
            return blk;
        }
    }
    return NULL;
}

/* an empty drive the IDE makes itself (a CD drive with no -drive) */
BlockBackend *blk_new(AioContext *ctx, uint64_t perm, uint64_t shared_perm)
{
    return lr_disk_alloc();
}

void blk_unref(BlockBackend *blk)
{
    if (blk && --blk->refcnt == 0) {
        bdrv_drain_all();
        lr_medium_close(&blk->m);
        for (int i = 0; i < LR_DISK_REQS; i++) {
            qemu_bh_delete(blk->req[i].bh);
        }
        QTAILQ_REMOVE(&lr_disks, blk, link);
        block_acct_cleanup(&blk->stats);
        g_free(blk->dinfo);
        g_free(blk->name);
        g_free(blk);
    }
}

int blk_attach_dev(BlockBackend *blk, DeviceState *dev)
{
    if (blk->dev) {
        return -EBUSY;
    }
    blk->refcnt++;
    blk->dev = dev;
    return 0;
}

void blk_detach_dev(BlockBackend *blk, DeviceState *dev)
{
    assert(blk->dev == dev);
    blk->dev = NULL;
    blk->dev_ops = NULL;
    blk->dev_opaque = NULL;
    blk_unref(blk);
}

void blk_set_dev_ops(BlockBackend *blk, const BlockDevOps *ops, void *opaque)
{
    blk->dev_ops = ops;
    blk->dev_opaque = opaque;
}

bool blk_dev_is_tray_open(BlockBackend *blk)
{
    return lr_disk_has_tray(blk) && blk->dev_ops->is_tray_open(blk->dev_opaque);
}

DriveInfo *blk_legacy_dinfo(BlockBackend *blk)
{
    return blk->dinfo;
}

DriveInfo *blk_set_legacy_dinfo(BlockBackend *blk, DriveInfo *dinfo)
{
    assert(!blk->dinfo);
    return blk->dinfo = dinfo;
}

BlockBackend *blk_by_legacy_dinfo(DriveInfo *dinfo)
{
    BlockBackend *blk;

    QTAILQ_FOREACH(blk, &lr_disks, link) {
        if (blk->dinfo == dinfo) {
            return blk;
        }
    }
    abort();
}

DriveInfo *drive_get(BlockInterfaceType type, int bus, int unit)
{
    BlockBackend *blk;

    QTAILQ_FOREACH(blk, &lr_disks, link) {
        DriveInfo *d = blk->dinfo;
        if (d && d->type == type && d->bus == bus && d->unit == unit) {
            return d;
        }
    }
    return NULL;
}

DriveInfo *drive_get_by_index(BlockInterfaceType type, int index)
{
    /* two drives to an IDE bus, as QEMU counts them */
    return type == IF_IDE ? drive_get(type, index / 2, index % 2)
                          : drive_get(type, 0, index);
}

bool blk_is_inserted(BlockBackend *blk)
{
    return blk->m.kind != LR_MEDIUM_NONE;
}

bool blk_is_writable(BlockBackend *blk)
{
    return blk->m.writable;
}

bool blk_supports_write_perm(BlockBackend *blk)
{
    return blk->m.kind == LR_MEDIUM_NONE || blk->m.writable;
}

int blk_set_perm(BlockBackend *blk, uint64_t perm, uint64_t shared_perm,
                 Error **errp)
{
    if ((perm & BLK_PERM_WRITE) && blk->m.kind != LR_MEDIUM_NONE &&
        !blk->m.writable) {
        error_setg(errp, "%s is read only", blk_name(blk));
        return -EPERM;
    }
    return 0;
}

int64_t blk_getlength(BlockBackend *blk)
{
    return blk->m.kind == LR_MEDIUM_NONE ? -ENOMEDIUM : blk->m.length;
}

void blk_get_geometry(BlockBackend *blk, uint64_t *nb_sectors_ptr)
{
    *nb_sectors_ptr = blk->m.kind == LR_MEDIUM_NONE ? 0 :
                      (uint64_t)blk->m.length >> BDRV_SECTOR_BITS;
}

int blk_probe_blocksizes(BlockBackend *blk, BlockSizes *bsz)
{
    return -ENOTSUP;
}

int blk_probe_geometry(BlockBackend *blk, HDGeometry *geo)
{
    return -ENOTSUP;
}

bool blk_enable_write_cache(BlockBackend *blk)
{
    return blk->wce;
}

void blk_set_enable_write_cache(BlockBackend *blk, bool wce)
{
    blk->wce = wce;
}

void blk_set_on_error(BlockBackend *blk, BlockdevOnError on_read_error,
                      BlockdevOnError on_write_error)
{
    blk->on_read_error = on_read_error;
    blk->on_write_error = on_write_error;
}

BlockdevOnError blk_get_on_error(BlockBackend *blk, bool is_read)
{
    return is_read ? blk->on_read_error : blk->on_write_error;
}

BlockErrorAction blk_get_error_action(BlockBackend *blk, bool is_read,
                                      int error)
{
    return blk_get_on_error(blk, is_read) == BLOCKDEV_ON_ERROR_IGNORE ?
           BLOCK_ERROR_ACTION_IGNORE : BLOCK_ERROR_ACTION_REPORT;
}

void blk_error_action(BlockBackend *blk, BlockErrorAction action,
                      bool is_read, int error)
{
    if (!blk->error_logged) {
        blk->error_logged = true;
        error_report("%s: %s failed: %s", blk_name(blk),
                     is_read ? "a read" : "a write", strerror(error));
    }
}

void blk_iostatus_enable(BlockBackend *blk)
{
}

BlockAcctStats *blk_get_stats(BlockBackend *blk)
{
    return &blk->stats;
}

AioContext *blk_get_aio_context(BlockBackend *blk)
{
    return qemu_get_aio_context();
}

/* the host's drive's lock and eject: an image has none */
void blk_lock_medium(BlockBackend *blk, bool locked)
{
}

void blk_eject(BlockBackend *blk, bool eject_flag)
{
}

/* No block nodes: a drive is its medium. The drive property's paths for
 * nodes (hw/core/qdev-properties-system.c) find none. */
BlockDriverState *blk_bs(BlockBackend *blk)
{
    return NULL;
}

BlockDriverState *bdrv_lookup_bs(const char *device, const char *node_name,
                                 Error **errp)
{
    error_setg(errp, "no block node '%s'", node_name ? node_name : device);
    return NULL;
}

const char *bdrv_get_node_name(const BlockDriverState *bs)
{
    return "";
}

AioContext *bdrv_get_aio_context(BlockDriverState *bs)
{
    return qemu_get_aio_context();
}

int blk_insert_bs(BlockBackend *blk, BlockDriverState *bs, Error **errp)
{
    error_setg(errp, "no block nodes in the core");
    return -ENOTSUP;
}

int blk_replace_bs(BlockBackend *blk, BlockDriverState *new_bs, Error **errp)
{
    error_setg(errp, "no block nodes in the core");
    return -ENOTSUP;
}

void blockdev_auto_del(BlockBackend *blk)
{
}

/* every byte data: what hw/block/block.c's read of a whole backend asks */
int bdrv_block_status(BlockDriverState *bs, int64_t offset, int64_t bytes,
                      int64_t *pnum, int64_t *map, BlockDriverState **file)
{
    *pnum = bytes;
    return BDRV_BLOCK_DATA;
}

/* The core makes its drives itself (system/vl.c); -drive is refused */
void override_max_devs(BlockInterfaceType type, int max_devs)
{
}

void drive_check_orphaned(void)
{
}

QemuOpts *drive_add(BlockInterfaceType type, int index, const char *file,
                    const char *optstr)
{
    error_report("-drive and its kin are not taken by the libretro core");
    return NULL;
}

DriveInfo *drive_new(QemuOpts *all_opts, BlockInterfaceType block_default_type,
                     Error **errp)
{
    error_setg(errp, "-drive is not taken by the libretro core");
    return NULL;
}

/* ---- what the rest of QEMU asks of a block layer ------------------------ */

/* Snapshots in the disks (savevm, loadvm): the core's states are its own
 * (ui/libretro-state.c), and its images keep none */
bool bdrv_all_can_snapshot(bool has_devices, strList *devices, Error **errp)
{
    error_setg(errp, "the libretro core keeps no snapshots in its disks");
    return false;
}

int bdrv_all_delete_snapshot(const char *name, bool has_devices,
                             strList *devices, Error **errp)
{
    return 0;
}

int bdrv_all_goto_snapshot(const char *name, bool has_devices,
                           strList *devices, Error **errp)
{
    error_setg(errp, "the libretro core keeps no snapshots in its disks");
    return -ENOTSUP;
}

int bdrv_all_has_snapshot(const char *name, bool has_devices,
                          strList *devices, Error **errp)
{
    return 0;
}

int bdrv_all_create_snapshot(QEMUSnapshotInfo *sn, BlockDriverState *vm_state_bs,
                             uint64_t vm_state_size, bool has_devices,
                             strList *devices, Error **errp)
{
    error_setg(errp, "the libretro core keeps no snapshots in its disks");
    return -ENOTSUP;
}

BlockDriverState *bdrv_all_find_vmstate_bs(const char *vmstate_bs,
                                           bool has_devices, strList *devices,
                                           Error **errp)
{
    error_setg(errp, "the libretro core keeps no snapshots in its disks");
    return NULL;
}

int bdrv_snapshot_find(BlockDriverState *bs, QEMUSnapshotInfo *sn_info,
                       const char *name)
{
    return -ENOENT;
}

int bdrv_snapshot_list(BlockDriverState *bs, QEMUSnapshotInfo **psn_info)
{
    *psn_info = NULL;
    return 0;
}

QIOChannelBlock *qio_channel_block_new(BlockDriverState *bs)
{
    abort();
}

/* Migration: the images are never handed to another process */
bool migration_block_activate(Error **errp)
{
    return true;
}

bool migration_block_inactivate(void)
{
    return true;
}

void dirty_bitmap_mig_init(void)
{
}

void dirty_bitmap_mig_before_vm_start(void)
{
}

void dirty_bitmap_mig_cancel_outgoing(void)
{
}

void dirty_bitmap_mig_cancel_incoming(void)
{
}

bool check_dirty_bitmap_mig_alias_map(const BitmapMigrationNodeAliasList *bbm,
                                      Error **errp)
{
    if (bbm) {
        error_setg(errp, "the libretro core has no dirty bitmaps to migrate");
        return false;
    }
    return true;
}

/* Jobs (the block jobs, QMP's snapshot-save and -load): none run */
void *job_create(const char *job_id, const JobDriver *driver, JobTxn *txn,
                 AioContext *ctx, int flags, BlockCompletionFunc *cb,
                 void *opaque, Error **errp)
{
    error_setg(errp, "the libretro core runs no jobs");
    return NULL;
}

void job_start(Job *job)
{
    abort();
}

void job_progress_update(Job *job, uint64_t done)
{
    abort();
}

void job_progress_set_remaining(Job *job, uint64_t remaining)
{
    abort();
}

void job_cancel_sync_all(void)
{
}
