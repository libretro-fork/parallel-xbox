/*
 * A qcow2 image's disk as a raw image (block/qcow2-raw.c): the core keeps
 * the Xbox's hard disk as a raw, sparse file, and an xbox_hdd.qcow2 (an
 * earlier build's, standalone xemu's, the image the core carries) is
 * turned into one once. Plain C, no QEMU headers.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BLOCK_QCOW2_RAW_H
#define BLOCK_QCOW2_RAW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "libretro-file.h"

/* Reads len bytes of the qcow2 image at off into buf. Returns how many it
 * read (fewer only at the image's end), -errno on failure. */
typedef int64_t (*LRQcow2ReadFunc)(void *opaque, uint64_t off, void *buf,
                                   size_t len);

/* whether head (the image's first bytes) starts a qcow2 image */
bool lr_qcow2_is_image(const void *head, size_t len);

/*
 * The disk the image holds into dst, an empty file: dst ends up the disk's
 * size, with only the clusters that hold data other than zeroes written
 * (the rest are holes). Versions 2 and 3, zlib-compressed clusters
 * (deflate, libretro-common's), zero clusters; not encryption, backing
 * files, external data files, zstd or subclusters (none of which xemu
 * makes). The image's internal snapshots are left behind: the disk is the
 * image's current state. 0 on success; otherwise -errno, and err (errlen
 * bytes) says why.
 */
int lr_qcow2_to_raw(LRQcow2ReadFunc read, void *opaque, LRFile *dst,
                    char *err, size_t errlen);

#endif
