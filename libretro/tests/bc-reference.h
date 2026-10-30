/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LIBRETRO_TEST_BC_REFERENCE_H
#define LIBRETRO_TEST_BC_REFERENCE_H
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
/* Independent normalized-color oracle; preserve interpolation precision
 * until the final RGBA8 quantization, as the decoder's DirectXTex oracle. */
static void reference_pixel(const uint8_t *block, unsigned mode, unsigned p,
                            uint8_t *pixel)
{
    const uint8_t *color = block + (mode ? 8 : 0);
    unsigned a = color[0] | color[1] << 8, b = color[2] | color[3] << 8;
    unsigned index = (color[4 + p / 4] >> (2 * (p % 4))) & 3;
    bool transparent = !mode && a <= b;
    for (unsigned ch = 0; ch < 3; ch++) {
        unsigned shift = ch == 0 ? 11 : ch == 1 ? 5 : 0;
        unsigned mask = ch == 1 ? 63 : 31;
        double c0 = ((a >> shift) & mask) / (double)mask;
        double c1 = ((b >> shift) & mask) / (double)mask;
        double value = index == 0 ? c0 : index == 1 ? c1 :
            transparent ? (index == 2 ? (c0 + c1) / 2 : 0) :
            ((4 - index) * c0 + (index - 1) * c1) / 3;
        pixel[ch] = (uint8_t)floor(value * 255 + 0.5001);
    }
    double alpha = transparent && index == 3 ? 0 : 1;
    if (mode == 1) {
        alpha = ((block[p / 2] >> (4 * (p % 2))) & 15) / 15.0;
    } else if (mode == 2) {
        uint64_t selectors = 0;
        for (unsigned i = 0; i < 6; i++) selectors |= (uint64_t)block[2+i] << (8*i);
        unsigned ai = (selectors >> (3*p)) & 7;
        double c0 = block[0] / 255.0, c1 = block[1] / 255.0;
        alpha = ai == 0 ? c0 : ai == 1 ? c1 : c0 > c1 ?
            ((8-ai)*c0 + (ai-1)*c1)/7 : ai < 6 ?
            ((6-ai)*c0 + (ai-1)*c1)/5 : ai == 6 ? 0 : 1;
    }
    pixel[3] = (uint8_t)floor(alpha * 255 + 0.5001);
}

#endif
