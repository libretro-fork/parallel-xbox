/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#define MAX(a,b) ((a) > (b) ? (a) : (b))
typedef struct { int unused; } PGRAPHState;
/* PRODUCTION */

/* Enumerate each mip's rows/blocks independently, using base-dimension
 * shifts rather than the production walker's incremental dimensions. */
static size_t reference(TextureShape s, unsigned bpp, unsigned block)
{
    if (s.pitch) return (size_t)s.height * s.pitch;
    unsigned w0 = s.width, h0 = s.height, d0 = s.dimensionality == 3 ? s.depth : 1;
    if (s.border) {
        w0 = w0 * 2 < 16 ? 16 : w0 * 2;
        h0 = h0 * 2 < 16 ? 16 : h0 * 2;
        if (s.dimensionality == 3) d0 = d0 * 2 < 16 ? 16 : d0 * 2;
    }
    size_t bytes = 0;
    for (unsigned level = 0; level < s.levels; level++) {
        unsigned w = w0 >> level, h = h0 >> level, d = d0 >> level;
        if (!w) w = 1;
        if (!h) h = 1;
        if (!d) d = 1;
        for (unsigned z = 0; z < d; z++) {
            if (block) {
                for (unsigned y = 0; y < h; y += 4)
                for (unsigned x = 0; x < w; x += 4) bytes += block;
            } else {
                for (unsigned y = 0; y < h; y++) bytes += (size_t)w * bpp;
            }
        }
    }
    if (s.cubemap) {
        while (bytes % 128) bytes++;
        bytes *= 6;
    }
    return bytes;
}
static uint32_t random_state = 0x9817ec3a;
static unsigned random_value(void)
{
    random_state ^= random_state << 13; random_state ^= random_state >> 17;
    random_state ^= random_state << 5; return random_state;
}
int main(void)
{
    PGRAPHState pg = {0};
    TextureShape s = { .cubemap = true, .dimensionality = 2,
        .color_format = NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5,
        .width = 1, .height = 1, .depth = 1, .levels = 1 };
    assert(pgraph_get_cubemap_face_length(&pg, s) == 128);
    assert(pgraph_get_texture_length(&pg, &s) == 768);
    s.width = s.height = 2; s.levels = 2;
    s.color_format = NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8;
    assert(pgraph_get_cubemap_face_length(&pg, s) == 128);
    assert(pgraph_get_texture_length(&pg, &s) == 768);
    s.width = s.height = 4; s.levels = 1; s.border = true;
    s.color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8;
    assert(pgraph_get_cubemap_face_length(&pg, s) == 1024);
    assert(pgraph_get_texture_length(&pg, &s) == 6144);
    s.cubemap = false; s.dimensionality = 3; s.border = false;
    s.width = s.height = s.depth = 8; s.levels = 4;
    assert(pgraph_get_texture_length(&pg, &s) == 2340);
    s.border = true; s.width = s.height = s.depth = 4; s.levels = 1;
    assert(pgraph_get_texture_length(&pg, &s) == 16384);
    s.dimensionality = 2; s.depth = 1;
    s.color_format = NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5;
    assert(pgraph_get_texture_length(&pg, &s) == 128);
    s.color_format = NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8;
    s.width = 3; s.height = 4; s.pitch = 5;
    assert(pgraph_get_texture_length(&pg, &s) == 20);
    const struct { unsigned format, bpp, block; } formats[] = {
        { NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5, 0, 8 },
        { NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8, 0, 16 },
        { NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8, 0, 16 },
        { NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8, 1, 0 },
        { NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8, 1, 0 },
        { NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5, 2, 0 },
        { NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5, 2, 0 },
        { NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8, 4, 0 },
    };
    for (unsigned test = 0; test < 5000; test++) {
        unsigned format = random_value() % (sizeof(formats) / sizeof(formats[0]));
        s = (TextureShape){ .cubemap = random_value() % 3 == 0,
            .dimensionality = 2 + random_value() % 2,
            .color_format = formats[format].format, .levels = 1 + random_value() % 9,
            .width = 1u << (random_value() % 8), .height = 1u << (random_value() % 8),
            .depth = 1u << (random_value() % 6), .border = random_value() & 1 };
        if (s.cubemap) { s.dimensionality = 2; s.height = s.width; }
        size_t expected = reference(s, formats[format].bpp, formats[format].block);
        assert(pgraph_get_texture_length(&pg, &s) == expected);
        if (s.cubemap) {
            size_t face = pgraph_get_cubemap_face_length(&pg, s);
            assert(face && face % 128 == 0 && face * 6 == expected);
        }
        /* Sampling clamps, including Force Texture LOD0, never change
         * guest mip-chain offsets, face strides or dirty-tracking spans. */
        s.min_mipmap_level = s.max_mipmap_level = 0;
        assert(pgraph_get_texture_length(&pg, &s) == expected);
        s.min_mipmap_level = s.max_mipmap_level = s.levels - 1;
        assert(pgraph_get_texture_length(&pg, &s) == expected);
    }
    puts("texture layout: fixed edge cases and 5000 independent mip walks passed; LOD clamps preserve offsets");
    return 0;
}
