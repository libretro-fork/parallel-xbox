/*
 * Block driver over the libretro frontend's VFS
 *
 * The libretro core is handed a disc image as a path, and on Android's Play
 * Store RetroArch that path is a URI (saf://...) that only the frontend can
 * open. The DVD drive is given that path (system/vl.c), and this protocol
 * driver reads it through RetroArch's VFS: open, size, seek and read. Read
 * only, as a DVD is.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qobject/qdict.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qemu/thread.h"
#include "block/block-io.h"
#include "block/block_int.h"

#include <libretro.h>

/* ui/libretro.c: the frontend's VFS, if it has one */
extern struct retro_vfs_interface *xemu_libretro_vfs;

typedef struct {
    struct retro_vfs_file_handle *handle;
    int64_t length;
    QemuMutex lock; /* a seek and its read belong together */
} BDRVLibretroVFSState;

static QemuOptsList runtime_opts = {
    .name = "libretro-vfs",
    .head = QTAILQ_HEAD_INITIALIZER(runtime_opts.head),
    .desc = {
        {
            .name = "filename",
            .type = QEMU_OPT_STRING,
            .help = "the URI the frontend handed over",
        },
        { /* end of list */ }
    },
};

static void lrvfs_parse_filename(const char *filename, QDict *options,
                                 Error **errp)
{
    qdict_put_str(options, "filename", filename);
}

static int lrvfs_open(BlockDriverState *bs, QDict *options, int flags,
                      Error **errp)
{
    BDRVLibretroVFSState *s = bs->opaque;
    QemuOpts *opts = qemu_opts_create(&runtime_opts, NULL, 0, &error_abort);
    const char *filename;
    int ret;

    if (!qemu_opts_absorb_qdict(opts, options, errp)) {
        ret = -EINVAL;
        goto out;
    }
    filename = qemu_opt_get(opts, "filename");
    if (!filename) {
        error_setg(errp, "no URI given");
        ret = -EINVAL;
        goto out;
    }
    if (!xemu_libretro_vfs) {
        error_setg(errp, "%s can only be read through the frontend's VFS, "
                   "which it does not offer", filename);
        ret = -ENOTSUP;
        goto out;
    }
    ret = bdrv_apply_auto_read_only(bs, "the frontend's VFS is read only here",
                                    errp);
    if (ret < 0) {
        goto out;
    }
    s->handle = xemu_libretro_vfs->open(filename, RETRO_VFS_FILE_ACCESS_READ,
                                        RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!s->handle) {
        error_setg(errp, "the frontend cannot open %s", filename);
        ret = -ENOENT;
        goto out;
    }
    s->length = xemu_libretro_vfs->size(s->handle);
    if (s->length < 0) {
        xemu_libretro_vfs->close(s->handle);
        s->handle = NULL;
        error_setg(errp, "the frontend cannot tell the size of %s", filename);
        ret = -EIO;
        goto out;
    }
    qemu_mutex_init(&s->lock);
    ret = 0;
out:
    qemu_opts_del(opts);
    return ret;
}

static void lrvfs_close(BlockDriverState *bs)
{
    BDRVLibretroVFSState *s = bs->opaque;

    if (s->handle) {
        xemu_libretro_vfs->close(s->handle);
        s->handle = NULL;
        qemu_mutex_destroy(&s->lock);
    }
}

static int64_t coroutine_fn lrvfs_co_getlength(BlockDriverState *bs)
{
    BDRVLibretroVFSState *s = bs->opaque;

    return s->length;
}

static int coroutine_fn lrvfs_co_preadv(BlockDriverState *bs, int64_t offset,
                                        int64_t bytes, QEMUIOVector *qiov,
                                        BdrvRequestFlags flags)
{
    BDRVLibretroVFSState *s = bs->opaque;
    int64_t got = 0;
    int ret = 0;

    /* Straight into the request's buffers, each in turn: no buffer of
     * the request's size allocated and copied out of */
    qemu_mutex_lock(&s->lock);
    if (xemu_libretro_vfs->seek(s->handle, offset,
                                RETRO_VFS_SEEK_POSITION_START) < 0) {
        ret = -EIO;
    }
    for (int i = 0; !ret && i < qiov->niov && got < bytes; i++) {
        size_t want = MIN(qiov->iov[i].iov_len, (size_t)(bytes - got));
        int64_t r = xemu_libretro_vfs->read(s->handle, qiov->iov[i].iov_base,
                                            want);
        if (r < 0) {
            ret = -EIO;
            break;
        }
        got += r;
        if ((size_t)r < want) {
            break; /* the image's end */
        }
    }
    qemu_mutex_unlock(&s->lock);

    /* past the end of the image reads as zeroes, as a file does */
    if (!ret && got < bytes) {
        qemu_iovec_memset(qiov, got, 0, bytes - got);
    }
    return ret;
}

#define LRVFS_DRIVER(name, scheme) \
    static BlockDriver name = { \
        .format_name         = "libretro-vfs-" scheme, \
        .protocol_name       = scheme, \
        .instance_size       = sizeof(BDRVLibretroVFSState), \
        .bdrv_parse_filename = lrvfs_parse_filename, \
        .bdrv_open           = lrvfs_open, \
        .bdrv_close          = lrvfs_close, \
        .bdrv_co_getlength   = lrvfs_co_getlength, \
        .bdrv_co_preadv      = lrvfs_co_preadv, \
    }

/* RetroArch's SAF paths (libretro-common vfs_implementation_saf.c), and
 * content:// for a frontend that passes Android's own URIs on */
LRVFS_DRIVER(bdrv_libretro_vfs_saf, "saf");
LRVFS_DRIVER(bdrv_libretro_vfs_content, "content");

static void bdrv_libretro_vfs_init(void)
{
    bdrv_register(&bdrv_libretro_vfs_saf);
    bdrv_register(&bdrv_libretro_vfs_content);
}

block_init(bdrv_libretro_vfs_init);
