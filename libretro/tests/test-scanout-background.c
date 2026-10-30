/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../../hw/xbox/nv2a/pgraph/scanout.h"

static unsigned random_state = 0x23afe5;
static unsigned next_random(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

int main(void)
{
    static PGRAPHScanoutBackground bg;
    unsigned tested = 0;
    for (unsigned n = 0; n < 2500; n++) {
        unsigned w = next_random() % 127 + 1, h = next_random() % 95 + 1;
        unsigned bpp = n & 1 ? 2 : 4;
        unsigned pitch = ((w * bpp + 7) & ~7u) + (n % 3) * 8;
        PGRAPHScanout scan;
        assert(pgraph_scanout_init(&scan, 0x1002, w, h, pitch, bpp == 2 ? 15 : 32,
                                   1 << 24) == (bpp == 2));
        assert(pgraph_scanout_init(&scan, 0x2000, w, h, pitch, bpp * 8, 1 << 24));
        assert(pgraph_scanout_background_init(&bg, &scan));
        PGRAPHScanoutRect rect;
        unsigned cursor = 0, pixels = 0;
        while (pgraph_scanout_background_next(&bg, &cursor, &rect)) {
            pixels += rect.width * rect.height;
            pgraph_scanout_background_clean(&bg, &rect);
        }
        assert(pixels == w * h);
        uint64_t offset = next_random() % (scan.size + 16);
        uint64_t length = n % 13 == 0 ? UINT64_MAX : next_random() % (scan.size + 16);
        pgraph_scanout_background_dirty(&bg, &scan, offset, length);
        bool pending[64] = { 0 }, covered[64] = { 0 }, consumed[64] = { 0 };
        uint64_t end = offset >= scan.size ? offset :
                       offset + (length > scan.size - offset ? scan.size - offset : length);
        for (unsigned y = 0; y < h; y++) {
            for (unsigned x = 0; x < w; x++) {
                uint64_t a = (uint64_t)y * pitch + x * bpp;
                if (length && a < end && a + bpp > offset) {
                    pending[(y / 16) * bg.cols + x / 16] = true;
                }
            }
        }
        PGRAPHScanoutKey keys[3] = { 0 };
        for (unsigned k = 0; k < 3; k++) {
            unsigned x0 = next_random() % w, y0 = next_random() % h;
            keys[k].rect = (PGRAPHScanoutRect){ 0, 0, x0, y0,
                1 + next_random() % (w - x0), 1 + next_random() % (h - y0) };
        }
        pgraph_scanout_background_cover(&bg, keys, 3);
        for (unsigned i = 0; i < bg.cols * bg.rows; i++) {
            unsigned x0 = (i % bg.cols) * 16, y0 = (i / bg.cols) * 16;
            unsigned x1 = x0 + 16 < w ? x0 + 16 : w;
            unsigned y1 = y0 + 16 < h ? y0 + 16 : h;
            for (unsigned k = 0; k < 3; k++) {
                PGRAPHScanoutRect *r = &keys[k].rect;
                covered[i] |= x0 >= r->dx && y0 >= r->dy &&
                              x1 <= r->dx + r->width && y1 <= r->dy + r->height;
            }
            assert(!!((bg.pending[i / 64] >> (i % 64)) & 1) == pending[i]);
            assert(pgraph_scanout_background_visible(&bg, i) == (pending[i] && !covered[i]));
        }
        cursor = 0;
        while (pgraph_scanout_background_next(&bg, &cursor, &rect)) {
            assert(rect.dx + rect.width <= w && rect.dy + rect.height <= h);
            for (unsigned x = rect.dx; x < rect.dx + rect.width; x += 16) {
                unsigned i = (rect.dy / 16) * bg.cols + x / 16;
                assert(pending[i] && !covered[i] && !consumed[i]);
                consumed[i] = true;
            }
            pgraph_scanout_background_clean(&bg, &rect);
        }
        for (unsigned i = 0; i < bg.cols * bg.rows; i++) {
            assert(!!((bg.pending[i / 64] >> (i % 64)) & 1) == (pending[i] && covered[i]));
        }
        /* Expose deferred cells, without another CPU write. */
        pgraph_scanout_background_cover(&bg, NULL, 0);
        cursor = 0;
        while (pgraph_scanout_background_next(&bg, &cursor, &rect)) {
            for (unsigned x = rect.dx; x < rect.dx + rect.width; x += 16) {
                unsigned i = (rect.dy / 16) * bg.cols + x / 16;
                assert(pending[i] && covered[i] && !consumed[i]);
                consumed[i] = true;
            }
            pgraph_scanout_background_clean(&bg, &rect);
        }
        for (unsigned i = 0; i < bg.cols * bg.rows; i++) assert(consumed[i] == pending[i]);
        tested++;
    }
    PGRAPHScanout oversized = { .width = 8193, .height = 1 };
    assert(!pgraph_scanout_background_init(&bg, &oversized));
    puts("scanout background: 2500 independent dirty-byte/coverage/exposure cases passed");
    return tested != 2500;
}
