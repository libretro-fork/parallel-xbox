/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../../hw/xbox/nv2a/pgraph/scanout.h"

int main(void)
{
    PGRAPHScanout s;
    PGRAPHScanoutRect r;
    unsigned tested = 0;
    for (unsigned bpp = 2; bpp <= 4; bpp += 2) {
        assert(pgraph_scanout_init(&s, 4096, 8, 8, 10 * bpp,
                                   bpp * 8, 8192));
        assert(!pgraph_scanout_overlaps(&s, s.start - 1, 1));
        assert(pgraph_scanout_overlaps(&s, s.start - 1, 2));
        assert(pgraph_scanout_overlaps(&s, s.start + s.size - 1, 1));
        assert(!pgraph_scanout_overlaps(&s, s.start + s.size, 1));
        for (int y = -8; y < 10; y++) {
            for (unsigned x = 0; x < 10; x++) {
                for (unsigned w = 1; w <= 10 - x; w++) {
                    for (unsigned h = 1; h <= 10; h++) {
                        uint64_t addr = s.start + y * (int64_t)s.pitch + x * bpp;
                        if (!pgraph_scanout_rect(&s, addr, w, h, s.pitch,
                                                 bpp, &r)) {
                            assert(x >= s.width || y >= (int)s.height ||
                                   y + (int)h <= 0);
                            continue;
                        }
                        unsigned hits = 0;
                        /* Independent reference: intersect each stored pixel
                         * by its byte address, not by rectangle arithmetic. */
                        for (unsigned sy = 0; sy < h; sy++) {
                            for (unsigned sx = 0; sx < w; sx++) {
                                uint64_t a = addr + sy * s.pitch + sx * bpp;
                                for (unsigned dy = 0; dy < s.height; dy++) {
                                    for (unsigned dx = 0; dx < s.width; dx++) {
                                        if (a == s.start + dy * s.pitch + dx * bpp) {
                                            assert(sx >= r.sx && sy >= r.sy);
                                            assert(sx - r.sx < r.width && sy - r.sy < r.height);
                                            assert(dx == r.dx + sx - r.sx);
                                            assert(dy == r.dy + sy - r.sy);
                                            hits++;
                                        }
                                    }
                                }
                            }
                        }
                        assert(hits == r.width * r.height);
                        tested++;
                    }
                }
            }
        }
        assert(!pgraph_scanout_rect(&s, s.start, 8, 8, s.pitch + bpp, bpp, &r));
        assert(!pgraph_scanout_rect(&s, s.start + 1, 8, 8, s.pitch, bpp, &r));
    }
    assert(!pgraph_scanout_init(&s, 0, 640, 480, 0, 32, 1 << 24));
    assert(!pgraph_scanout_init(&s, UINT64_MAX - 8, 640, 480, 2560, 32, UINT64_MAX));
    assert(pgraph_scanout_init(&s, 0, 720, 480, 2560, 32, 1 << 24));
    assert(s.width == 640);
    printf("scanout: %u clipped mappings passed\n", tested);
    return 0;
}
