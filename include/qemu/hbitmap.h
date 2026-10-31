/*
 * Hierarchical Bitmap Data Type
 *
 * Copyright Red Hat, Inc., 2012
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef HBITMAP_H
#define HBITMAP_H

#include "bitops.h"
#include "host-utils.h"

typedef struct HBitmap HBitmap;
typedef struct HBitmapIter HBitmapIter;

#define BITS_PER_LEVEL         (BITS_PER_LONG == 32 ? 5 : 6)

/* For 32-bit, the largest that fits in a 4 GiB address space.
 * For 64-bit, the number of sectors in 1 PiB.  Good luck, in
 * either case... :)
 */
#define HBITMAP_LOG_MAX_SIZE   (BITS_PER_LONG == 32 ? 34 : 41)

/* We need to place a sentinel in level 0 to speed up iteration.  Thus,
 * we do this instead of HBITMAP_LOG_MAX_SIZE / BITS_PER_LEVEL.  The
 * difference is that it allocates an extra level when HBITMAP_LOG_MAX_SIZE
 * is an exact multiple of BITS_PER_LEVEL.
 */
#define HBITMAP_LEVELS         ((HBITMAP_LOG_MAX_SIZE / BITS_PER_LEVEL) + 1)

struct HBitmapIter {
    const HBitmap *hb;

    /* Copied from hb for access in the inline functions (hb is opaque).  */
    int granularity;

    /* Entry offset into the last-level array of longs.  */
    size_t pos;

    /* The currently-active path in the tree.  Each item of cur[i] stores
     * the bits (i.e. the subtrees) yet to be processed under that node.
     */
    unsigned long cur[HBITMAP_LEVELS];
};












#endif
