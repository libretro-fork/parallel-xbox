/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../../hw/xbox/nv2a/pgraph/scanout.h"
static unsigned rng = 0x128afed;
static unsigned random_u(void) { rng = rng * 1664525u + 1013904223u; return rng; }
int main(void)
{
    unsigned exact = 0, fallback = 0;
    for (unsigned n = 0; n < 12000; n++) {
        unsigned w = 1 + random_u() % 73, h = 1 + random_u() % 55;
        unsigned count = random_u() % 33;
        PGRAPHScanout scan = { .width = w, .height = h };
        PGRAPHScanoutKey keys[32] = { 0 };
        for (unsigned k = 0; k < count; k++) {
            keys[k].rect = (PGRAPHScanoutRect){ 0, 0,
                random_u() % (w + 8), random_u() % (h + 8),
                random_u() % (w + 8), random_u() % (h + 8) };
        }
        if (count && n % 11 == 0) keys[0].rect.width = UINT32_MAX;
        if (count && n % 17 == 0) keys[0].rect.dx = UINT32_MAX;
        PGRAPHScanoutRegions plan;
        pgraph_scanout_background_regions(&plan, &scan, keys, count);
        assert(plan.count <= PGRAPH_SCANOUT_BACKGROUND_RECTS);
        if (plan.fallback) {
            assert(plan.count == 1 && !plan.rects[0].dx && !plan.rects[0].dy);
            assert(plan.rects[0].width == w && plan.rects[0].height == h);
            fallback++;
        } else exact++;
        for (unsigned y = 0; y < h; y++) {
            for (unsigned x = 0; x < w; x++) {
                bool covered = false;
                for (unsigned k = 0; k < count; k++) {
                    PGRAPHScanoutRect *a = &keys[k].rect;
                    covered |= x >= a->dx && y >= a->dy &&
                        (uint64_t)x < (uint64_t)a->dx + a->width &&
                        (uint64_t)y < (uint64_t)a->dy + a->height;
                }
                unsigned draws = 0;
                for (unsigned k = 0; k < plan.count; k++) {
                    PGRAPHScanoutRect *a = &plan.rects[k];
                    assert(a->width && a->height && a->dx + a->width <= w && a->dy + a->height <= h);
                    draws += x >= a->dx && x < a->dx + a->width &&
                             y >= a->dy && y < a->dy + a->height;
                }
                assert(draws == (plan.fallback || !covered));
            }
        }
    }
    PGRAPHScanout scan = { .width = 640, .height = 480 };
    PGRAPHScanoutKey keys[32] = { 0 };
    keys[0].rect = (PGRAPHScanoutRect){ 0, 0, 16, 64, 608, 176 };
    keys[1].rect = (PGRAPHScanoutRect){ 0, 0, 16, 240, 608, 176 };
    PGRAPHScanoutRegions plan;
    pgraph_scanout_background_regions(&plan, &scan, keys, 2);
    assert(!plan.fallback && plan.count == 4);
    uint64_t pixels = 0;
    for (unsigned i = 0; i < plan.count; i++) pixels += (uint64_t)plan.rects[i].width * plan.rects[i].height;
    assert(pixels == 93184);
    assert(pgraph_scanout_regions_worthwhile(&plan, 640, 480, 1));
    pgraph_scanout_background_regions(&plan, &scan, &keys[1], 1);
    assert(!pgraph_scanout_regions_worthwhile(&plan, 640, 480, 1));
    assert(pgraph_scanout_regions_worthwhile(&plan, 640, 480, 2));
    for (unsigned i = 0; i < 32; i++) keys[i].rect = (PGRAPHScanoutRect){0,0,16,64+i*8,608,8};
    pgraph_scanout_background_regions(&plan, &scan, keys, 32);
    assert(!plan.fallback && plan.count == 4); /* adjacent strips coalesce */
    keys[0].rect = (PGRAPHScanoutRect){0,0,0,0,640,480};
    pgraph_scanout_background_regions(&plan, &scan, keys, 1);
    assert(!plan.fallback && !plan.count);
    pgraph_scanout_background_regions(&plan, &scan, NULL, 0);
    assert(!plan.fallback && plan.count == 1);
    for (unsigned i = 0; i < 32; i++) keys[i].rect = (PGRAPHScanoutRect){0,0,8+(i%8)*64,8+(i/8)*64,8,8};
    pgraph_scanout_background_regions(&plan, &scan, keys, 32);
    assert(plan.fallback && plan.count == 1);
    printf("scanout regions: 12000 raster-reference cases passed (%u exact, %u bounded fallbacks)\n",exact,fallback);
    return 0;
}
