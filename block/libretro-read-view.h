/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLOCK_LIBRETRO_READ_VIEW_H
#define BLOCK_LIBRETRO_READ_VIEW_H

/* First member of an owner-serialized retained read owner. */
typedef struct LRReadViewOwner {
    void (*release)(void *owner);
} LRReadViewOwner;

#endif
