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
    if (offset < 0) {
        return -EINVAL;
    }
    if (*cursor != offset) {
        int64_t position = vfs->seek(file, offset, RETRO_VFS_SEEK_POSITION_START);
        if (position != offset) {
            *cursor = -1;
            return -EIO;
        }
        *cursor = position;
    }
    for (int i = 0; i < count; i++) {
        uint8_t *data = iov[i].base;
        size_t left = iov[i].len;
        while (left) {
            int64_t n = vfs->read(file, data, left);
            if (n < 0 || (uint64_t)n > left || n > INT64_MAX - *cursor) {
                *cursor = -1;
                return -EIO;
            }
            if (!n) {
                memset(data, 0, left);
                for (int j = i + 1; j < count; j++) {
                    memset(iov[j].base, 0, iov[j].len);
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
