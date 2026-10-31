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
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/bswap.h"
#include "qemu-file.h"

/* Saved-state cursors read and write bounded frontend-owned memory.
 * The caller owns this small cursor and the backing buffer until finish. */
static void qemu_file_init(QEMUFile *f, const void *data, size_t size,
                           bool writable)
{
    assert(data || !size);
    *f = (QEMUFile) {
        .buf = (uint8_t *)data,
        .buf_size = size,
        .is_writable = writable,
    };
}

void qemu_file_init_input(QEMUFile *f, const void *data, size_t size)
{
    qemu_file_init(f, data, size, false);
}

void qemu_file_init_output(QEMUFile *f, void *data, size_t size)
{
    qemu_file_init(f, data, size, true);
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
            error_setg_errno(errp, -f->last_error, "State buffer error");
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


static size_t qemu_file_remaining(QEMUFile *f)
{
    return f->buf_size - f->buf_index;
}

static bool put_fast(QEMUFile *f, size_t size)
{
    if (f->last_error) {
        return false;
    }
    if (!f->is_writable) {
        qemu_file_set_error(f, -EBADF);
        return false;
    }
    if (size > qemu_file_remaining(f)) {
        qemu_file_set_error(f, -ENOSPC);
        return false;
    }
    return true;
}

static bool get_fast(QEMUFile *f, size_t size)
{
    return !f->last_error && !f->is_writable &&
           size <= qemu_file_remaining(f);
}

int qemu_fflush(QEMUFile *f)
{
    /* Writes are already in their final destination. */
    return f->last_error;
}

int qemu_fclose(QEMUFile *f)
{
    /* Cursor and backing storage remain owned by the caller. */
    error_free(f->last_error_obj);
    f->last_error_obj = NULL;
    return f->last_error;
}

uint64_t qemu_file_transferred(QEMUFile *f)
{
    return f->buf_index;
}

void qemu_put_buffer(QEMUFile *f, const uint8_t *buf, size_t size)
{
    if (put_fast(f, size)) {
        if (size) {
            memcpy(f->buf + f->buf_index, buf, size);
        }
        f->buf_index += size;
    }
}

void qemu_put_byte(QEMUFile *f, int value)
{
    if (put_fast(f, 1)) {
        f->buf[f->buf_index++] = value;
    }
}

void qemu_file_skip(QEMUFile *f, int size)
{
    if (size < 0 || (size_t)size > qemu_file_remaining(f)) {
        qemu_file_set_error(f, -EIO);
        return;
    }
    f->buf_index += size;
}

size_t coroutine_mixed_fn qemu_peek_buffer(QEMUFile *f, uint8_t **buf,
                                           size_t size, size_t offset)
{
    size_t available, result;

    *buf = NULL;
    if (f->last_error) {
        return 0;
    }
    if (f->is_writable) {
        qemu_file_set_error(f, -EBADF);
        return 0;
    }
    available = qemu_file_remaining(f);
    if (offset > available) {
        qemu_file_set_error(f, -EIO);
        return 0;
    }
    result = MIN(size, available - offset);
    if (result) {
        *buf = f->buf + f->buf_index + offset;
    }
    if (result < size) {
        qemu_file_set_error(f, -EIO);
    }
    return result;
}

size_t coroutine_mixed_fn qemu_get_buffer(QEMUFile *f, uint8_t *buf, size_t size)
{
    uint8_t *source;
    size_t result = qemu_peek_buffer(f, &source, size, 0);

    if (result) {
        memcpy(buf, source, result);
        f->buf_index += result;
    }
    return result;
}

int coroutine_mixed_fn qemu_peek_byte(QEMUFile *f, int offset)
{
    uint8_t *byte;

    if (offset < 0) {
        qemu_file_set_error(f, -EIO);
        return 0;
    }
    return qemu_peek_buffer(f, &byte, 1, offset) == 1 ? *byte : 0;
}

int coroutine_mixed_fn qemu_get_byte(QEMUFile *f)
{
    uint8_t *byte;

    if (qemu_peek_buffer(f, &byte, 1, 0) != 1) {
        return 0;
    }
    f->buf_index++;
    return *byte;
}

void qemu_put_be16(QEMUFile *f, unsigned int v)
{
    if (put_fast(f, 2)) {
        stw_be_p(f->buf + f->buf_index, v);
        f->buf_index += 2;
        return;
    }
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be32(QEMUFile *f, unsigned int v)
{
    if (put_fast(f, 4)) {
        stl_be_p(f->buf + f->buf_index, v);
        f->buf_index += 4;
        return;
    }
    qemu_put_byte(f, v >> 24);
    qemu_put_byte(f, v >> 16);
    qemu_put_byte(f, v >> 8);
    qemu_put_byte(f, v);
}

void qemu_put_be64(QEMUFile *f, uint64_t v)
{
    if (put_fast(f, 8)) {
        stq_be_p(f->buf + f->buf_index, v);
        f->buf_index += 8;
        return;
    }
    qemu_put_be32(f, v >> 32);
    qemu_put_be32(f, v);
}

unsigned int qemu_get_be16(QEMUFile *f)
{
    unsigned int v;

    if (get_fast(f, 2)) {
        v = lduw_be_p(f->buf + f->buf_index);
        f->buf_index += 2;
        return v;
    }
    v = qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

unsigned int qemu_get_be32(QEMUFile *f)
{
    unsigned int v;

    if (get_fast(f, 4)) {
        v = ldl_be_p(f->buf + f->buf_index);
        f->buf_index += 4;
        return v;
    }
    v = (unsigned int)qemu_get_byte(f) << 24;
    v |= qemu_get_byte(f) << 16;
    v |= qemu_get_byte(f) << 8;
    v |= qemu_get_byte(f);
    return v;
}

uint64_t qemu_get_be64(QEMUFile *f)
{
    uint64_t v;

    if (get_fast(f, 8)) {
        v = ldq_be_p(f->buf + f->buf_index);
        f->buf_index += 8;
        return v;
    }
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

