/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_SURFACE_CLEAR_H
#define HW_XBOX_NV2A_PGRAPH_SURFACE_CLEAR_H

#include <stdbool.h>
#include <stdint.h>
#include "hw/xbox/nv2a/nv2a_regs.h"

/* Inclusive guest clear bounds. Only a complete RGBA overwrite makes the
 * previous color contents irrelevant; partial channels/rectangles need them. */
static inline bool pgraph_full_color_clear(uint32_t parameter,
                                           unsigned xmin, unsigned xmax,
                                           unsigned ymin, unsigned ymax,
                                           unsigned width, unsigned height)
{
    return width && height && xmin == 0 && ymin == 0 &&
           xmax >= width - 1 && ymax >= height - 1 &&
           (parameter & NV097_CLEAR_SURFACE_COLOR) == NV097_CLEAR_SURFACE_COLOR;
}

#endif
