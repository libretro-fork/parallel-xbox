/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_COPY_H
#define HW_XBOX_NV2A_PGRAPH_COPY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The caller validates the accessible ranges. Preserve forward row order for
 * aliasing copies: one large memmove can change which earlier writes a later
 * row reads. Disjoint, tightly packed rows have no such dependency. */
static inline void pgraph_copy_rows(uint8_t *dest, const uint8_t *source,
                                    size_t width_bytes, size_t height,
                                    size_t source_pitch, size_t dest_pitch)
{
    if (!width_bytes || !height) {
        return;
    }
    if (source_pitch == width_bytes && dest_pitch == width_bytes &&
        height <= SIZE_MAX / width_bytes) {
        size_t bytes = width_bytes * height;
        uintptr_t src = (uintptr_t)source;
        uintptr_t dst = (uintptr_t)dest;
        uintptr_t distance = src < dst ? dst - src : src - dst;
        if (distance >= bytes) {
            memmove(dest, source, bytes);
            return;
        }
    }
    for (size_t y = 0; y < height; y++) {
        memmove(dest, source, width_bytes);
        /* Do not form a pointer beyond the last row's valid range. */
        if (y + 1 < height) {
            source += source_pitch;
            dest += dest_pitch;
        }
    }
}
#endif
