/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent normalized BC1/2/3 oracle: interpolate before converting to
 * RGBA8, as Microsoft's DirectXTex decoder does. No intermediate byte palette.
 * Reference: microsoft/DirectXTex, BC.cpp, ec66b78b37389e8373a8e14ccc3cd29be94146b3.
 * DIRECTXTEX_ORACLE substitutes the actual vendor decoder for cross-checking.
 */
#include "qemu/osdep.h"
#include <math.h>
#include "hw/xbox/nv2a/pgraph/s3tc.h"

#ifdef DIRECTXTEX_ORACLE
extern void directx_oracle(unsigned format, const uint8_t *block, float *out);
#endif

static unsigned long checks, mismatches;

static void check(unsigned format, const uint8_t block[16])
{
    uint8_t actual[64], volume[64];
    s3tc_decompress_2d_into(format, block, 4, 4, actual);
    s3tc_decompress_3d_into(format, block, 4, 4, 1, volume);
    assert(memcmp(actual, volume, sizeof(actual)) == 0);
#ifdef DIRECTXTEX_ORACLE
    float reference[64];
    directx_oracle(format, block, reference);
#else
    double reference[64];
    const uint8_t *colors = block + (format ? 8 : 0);
    unsigned c0 = colors[0] | (colors[1] << 8);
    unsigned c1 = colors[2] | (colors[3] << 8);
    uint32_t indices;
    uint64_t alpha_bits;
    memcpy(&indices, colors + 4, sizeof(indices));
    memcpy(&alpha_bits, block, sizeof(alpha_bits));
    for (unsigned p = 0; p < 16; p++) {
        unsigned idx = (indices >> (2*p)) & 3;
        bool transparent = !format && c0 <= c1;
        for (unsigned ch = 0; ch < 3; ch++) {
            unsigned shift = ch == 0 ? 11 : ch == 1 ? 5 : 0;
            unsigned mask = ch == 1 ? 63 : 31;
            double a = ((c0 >> shift) & mask) / (double)mask;
            double b = ((c1 >> shift) & mask) / (double)mask;
            double v;
            if (idx == 0) v = a;
            else if (idx == 1) v = b;
            else if (transparent) v = idx == 2 ? (a+b)/2 : 0;
            else v = ((4-idx)*a + (idx-1)*b)/3;
            reference[4*p+ch] = v;
        }
        double alpha = transparent && idx == 3 ? 0 : 1;
        if (format == S3TC_DECOMPRESS_FORMAT_DXT3) {
            alpha = ((alpha_bits >> (4*p)) & 15)/15.0;
        } else if (format == S3TC_DECOMPRESS_FORMAT_DXT5) {
            unsigned ai = (alpha_bits >> (16+3*p)) & 7;
            double a = block[0]/255.0, b = block[1]/255.0;
            if (ai == 0) alpha = a;
            else if (ai == 1) alpha = b;
            else if (a > b) alpha = ((8-ai)*a + (ai-1)*b)/7;
            else if (ai < 6) alpha = ((6-ai)*a + (ai-1)*b)/5;
            else alpha = ai == 6 ? 0 : 1;
        }
        reference[4*p+3] = alpha;
    }
#endif
    for (unsigned i = 0; i < 64; i++) {
        /* Stabilize exact half-byte ties against float oracle roundoff. */
        unsigned expected = (unsigned)floor(reference[i]*255.0 + 0.5001);
        checks++;
        if (actual[i] != expected) {
            if (mismatches < 4) {
                printf("format=%u component=%u actual=%u oracle=%u\n",
                       format, i, actual[i], expected);
            }
            mismatches++;
        }
    }
}

int main(void)
{
    uint8_t block[16];
    /* All endpoint pairs per component and all four color selectors. DXT1
     * covers both palette modes; BC2/3 must keep four colors in either order. */
    for (unsigned format = 0; format < 3; format++)
    for (unsigned ch = 0; ch < 3; ch++) {
        unsigned shift = ch == 0 ? 11 : ch == 1 ? 5 : 0;
        unsigned max = ch == 1 ? 63 : 31;
        for (unsigned a = 0; a <= max; a++)
        for (unsigned b = 0; b <= max; b++) {
            memset(block, 0, sizeof(block));
            if (format == S3TC_DECOMPRESS_FORMAT_DXT3) memset(block, 255, 8);
            if (format == S3TC_DECOMPRESS_FORMAT_DXT5) block[0] = 255;
            uint8_t *color = block + (format ? 8 : 0);
            unsigned c0 = a << shift, c1 = b << shift;
            color[0] = c0; color[1] = c0 >> 8;
            color[2] = c1; color[3] = c1 >> 8;
            memset(color+4, 0xe4, 4);
            check(format, block);
        }
    }
    /* Every explicit alpha nibble, and every adaptive endpoint pair with
     * all eight selectors, including equal endpoints and special 0/1 values. */
    memset(block, 0, sizeof(block));
    for (unsigned p = 0; p < 8; p++) block[p] = 2*p | ((2*p+1) << 4);
    check(S3TC_DECOMPRESS_FORMAT_DXT3, block);
    for (unsigned a = 0; a < 256; a++)
    for (unsigned b = 0; b < 256; b++) {
        uint64_t alpha = a | (b << 8);
        for (unsigned p = 0; p < 16; p++) alpha |= (uint64_t)(p%8) << (16+3*p);
        memcpy(block, &alpha, 8);
        check(S3TC_DECOMPRESS_FORMAT_DXT5, block);
    }
    printf("S3TC oracle: %lu components, %lu mismatches\n", checks, mismatches);
    return mismatches ? 1 : 0;
}
