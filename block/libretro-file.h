/*
 * A disk image file, as the core's disk layer (block/libretro-disk.c) and
 * the HDD's conversion (block/qcow2-raw.c) reach it: positional reads and
 * writes, a size that leaves the file sparse, and holes punched where the
 * data is dropped. The host's own calls (POSIX, Win32), not the
 * frontend's VFS, which has no sparse files and no holes. Plain C, no QEMU
 * headers: libretro/tests/test-qcow2-raw.c builds it on its own.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BLOCK_LIBRETRO_FILE_H
#define BLOCK_LIBRETRO_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct LRFile LRFile;

typedef struct LRFileIOV {
    void *base;
    size_t len;
} LRFileIOV;

/* NULL with errno set when it cannot be opened. create: made (empty) when
 * absent, and sparse where the file system has sparse files. */
LRFile *lr_file_open(const char *path, bool writable, bool create);
/* writes what the system holds of it to the disk first, when writable */
void lr_file_close(LRFile *f);

/* the file's size, -errno on failure */
int64_t lr_file_size(LRFile *f);
/* the size, the end grown as a hole; 0 or -errno */
int lr_file_set_size(LRFile *f, int64_t size);

/* Whole requests: 0 when every byte moved, -errno otherwise. A read past
 * the end reads zeroes. */
int lr_file_preadv(LRFile *f, int64_t off, const LRFileIOV *iov, int n);
int lr_file_pwritev(LRFile *f, int64_t off, const LRFileIOV *iov, int n);
int lr_file_pread(LRFile *f, int64_t off, void *buf, size_t len);
int lr_file_pwrite(LRFile *f, int64_t off, const void *buf, size_t len);

/* The range reads zeroes afterwards; what the file system can free of it
 * is freed (a hole), the rest written as zeroes. 0 or -errno. */
int lr_file_zero(LRFile *f, int64_t off, int64_t len);

/* to the disk: 0 or -errno */
int lr_file_sync(LRFile *f);

/* the space the file takes on the disk, in bytes (its holes not counted);
 * -1 when the system cannot tell */
int64_t lr_file_allocated(LRFile *f);

#endif
