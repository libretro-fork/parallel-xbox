/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_BLEND_H
#define HW_XBOX_NV2A_PGRAPH_BLEND_H

#include <stdint.h>
#include "hw/xbox/nv2a/nv2a_regs.h"

#define PGRAPH_BLEND_STATE_MASK (NV_PGRAPH_BLEND_EQN | NV_PGRAPH_BLEND_EN | \
                                 NV_PGRAPH_BLEND_SFACTOR | NV_PGRAPH_BLEND_DFACTOR)

/* The Xbox uses one for destination alpha on these targets, irrespective
 * of the backing image's alpha bits. Canonicalize the register before both
 * cache comparisons and host state emission; no image conversion is needed. */
static inline uint32_t pgraph_surface_blend(uint32_t blend, unsigned color_format)
{
    if (!(blend & NV_PGRAPH_BLEND_EN)) {
        return blend;
    }
    switch (color_format) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_O1R5G5B5:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_O8R8G8B8:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_Z1A7R8G8B8:
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_X1A7R8G8B8_O1A7R8G8B8:
        if ((blend & NV_PGRAPH_BLEND_SFACTOR) ==
            (NV_PGRAPH_BLEND_SFACTOR_DST_ALPHA << 4)) {
            blend = (blend & ~NV_PGRAPH_BLEND_SFACTOR) |
                    (NV_PGRAPH_BLEND_SFACTOR_ONE << 4);
        } else if ((blend & NV_PGRAPH_BLEND_SFACTOR) ==
                   (NV_PGRAPH_BLEND_SFACTOR_ONE_MINUS_DST_ALPHA << 4)) {
            blend &= ~NV_PGRAPH_BLEND_SFACTOR;
        }
        if ((blend & NV_PGRAPH_BLEND_DFACTOR) ==
            (NV_PGRAPH_BLEND_DFACTOR_DST_ALPHA << 8)) {
            blend = (blend & ~NV_PGRAPH_BLEND_DFACTOR) |
                    (NV_PGRAPH_BLEND_DFACTOR_ONE << 8);
        } else if ((blend & NV_PGRAPH_BLEND_DFACTOR) ==
                   (NV_PGRAPH_BLEND_DFACTOR_ONE_MINUS_DST_ALPHA << 8)) {
            blend &= ~NV_PGRAPH_BLEND_DFACTOR;
        }
        break;
    default:
        break;
    }
    return blend;
}

#endif
