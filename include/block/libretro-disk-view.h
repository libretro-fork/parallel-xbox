/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLOCK_LIBRETRO_DISK_VIEW_H
#define BLOCK_LIBRETRO_DISK_VIEW_H
#include "block/libretro-disk.h"
/* Pure capability/range query, no I/O or allocation. Zero selects ordinary
 * preadv; otherwise this bounds each retained source request. The complete
 * logical range is checked before any destination can be written. */
size_t lr_disk_read_view_max(BlockBackend *blk, uint64_t offset, uint64_t size);
uint64_t lr_disk_read_identity(BlockBackend *blk);
#endif
