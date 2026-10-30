/*
 * The libretro core's disks (block/libretro-disk.c), in place of QEMU's
 * block layer: the Xbox's hard disk and DVD drive, and the Chihiro media
 * board's interface. The devices (hw/ide, hw/block) reach them through
 * the BlockBackend calls of system/block-backend-*.h; the core creates and
 * changes them with these.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BLOCK_LIBRETRO_DISK_H
#define BLOCK_LIBRETRO_DISK_H

#include "system/blockdev.h"
#include "system/block-backend.h"

/* Requests done, their completions not yet run (a bottom half each): the
 * frame loop waits for them as for the GPU (ui/libretro.c). The emulator
 * thread only. */
extern int lr_disk_pending;

/* A drive, empty: bus and unit as -drive gives them (ide0-hd0, ide0-cd1).
 * locked: the hard disk's ATA lock (hw/block's blkconf_locked). */
BlockBackend *lr_disk_new(const char *name, BlockInterfaceType type,
                          int bus, int unit, bool cdrom, bool locked);

/* The image at path in the drive, from offset (a Redump image's game
 * partition) to its end; on the host's own file calls, or through the
 * frontend's VFS for a path only it can open (saf://, content://). The
 * tray is not involved: for a drive the devices have not seen yet. */
bool lr_disk_open(BlockBackend *blk, const char *path, uint64_t offset,
                  bool writable, Error **errp);

/* size bytes of an address space as the medium (the Chihiro's interface) */
void lr_disk_open_memory(BlockBackend *blk, AddressSpace *as, uint64_t size);

/* Another image into a drive the guest has: its tray opens, the medium
 * goes, the new one goes in and the tray closes, as the guest's drive sees
 * a disc changed by hand (QEMU's blockdev-change-medium). An empty path
 * leaves the tray open and the drive empty (eject). */
bool lr_disk_change(BlockBackend *blk, const char *path, uint64_t offset,
                    Error **errp);

/* The range reads zeroes, freed from the image where its file system can
 * (a hole); the hard disk's cache partitions. 0 or -errno. */
int lr_disk_zero(BlockBackend *blk, int64_t offset, int64_t bytes);

/* every writable image's data to the host's disk (the guest's flushes do
 * not wait for that: block/libretro-disk.c) */
void lr_disk_sync_all(void);

/* the space the image takes on the host's disk, -1 when not known */
int64_t lr_disk_allocated(BlockBackend *blk);

#endif
