/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_XBOX_NV2A_PGRAPH_UTIL_H
#define HW_XBOX_NV2A_PGRAPH_UTIL_H

/* Bytes fetched by a non-uniform vertex attribute. There is no trailing
 * stride after the last element; overlapping elements can extend beyond it. */
static inline uint64_t pgraph_vertex_fetch_span(uint32_t count,
                                                uint32_t stride,
                                                size_t element_size)
{
    if (!count || !stride) {
        return 0;
    }
    return (uint64_t)(count - 1) * stride + element_size;
}

static const float f16_max = 511.9375f;
static const float f24_max = 1.0E30;

/* 16 bit to [0.0, F16_MAX = 511.9375] */
static inline 
float convert_f16_to_float(uint16_t f16) {
    if (f16 == 0x0000) { return 0.0; }
    uint32_t i = (f16 << 11) + 0x3C000000;
    return *(float*)&i;
}

/* 24 bit to [0.0, F24_MAX] */
static inline 
float convert_f24_to_float(uint32_t f24) {
    assert(!(f24 >> 24));
    f24 &= 0xFFFFFF;
    if (f24 == 0x000000) { return 0.0; }
    uint32_t i = f24 << 7;
    return *(float*)&i;
}

static inline 
uint8_t cliptobyte(int x)
{
    return (uint8_t)((x < 0) ? 0 : ((x > 255) ? 255 : x));
}

static inline 
void convert_yuy2_to_rgb(const uint8_t *line, unsigned int ix,
                                uint8_t *r, uint8_t *g, uint8_t* b) {
    int c, d, e;
    c = (int)line[ix * 2] - 16;
    if (ix % 2) {
        d = (int)line[ix * 2 - 1] - 128;
        e = (int)line[ix * 2 + 1] - 128;
    } else {
        d = (int)line[ix * 2 + 1] - 128;
        e = (int)line[ix * 2 + 3] - 128;
    }
    *r = cliptobyte((298 * c + 409 * e + 128) >> 8);
    *g = cliptobyte((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = cliptobyte((298 * c + 516 * d + 128) >> 8);
}

static inline 
void convert_uyvy_to_rgb(const uint8_t *line, unsigned int ix,
                                uint8_t *r, uint8_t *g, uint8_t* b) {
    int c, d, e;
    c = (int)line[ix * 2 + 1] - 16;
    if (ix % 2) {
        d = (int)line[ix * 2 - 2] - 128;
        e = (int)line[ix * 2 + 0] - 128;
    } else {
        d = (int)line[ix * 2 + 0] - 128;
        e = (int)line[ix * 2 + 2] - 128;
    }
    *r = cliptobyte((298 * c + 409 * e + 128) >> 8);
    *g = cliptobyte((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = cliptobyte((298 * c + 516 * d + 128) >> 8);
}

/* Write directly into the caller's final RGBA storage. Both packed formats
 * share one U/V sample per pair; preserve the scalar converter's rounding. */
static inline void pgraph_yuv422_store_rgba(uint8_t *out, int y,
                                          int red, int green, int blue)
{
    int c = 298 * (y - 16);
    out[0] = cliptobyte((c + red) >> 8);
    out[1] = cliptobyte((c + green) >> 8);
    out[2] = cliptobyte((c + blue) >> 8);
    out[3] = 255;
}

static inline void pgraph_yuv422_to_rgba(uint8_t *out, const uint8_t *in,
                                       unsigned width, bool uyvy)
{
    unsigned y = uyvy ? 1 : 0;
    unsigned u = uyvy ? 0 : 1;
    while (width) {
        int d = (int)in[u] - 128;
        int e = (int)in[u + 2] - 128;
        int red = 409 * e + 128;
        int green = -100 * d - 208 * e + 128;
        int blue = 516 * d + 128;
        pgraph_yuv422_store_rgba(out, in[y], red, green, blue);
        if (width == 1) {
            /* Odd rows still use the final pair's chroma, without reading
             * its unused second luma or writing a second output pixel. */
            break;
        }
        pgraph_yuv422_store_rgba(out + 4, in[y + 2], red, green, blue);
        in += 4;
        out += 8;
        width -= 2;
    }
}

#endif
