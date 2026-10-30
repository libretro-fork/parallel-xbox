/*
 * Bitmap Module
 *
 * Stolen from linux/src/lib/bitmap.c
 *
 * Copyright (C) 2010 Corentin Chary
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/bitmap.h"
#include "qemu/atomic.h"

void bitmap_set(unsigned long *map, long start, long nr)
{
    unsigned long *p = map + BIT_WORD(start);
    const long size = start + nr;
    int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

    assert(start >= 0 && nr >= 0);

    while (nr - bits_to_set >= 0) {
        *p |= mask_to_set;
        nr -= bits_to_set;
        bits_to_set = BITS_PER_LONG;
        mask_to_set = ~0UL;
        p++;
    }
    if (nr) {
        mask_to_set &= BITMAP_LAST_WORD_MASK(size);
        *p |= mask_to_set;
    }
}

void bitmap_set_atomic(unsigned long *map, long start, long nr)
{
    unsigned long *p = map + BIT_WORD(start);
    const long size = start + nr;
    int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

    assert(start >= 0 && nr >= 0);

    /* First word */
    if (nr - bits_to_set > 0) {
        qatomic_or(p, mask_to_set);
        nr -= bits_to_set;
        bits_to_set = BITS_PER_LONG;
        mask_to_set = ~0UL;
        p++;
    }

    /* Full words */
    if (bits_to_set == BITS_PER_LONG) {
        while (nr >= BITS_PER_LONG) {
            *p = ~0UL;
            nr -= BITS_PER_LONG;
            p++;
        }
    }

    /* Last word */
    if (nr) {
        mask_to_set &= BITMAP_LAST_WORD_MASK(size);
        qatomic_or(p, mask_to_set);
    } else {
        /* If we avoided the full barrier in qatomic_or(), issue a
         * barrier to account for the assignments in the while loop.
         */
        smp_mb();
    }
}

void bitmap_clear(unsigned long *map, long start, long nr)
{
    unsigned long *p = map + BIT_WORD(start);
    const long size = start + nr;
    int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);

    assert(start >= 0 && nr >= 0);

    while (nr - bits_to_clear >= 0) {
        *p &= ~mask_to_clear;
        nr -= bits_to_clear;
        bits_to_clear = BITS_PER_LONG;
        mask_to_clear = ~0UL;
        p++;
    }
    if (nr) {
        mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
        *p &= ~mask_to_clear;
    }
}

bool bitmap_test_and_clear(unsigned long *map, long start, long nr)
{
    unsigned long *p = map + BIT_WORD(start);
    const long size = start + nr;
    int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);
    bool dirty = false;

    assert(start >= 0 && nr >= 0);

    /* First word */
    if (nr - bits_to_clear > 0) {
        if ((*p) & mask_to_clear) {
            dirty = true;
        }
        *p &= ~mask_to_clear;
        nr -= bits_to_clear;
        bits_to_clear = BITS_PER_LONG;
        p++;
    }

    /* Full words */
    if (bits_to_clear == BITS_PER_LONG) {
        while (nr >= BITS_PER_LONG) {
            if (*p) {
                dirty = true;
                *p = 0;
            }
            nr -= BITS_PER_LONG;
            p++;
        }
    }

    /* Last word */
    if (nr) {
        mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
        if ((*p) & mask_to_clear) {
            dirty = true;
        }
        *p &= ~mask_to_clear;
    }

    return dirty;
}

bool bitmap_test_and_clear_atomic(unsigned long *map, long start, long nr)
{
    unsigned long *p = map + BIT_WORD(start);
    const long size = start + nr;
    int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);
    unsigned long dirty = 0;
    unsigned long old_bits;

    assert(start >= 0 && nr >= 0);

    /* First word */
    if (nr - bits_to_clear > 0) {
        old_bits = qatomic_fetch_and(p, ~mask_to_clear);
        dirty |= old_bits & mask_to_clear;
        nr -= bits_to_clear;
        bits_to_clear = BITS_PER_LONG;
        mask_to_clear = ~0UL;
        p++;
    }

    /* Full words */
    if (bits_to_clear == BITS_PER_LONG) {
        while (nr >= BITS_PER_LONG) {
            if (*p) {
                old_bits = qatomic_xchg(p, 0);
                dirty |= old_bits;
            }
            nr -= BITS_PER_LONG;
            p++;
        }
    }

    /* Last word */
    if (nr) {
        mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
        old_bits = qatomic_fetch_and(p, ~mask_to_clear);
        dirty |= old_bits & mask_to_clear;
    } else {
        if (!dirty) {
            smp_mb();
        }
    }

    return dirty != 0;
}

void bitmap_copy_and_clear_atomic(unsigned long *dst, unsigned long *src,
                                  long nr)
{
    while (nr > 0) {
        *dst = qatomic_xchg(src, 0);
        dst++;
        src++;
        nr -= BITS_PER_LONG;
    }
}

#define ALIGN_MASK(x,mask)      (((x)+(mask))&~(mask))

long slow_bitmap_count_one(const unsigned long *bitmap, long nbits)
{
    long k, lim = nbits / BITS_PER_LONG, result = 0;

    for (k = 0; k < lim; k++) {
        result += ctpopl(bitmap[k]);
    }

    if (nbits % BITS_PER_LONG) {
        result += ctpopl(bitmap[k] & BITMAP_LAST_WORD_MASK(nbits));
    }

    return result;
}

static void bitmap_to_from_le(unsigned long *dst,
                              const unsigned long *src, long nbits)
{
    long len = BITS_TO_LONGS(nbits);

#if HOST_BIG_ENDIAN
    long index;

    for (index = 0; index < len; index++) {
# if HOST_LONG_BITS == 64
        dst[index] = bswap64(src[index]);
# else
        dst[index] = bswap32(src[index]);
# endif
    }
#else
    memcpy(dst, src, len * sizeof(unsigned long));
#endif
}

void bitmap_to_le(unsigned long *dst, const unsigned long *src,
                  long nbits)
{
    bitmap_to_from_le(dst, src, nbits);
}

