/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIBRETRO_VFS_READ_H
#define LIBRETRO_VFS_READ_H
#include "libretro-file.h"
#include <libretro.h>
#include <errno.h>
#include <string.h>

/* The medium owns the handle exclusively. Cursor is host I/O state, not
 * emulated state: restored/random requests seek only when actually needed. */
static inline int lr_vfs_readv(const struct retro_vfs_interface *vfs,
                               struct retro_vfs_file_handle *file,
                               int64_t *cursor, int64_t offset,
                               const LRFileIOV *iov, int count)
{
    uint64_t bytes = 0;

    if (offset < 0 || count < 0 || (count && !iov)) {
        return -EINVAL;
    }
    /* Validate the entire range before a seek/read can change provider or
     * destination state, including spans after EOF and nonadjacent spans. */
    for (int i = 0; i < count; i++) {
        if (iov[i].len > (uint64_t)(INT64_MAX - offset) - bytes) {
            return -EINVAL;
        }
        bytes += iov[i].len;
    }
    if (!bytes) {
        return 0;
    }
    if (*cursor != offset) {
        if (vfs->seek(file, offset, RETRO_VFS_SEEK_POSITION_START) < 0) {
            *cursor = -1;
            return -EIO;
        }
        /* The VFS API returns zero on success, not the resulting position. */
        *cursor = offset;
    }
    for (int i = 0; i < count;) {
        uint8_t *data = iov[i].base;
        size_t left = iov[i++].len;
        /* Adjacent guest spans need one provider call, with no staging copy.
         * Keep gaps separate and retry any short successful bulk read. */
        while (i < count && left <= UINTPTR_MAX - (uintptr_t)data &&
               (uintptr_t)iov[i].base == (uintptr_t)data + left &&
               iov[i].len <= SIZE_MAX - left) {
            left += iov[i++].len;
        }
        if (left > (uint64_t)(INT64_MAX - *cursor)) {
            return -EINVAL;
        }
        while (left) {
            int64_t n = vfs->read(file, data, left);
            if (n < 0 || (uint64_t)n > left || n > INT64_MAX - *cursor) {
                *cursor = -1;
                return -EIO;
            }
            if (!n) {
                memset(data, 0, left);
                for (int j = i; j < count; j++) {
                    if (iov[j].len) {
                        memset(iov[j].base, 0, iov[j].len);
                    }
                }
                return 0;
            }
            data += n;
            left -= n;
            *cursor += n;
        }
    }
    return 0;
}
#endif
