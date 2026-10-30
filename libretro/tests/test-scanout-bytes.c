/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../../hw/xbox/nv2a/pgraph/scanout.h"
static unsigned seed = 0x9813ed;
static unsigned rnd(void) { seed = seed * 1664525u + 1013904223u; return seed; }
int main(void)
{
    for (unsigned n = 0; n < 6000; n++) {
        unsigned bpp = ((rnd() >> 16) & 1) ? 2 : 4;
        unsigned w = 1 + rnd() % 65, h = 1 + rnd() % 43;
        unsigned pitch = (w + rnd() % 13) * bpp;
        PGRAPHScanout scan = {4096, (uint64_t)pitch*h, w,h,pitch,bpp};
        uint64_t addr = (2048 + rnd() % (pitch*(h+4))) / bpp * bpp;
        uint64_t size = (1 + rnd() % (pitch*(h+4))) / bpp * bpp;
        PGRAPHScanoutRect rects[3];
        unsigned count = pgraph_scanout_byte_rects(&scan, addr, size, rects);
        assert(count <= 3);
        for (unsigned y = 0; y < h; y++) for (unsigned x = 0; x < w; x++) {
            uint64_t byte = scan.start + y*pitch + x*bpp;
            unsigned draws = 0;
            for (unsigned i = 0; i < count; i++) {
                PGRAPHScanoutRect *p = &rects[i];
                assert(p->width && p->height && p->dx+p->width <= w && p->dy+p->height <= h);
                draws += x >= p->dx && x < p->dx+p->width && y >= p->dy && y < p->dy+p->height;
            }
            assert(draws == (byte >= addr && byte < addr+size));
        }
    }
    PGRAPHScanout scan = {4096,4096,32,32,128,4};
    PGRAPHScanoutRect rects[3];
    assert(!pgraph_scanout_byte_rects(&scan, UINT64_MAX-3, 16, rects));
    assert(!pgraph_scanout_byte_rects(&scan, 4097,16,rects));
    assert(!pgraph_scanout_byte_rects(&scan, 4096,15,rects));
    PGRAPHScanoutCache cache = {0};
    PGRAPHScanoutKey key = { .rect = {0,0,0,0,16,16} };
    pgraph_scanout_cache_update(&cache, &scan, &key, 1, false);
    uint64_t geometry = cache.geometry;
    key.holes = true;
    pgraph_scanout_cache_update(&cache, &scan, &key, 1, false);
    assert(cache.geometry == geometry + 1);
    puts("scanout bytes: 6000 per-pixel packed-span reference cases passed");
    return 0;
}
