/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLOCK_LIBRETRO_DISK_DEFERRED_H
#define BLOCK_LIBRETRO_DISK_DEFERRED_H
#include "block/libretro-disk.h"
#include "block/libretro-disk-provider.h"

/* Serialized disk owner only. Submit reserves the same bounded fixed slots
 * as ordinary requests. A normal dispatch pass checks ready once per waiting
 * request. ready returns true only when ret and any owned result are published;
 * it must not wait, access storage, touch guest memory or reenter dispatch.
 * abandon drops the provider's consumer interest without waiting, and is called
 * once before the guest completion receives -ECANCELED. Neither callback may
 * borrow a transient stack object. Provider storage outlives its queued work.
 *
 * Provider work is excluded from lr_disk_pending, the frame's device-busy hint.
 * Poll/drain make only ready progress; they never wait for external work. An
 * integrating device must therefore snapshot/replay its unfinished commands
 * explicitly before using this for media I/O. CHD mounting is not enabled yet. */
BlockAIOCB *lr_disk_submit_deferred(BlockBackend *blk,
                                    LRDiskProvider *provider,
                                    BlockCompletionFunc *cb, void *opaque);
#endif
