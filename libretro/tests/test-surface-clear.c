/* SPDX-License-Identifier: GPL-2.0-or-later
 * A per-pixel/channel overwrite oracle for the full-clear upload bypass.
 */
#include <assert.h>
#include <stdio.h>
#include "hw/xbox/nv2a/pgraph/surface-clear.h"

static uint32_t rng = 0x7de419a1;
static unsigned next(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

int main(void)
{
    unsigned accepted = 0;
    for (unsigned n = 0; n < 200000; n++) {
        unsigned w = next() % 9, h = next() % 9;
        unsigned x0 = next() % 4, x1 = x0 + next() % 12;
        unsigned y0 = next() % 4, y1 = y0 + next() % 12;
        uint32_t mask = next() % 256;
        /* Include complete-channel clears often enough to exercise the gate. */
        if (n % 3 == 0) mask |= NV097_CLEAR_SURFACE_COLOR;
        bool all_overwritten = w && h;
        for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++)
        for (unsigned channel = 0; channel < 4; channel++) {
            unsigned before = 17 + channel;
            unsigned after = before;
            if (x >= x0 && x <= x1 && y >= y0 && y <= y1 &&
                (mask & (NV097_CLEAR_SURFACE_R << channel))) {
                after = 201 + channel;
            }
            if (after == before) all_overwritten = false;
        }
        bool discard = pgraph_full_color_clear(mask, x0, x1, y0, y1, w, h);
        assert(discard == all_overwritten);
        accepted += discard;
    }
    unsigned zeta_accepted = 0;
    for (unsigned n = 0; n < 200000; n++) {
        unsigned w = next() % 9, h = next() % 9;
        unsigned x0 = next() % 4, x1 = x0 + next() % 12;
        unsigned y0 = next() % 4, y1 = y0 + next() % 12;
        bool stencil = next() & 1;
        uint32_t mask = next() % 256;
        bool overwritten = w && h;
        for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++)
        for (unsigned aspect = 0; aspect < (stencil ? 2u : 1u); aspect++) {
            unsigned flag = aspect ? NV097_CLEAR_SURFACE_STENCIL : NV097_CLEAR_SURFACE_Z;
            if (!(x >= x0 && x <= x1 && y >= y0 && y <= y1 && (mask & flag)))
                overwritten = false;
        }
        bool discard = pgraph_full_zeta_clear(mask, stencil, x0, x1, y0, y1, w, h);
        assert(discard == overwritten);
        zeta_accepted += discard;
    }
    assert(pgraph_full_zeta_clear(NV097_CLEAR_SURFACE_Z, false, 0, 639, 0, 479, 640, 480));
    assert(!pgraph_full_zeta_clear(NV097_CLEAR_SURFACE_Z, true, 0, 639, 0, 479, 640, 480));
    assert(!pgraph_full_zeta_clear(NV097_CLEAR_SURFACE_STENCIL, true, 0, 639, 0, 479, 640, 480));
    assert(pgraph_full_zeta_clear(NV097_CLEAR_SURFACE_Z | NV097_CLEAR_SURFACE_STENCIL,
                                  true, 0, 639, 0, 479, 640, 480));
    printf("depth/stencil clear upload gate: 200000 pixel/aspect oracle cases passed (%u complete)\n", zeta_accepted);
    /* Guard against inclusive-bound off-by-one and invalid zero dimensions. */
    assert(pgraph_full_color_clear(NV097_CLEAR_SURFACE_COLOR, 0, 639, 0, 479, 640, 480));
    assert(!pgraph_full_color_clear(NV097_CLEAR_SURFACE_COLOR, 0, 638, 0, 479, 640, 480));
    assert(!pgraph_full_color_clear(NV097_CLEAR_SURFACE_COLOR, 0, 639, 0, 478, 640, 480));
    assert(!pgraph_full_color_clear(NV097_CLEAR_SURFACE_COLOR, 0, 4095, 0, 4095, 0, 480));
    assert(!pgraph_full_color_clear(NV097_CLEAR_SURFACE_COLOR & ~NV097_CLEAR_SURFACE_A,
                                  0, 639, 0, 479, 640, 480));
    printf("full-clear upload gate: 200000 pixel/channel oracle cases passed (%u complete)\n", accepted);
    return 0;
}
