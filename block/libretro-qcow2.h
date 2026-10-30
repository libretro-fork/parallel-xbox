/*
 * A qcow2 image read and written in place (block/libretro-qcow2.c), for the
 * core's disk layer: the xbox_hdd.qcow2 that standalone xemu and earlier
 * builds use, shared with them as it is. Plain C over block/libretro-file.h,
 * no QEMU headers: libretro/tests/test-qcow2.c builds it on its own.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BLOCK_LIBRETRO_QCOW2_H
#define BLOCK_LIBRETRO_QCOW2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libretro-file.h"

typedef struct LRQcow2 LRQcow2;

/* The image in f (which stays the caller's to close, after this). NULL
 * when it is not one this can use, err (errlen bytes) saying why. Every
 * table the disk is found through is read into memory here, once: no
 * allocation after. */
LRQcow2 *lr_qcow2_open(LRFile *f, bool writable, char *err, size_t errlen);
void lr_qcow2_close(LRQcow2 *q);

/* the disk's size */
int64_t lr_qcow2_size(LRQcow2 *q);

/* Whole requests on the disk: 0, or -errno. */
int lr_qcow2_preadv(LRQcow2 *q, int64_t off, const LRFileIOV *iov, int n);
int lr_qcow2_pwritev(LRQcow2 *q, int64_t off, const LRFileIOV *iov, int n);

/* the range reads zeroes; its whole clusters are given back (holes where
 * nothing else uses them) */
int lr_qcow2_zero(LRQcow2 *q, int64_t off, int64_t len);

/* to the disk, when its tables changed since the last time: 0 or -errno */
int lr_qcow2_flush(LRQcow2 *q);

#endif
