/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
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
#include "qemu/madvise.h"
#include "qemu/error-report.h"
#include "qemu/iov.h"
#include "migration.h"
#include "migration-stats.h"
#include "qemu-file.h"
#include "trace.h"
#include "options.h"
#include "qapi/error.h"
#include "rdma.h"
#include "io/channel-file.h"

#define IO_BUF_SIZE 32768
#define MAX_IOV_SIZE MIN_CONST(IOV_MAX, 64)

typedef struct FdEntry {
    QTAILQ_ENTRY(FdEntry) entry;
    int fd;
} FdEntry;

struct QEMUFile {
    QIOChannel *ioc;
    bool is_writable;

    int buf_index;
    int buf_size; /* 0 when writing */
    uint8_t *buf; /* buf_storage, or a window of the mapped input */
    uint8_t buf_storage[IO_BUF_SIZE];

    /* libretro: an input file over a buffer in memory (a savestate the
     * frontend hands over) reads it in place, buf a window of it, rather
     * than copying it IO_BUF_SIZE at a time into buf_storage first */
    const uint8_t *map;
    size_t map_len;
    size_t map_pos; /* where buf's window starts in map */

    DECLARE_BITMAP(may_free, MAX_IOV_SIZE);
    struct iovec iov[MAX_IOV_SIZE];
    unsigned int iovcnt;

    int last_error;
    Error *last_error_obj;

    bool can_pass_fd;
    QTAILQ_HEAD(, FdEntry) fds;
};

static QEMUFile *qemu_file_new_impl(QIOChannel *ioc, bool is_writable)
{
    QEMUFile *f;

    f = g_new0(QEMUFile, 1);

    object_ref(ioc);
    f->ioc = ioc;
    f->is_writable = is_writable;
    f->buf = f->buf_storage;
    f->can_pass_fd = qio_channel_has_feature(ioc, QIO_CHANNEL_FEATURE_FD_PASS);
    QTAILQ_INIT(&f->fds);

    return f;
}

QEMUFile *qemu_file_new_output(QIOChannel *ioc)
{
    return qemu_file_new_impl(ioc, true);
}

/*
 * An input file that reads data, len bytes the caller keeps alive until
 * the file is closed, in place: nothing is read from ioc, which is only
 * the channel the file is closed and shut down through.
 */
QEMUFile *qemu_file_new_input_mapped(QIOChannel *ioc, const void *data,
                                     size_t len)
{
    QEMUFile *f = qemu_file_new_impl(ioc, false);

    f->map = data;
    f->map_len = len;
    return f;
}

/*
 * Get last error for stream f with optional Error*
 *
 * Return negative error value if there has been an error on previous
 * operations, return 0 if no error happened.
 *
 * If errp is specified, a verbose error message will be copied over.
 */
int qemu_file_get_error_obj(QEMUFile *f, Error **errp)
{
    if (!f->last_error) {
        return 0;
    }

    /* There is an error */
    if (errp) {
        if (f->last_error_obj) {
            *errp = error_copy(f->last_error_obj);
        } else {
            error_setg_errno(errp, -f->last_error, "Channel error");
        }
    }

    return f->last_error;
}

/*
 * Get last error for either stream f1 or f2 with optional Error*.
 * The error returned (non-zero) can be either from f1 or f2.
 *
 * If any of the qemufile* is NULL, then skip the check on that file.
 *
 * When there is no error on both qemufile, zero is returned.
 */
int qemu_file_get_error_obj_any(QEMUFile *f1, QEMUFile *f2, Error **errp)
{
    int ret = 0;

    if (f1) {
        ret = qemu_file_get_error_obj(f1, errp);
        /* If there's already error detected, return */
        if (ret) {
            return ret;
        }
    }

    if (f2) {
        ret = qemu_file_get_error_obj(f2, errp);
    }

    return ret;
}

/*
 * Set the last error for stream f with optional Error*
 */
void qemu_file_set_error_obj(QEMUFile *f, int ret, Error *err)
{
    if (f->last_error == 0 && ret) {
        f->last_error = ret;
        error_propagate(&f->last_error_obj, err);
    } else if (err) {
        error_report_err(err);
    }
}

/*
 * Get last error for stream f
 *
 * Return negative error value if there has been an error on previous
 * operations, return 0 if no error happened.
 *
 */
int qemu_file_get_error(QEMUFile *f)
{
    return f->last_error;
}

/*
 * Set the last error for stream f
 */
void qemu_file_set_error(QEMUFile *f, int ret)
{
    qemu_file_set_error_obj(f, ret, NULL);
}

static bool qemu_file_is_writable(QEMUFile *f)
{
    return f->is_writable;
}

static void qemu_iovec_release_ram(QEMUFile *f)
{
    struct iovec iov;
    unsigned long idx;

    /* Find and release all the contiguous memory ranges marked as may_free. */
    idx = find_next_bit(f->may_free, f->iovcnt, 0);
    if (idx >= f->iovcnt) {
        return;
    }
    iov = f->iov[idx];

    /* The madvise() in the loop is called for iov within a continuous range and
     * then reinitialize the iov. And in the end, madvise() is called for the
     * last iov.
     */
    while ((idx = find_next_bit(f->may_free, f->iovcnt, idx + 1)) < f->iovcnt) {
        /* check for adjacent buffer and coalesce them */
        if (iov.iov_base + iov.iov_len == f->iov[idx].iov_base) {
            iov.iov_len += f->iov[idx].iov_len;
            continue;
        }
        if (qemu_madvise(iov.iov_base, iov.iov_len, QEMU_MADV_DONTNEED) < 0) {
            error_report("migrate: madvise DONTNEED failed %p %zd: %s",
                         iov.iov_base, iov.iov_len, strerror(errno));
        }
        iov = f->iov[idx];
    }
    if (qemu_madvise(iov.iov_base, iov.iov_len, QEMU_MADV_DONTNEED) < 0) {
            error_report("migrate: madvise DONTNEED failed %p %zd: %s",
                         iov.iov_base, iov.iov_len, strerror(errno));
    }
    memset(f->may_free, 0, sizeof(f->may_free));
}

/**
 * Flushes QEMUFile buffer
 *
 * This will flush all pending data. If data was only partially flushed, it
 * will set an error state.
 */
int qemu_fflush(QEMUFile *f)
{
    if (!qemu_file_is_writable(f)) {
        return f->last_error;
    }

    if (f->last_error) {
        return f->last_error;
    }
    if (f->iovcnt > 0) {
        Error *local_error = NULL;
        if (qio_channel_writev_all(f->ioc,
                                   f->iov, f->iovcnt,
                                   &local_error) < 0) {
            qemu_file_set_error_obj(f, -EIO, local_error);
        } else {
            uint64_t size = iov_size(f->iov, f->iovcnt);
            stat64_add(&mig_stats.qemu_file_transferred, size);
        }

        qemu_iovec_release_ram(f);
    }

    f->buf_index = 0;
    f->iovcnt = 0;
    return f->last_error;
}

/*
 * Attempt to fill the buffer from the underlying file
 * Returns the number of bytes read, or negative value for an error.
 *
 * Note that it can return a partially full buffer even in a not error/not EOF
 * case if the underlying file descriptor gives a short read, and that can
 * happen even on a blocking fd.
 */
static ssize_t coroutine_mixed_fn qemu_fill_buffer(QEMUFile *f)
{
    int len;
    int pending;
    Error *local_error = NULL;
    g_autofree int *fds = NULL;
    size_t nfd = 0;
    int **pfds = f->can_pass_fd ? &fds : NULL;
    size_t *pnfd = f->can_pass_fd ? &nfd : NULL;

    assert(!qemu_file_is_writable(f));

    if (f->map) {
        /* The window moves on to the first unread byte, and reaches as
         * far as a buffer would: no byte is copied */
        pending = f->buf_size - f->buf_index;
        f->map_pos += f->buf_index;
        f->buf = (uint8_t *)f->map + f->map_pos;
        f->buf_index = 0;
        f->buf_size = MIN(f->map_len - f->map_pos, IO_BUF_SIZE);
        len = f->buf_size - pending;
        if (len <= 0) {
            qemu_file_set_error(f, -EIO); /* the end, as a channel's */
        }
        return len;
    }

    pending = f->buf_size - f->buf_index;
    if (pending > 0) {
        memmove(f->buf, f->buf + f->buf_index, pending);
    }
    f->buf_index = 0;
    f->buf_size = pending;

    if (qemu_file_get_error(f)) {
        return 0;
    }

    do {
        struct iovec iov = { f->buf + pending, IO_BUF_SIZE - pending };
        len = qio_channel_readv_full(f->ioc, &iov, 1, pfds, pnfd,
                                     QIO_CHANNEL_READ_FLAG_FD_PRESERVE_BLOCKING,
                                     &local_error);
        if (len == QIO_CHANNEL_ERR_BLOCK) {
            qio_channel_wait_cond(f->ioc, G_IO_IN);
        }
    } while (len == QIO_CHANNEL_ERR_BLOCK);

    if (len > 0) {
        f->buf_size += len;
    } else {
        qemu_file_set_error_obj(f, -EIO, local_error);
    }

    for (int i = 0; i < nfd; i++) {
        FdEntry *fde = g_new0(FdEntry, 1);
        fde->fd = fds[i];
        QTAILQ_INSERT_TAIL(&f->fds, fde, entry);
    }

    return len;
}

/** Closes the file
 *
 * Returns negative error value if any error happened on previous operations or
 * while closing the file. Returns 0 or positive number on success.
 *
 * The meaning of return value on success depends on the specific backend
 * being used.
 */
int qemu_fclose(QEMUFile *f)
{
    FdEntry *fde, *next;
    int ret = qemu_fflush(f);
    int ret2 = qio_channel_close(f->ioc, NULL);
    if (ret >= 0) {
        ret = ret2;
    }
    QTAILQ_FOREACH_SAFE(fde, &f->fds, entry, next) {
        warn_report("qemu_fclose: received fd %d was never claimed", fde->fd);
        close(fde->fd);
        g_free(fde);
    }
    g_clear_pointer(&f->ioc, object_unref);
    error_free(f->last_error_obj);
    g_free(f);
    trace_qemu_file_fclose();
    return ret;
}

/*
 * Add buf to iovec. Do flush if iovec is full.
 *
 * Return values:
 * 1 iovec is full and flushed
 * 0 iovec is not flushed
 *
 */
static int add_to_iovec(QEMUFile *f, const uint8_t *buf, size_t size,
                        bool may_free)
{
    /* check for adjacent buffer and coalesce them */
    if (f->iovcnt > 0 && buf == f->iov[f->iovcnt - 1].iov_base +
        f->iov[f->iovcnt - 1].iov_len &&
        may_free == test_bit(f->iovcnt - 1, f->may_free))
    {
        f->iov[f->iovcnt - 1].iov_len += size;
    } else {
        if (f->iovcnt >= MAX_IOV_SIZE) {
            /* Should only happen if a previous fflush failed */
            assert(qemu_file_get_error(f) || !qemu_file_is_writable(f));
            return 1;
        }
        if (may_free) {
            set_bit(f->iovcnt, f->may_free);
        }
        f->iov[f->iovcnt].iov_base = (uint8_t *)buf;
        f->iov[f->iovcnt++].iov_len = size;
    }

    if (f->iovcnt >= MAX_IOV_SIZE) {
        qemu_fflush(f);
        return 1;
    }

    return 0;
}

static void add_buf_to_iovec(QEMUFile *f, size_t len)
{
    if (!add_to_iovec(f, f->buf + f->buf_index, len, false)) {
        f->buf_index += len;
        if (f->buf_index == IO_BUF_SIZE) {
            qemu_fflush(f);
        }
    }
}

void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, size_t size)
{
    size_t l;

    if (f->last_error) {
        return;
    }

    while (size > 0) {
        l = IO_BUF_SIZE - f->buf_index;
        if (l > size) {
            l = size;
        }
        memcpy(f->buf + f->buf_index, buf, l);
        add_buf_to_iovec(f, l);
        if (qemu_file_get_error(f)) {
            break;
        }
        buf += l;
        size -= l;
    }
}



void qemu_put_byte(QEMUFile *f, int v)
{
    if (f->last_error) {
        return;
    }

    f->buf[f->buf_index] = v;
    add_buf_to_iovec(f, 1);
}

void qemu_file_skip(QEMUFile *f, int size)
{
    if (f->buf_index + size <= f->buf_size) {
        f->buf_index += size;
    }
}

/*
 * Read 'size' bytes from file (at 'offset') without moving the
 * pointer and set 'buf' to point to that data.
 *
 * It will return size bytes unless there was an error, in which case it will
 * return as many as it managed to read (assuming blocking fd's which
 * all current QEMUFile are)
 */
size_t coroutine_mixed_fn qemu_peek_buffer(QEMUFile *f, uint8_t **buf, size_t size, size_t offset)
{
    ssize_t pending;
    size_t index;

    assert(!qemu_file_is_writable(f));
    assert(offset < IO_BUF_SIZE);
    assert(size <= IO_BUF_SIZE - offset);

    /* The 1st byte to read from */
    index = f->buf_index + offset;
    /* The number of available bytes starting at index */
    pending = f->buf_size - index;

    /*
     * qemu_fill_buffer might return just a few bytes, even when there isn't
     * an error, so loop collecting them until we get enough.
     */
    while (pending < size) {
        int received = qemu_fill_buffer(f);

        if (received <= 0) {
            break;
        }

        index = f->buf_index + offset;
        pending = f->buf_size - index;
    }

    if (pending <= 0) {
        return 0;
    }
    if (size > pending) {
        size = pending;
    }

    *buf = f->buf + index;
    return size;
}

/*
 * Read 'size' bytes of data from the file into buf.
 * 'size' can be larger than the internal buffer.
 *
 * It will return size bytes unless there was an error, in which case it will
 * return as many as it managed to read (assuming blocking fd's which
 * all current QEMUFile are)
 */
size_t coroutine_mixed_fn qemu_get_buffer(QEMUFile *f, uint8_t *buf, size_t size)
{
    size_t pending = size;
    size_t done = 0;

    while (pending > 0) {
        size_t res;
        uint8_t *src;

        res = qemu_peek_buffer(f, &src, MIN(pending, IO_BUF_SIZE), 0);
        if (res == 0) {
            return done;
        }
        memcpy(buf, src, res);
        qemu_file_skip(f, res);
        buf += res;
        pending -= res;
        done += res;
    }
    return done;
}

/*
 * Peeks a single byte from the buffer; this isn't guaranteed to work if
 * offset leaves a gap after the previous read/peeked data.
 */
int coroutine_mixed_fn qemu_peek_byte(QEMUFile *f, int offset)
{
    int index = f->buf_index + offset;

    assert(!qemu_file_is_writable(f));
    assert(offset < IO_BUF_SIZE);

    if (index >= f->buf_size) {
        qemu_fill_buffer(f);
        index = f->buf_index + offset;
        if (index >= f->buf_size) {
            return 0;
        }
    }
    return f->buf[index];
}

int coroutine_mixed_fn qemu_get_byte(QEMUFile *f)
{
    int result;

    result = qemu_peek_byte(f, 0);
    qemu_file_skip(f, 1);
    return result;
}

uint64_t qemu_file_transferred(QEMUFile *f)
{
    uint64_t ret = stat64_get(&mig_stats.qemu_file_transferred);
    int i;

    g_assert(qemu_file_is_writable(f));

    for (i = 0; i < f->iovcnt; i++) {
        ret += f->iov[i].iov_len;
    }

    return ret;
}

void qemu_put_be16(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be32(QEMUFile *f, unsigned int v)
{
    qemu_put_byte(f, v >> 24);
    qemu_put_byte(f, v >> 16);
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be64(QEMUFile *f, uint64_t v)
{
    qemu_put_be32(f, v >> 32);
    qemu_put_be32(f, v);
}

unsigned int qemu_get_be16(QEMUFile *f)
{
    unsigned int v;
    v = qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

unsigned int qemu_get_be32(QEMUFile *f)
{
    unsigned int v;
    v = (unsigned int)qemu_get_byte(f) << 24;
    v |= qemu_get_byte(f) << 16;
    v |= qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

uint64_t qemu_get_be64(QEMUFile *f)
{
    uint64_t v;
    v = (uint64_t)qemu_get_be32(f) << 32;
    v |= qemu_get_be32(f);
    return v;
}

/*
 * Get a string whose length is determined by a single preceding byte
 * A preallocated 256 byte buffer must be passed in.
 * Returns: len on success and a 0 terminated string in the buffer
 *          else 0
 *          (Note a 0 length string will return 0 either way)
 */
size_t coroutine_fn qemu_get_counted_string(QEMUFile *f, char buf[256])
{
    size_t len = qemu_get_byte(f);
    size_t res = qemu_get_buffer(f, (uint8_t *)buf, len);

    buf[res] = 0;

    return res == len ? res : 0;
}

/*
 * qemu_file_get_ioc:
 *
 * Get the ioc object for the file, without incrementing
 * the reference count.
 *
 * Returns: the ioc object
 */
QIOChannel *qemu_file_get_ioc(QEMUFile *file)
{
    return file->ioc;
}

