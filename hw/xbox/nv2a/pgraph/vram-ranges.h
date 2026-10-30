/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_VRAM_RANGES_H
#define HW_XBOX_NV2A_PGRAPH_VRAM_RANGES_H

#include <stdbool.h>
#include <stdint.h>

/* Validate without overflowing the exclusive endpoint. */
static inline bool pgraph_vram_range_valid(uint64_t addr, uint64_t size,
                                          uint64_t limit)
{
    return addr <= limit && size <= limit - addr;
}

/* Half-open, non-wrapping byte spans. Subtraction avoids overflowing an
 * endpoint addition, and empty spans never own a byte. */
static inline bool pgraph_vram_ranges_overlap(uint64_t a, uint64_t a_size,
                                              uint64_t b, uint64_t b_size)
{
    if (!a_size || !b_size) {
        return false;
    }
    return a <= b ? b - a < a_size : a - b < b_size;
}

#endif
