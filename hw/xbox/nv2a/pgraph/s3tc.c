/*
 * S3TC Texture Decompression
 *
 * Copyright (c) 2020 Wilhelm Kovatch
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "s3tc.h"

/* Interpolate the normalized 5/6-bit endpoints before rounding to RGBA8.
 * Expanding endpoints to bytes first adds a second quantization step and
 * downward bias. Integer ratios preserve precision without host FP variance. */
static void decode_bc1_channel(unsigned c0, unsigned c1, unsigned max,
                               uint8_t values[4], bool transparent)
{
    values[0] = (c0 * 255 + max / 2) / max;
    values[1] = (c1 * 255 + max / 2) / max;
    if (transparent) {
        values[2] = ((c0 + c1) * 255 + max) / (2 * max);
        values[3] = 0;
    } else {
        unsigned denom = 3 * max;
        values[2] = ((2 * c0 + c1) * 255 + denom / 2) / denom;
        values[3] = ((c0 + 2 * c1) * 255 + denom / 2) / denom;
    }
}

static void decode_bc1_colors(uint16_t c0, uint16_t c1, uint8_t r[4],
                              uint8_t g[4], uint8_t b[4], uint8_t a[16],
                              bool transparent)
{
    decode_bc1_channel((c0 >> 11) & 31, (c1 >> 11) & 31, 31, r, transparent);
    decode_bc1_channel((c0 >> 5) & 63, (c1 >> 5) & 63, 63, g, transparent);
    decode_bc1_channel(c0 & 31, c1 & 31, 31, b, transparent);
    a[0] = a[1] = a[2] = 255;
    a[3] = transparent ? 0 : 255;
}

static void write_block_to_texture(uint8_t *converted_data, uint32_t indices,
                                   int i, int j, int width, int height,
                                   int z_pos_factor, uint8_t r[4],
                                   uint8_t g[4], uint8_t b[4], uint8_t a[16],
                                   bool separate_alpha)
{
    int x0 = i * 4,
        y0 = j * 4;

    int x1 = x0 + 4,
        y1 = y0 + 4;

    for (int y = y0; y < y1 && y < height; y++) {
        int y_index = 4 * (y - y0);
        int z_plus_y_pos_factor = z_pos_factor + y * width;
        for (int x = x0; x < x1 && x < width; x++) {
            int xy_index = y_index + x - x0;
            uint8_t index = (indices >> 2 * xy_index) & 0x03;
            uint8_t alpha_index = separate_alpha ? xy_index : index;
            uint8_t *p = converted_data + (z_plus_y_pos_factor + x) * 4;
            *p++ = r[index];
            *p++ = g[index];
            *p++ = b[index];
            *p++ = a[alpha_index];
        }
    }
}

static void decompress_dxt1_block(const uint8_t block_data[8],
                                  uint8_t *converted_data, int i, int j,
                                  int width, int height, int z_pos_factor)
{
    uint16_t c0 = ((uint16_t*)block_data)[0],
             c1 = ((uint16_t*)block_data)[1];
    uint8_t r[4], g[4], b[4], a[16];
    decode_bc1_colors(c0, c1, r, g, b, a, c0 <= c1);

    uint32_t indices = ((uint32_t*)block_data)[1];
    write_block_to_texture(converted_data, indices,
                           i, j, width, height, z_pos_factor,
                           r, g, b, a, false);
}

static void decompress_dxt3_block(const uint8_t block_data[16],
                                  uint8_t *converted_data, int i, int j,
                                  int width, int height, int z_pos_factor)
{
    uint16_t c0 = ((uint16_t*)block_data)[4],
             c1 = ((uint16_t*)block_data)[5];
    uint8_t r[4], g[4], b[4], a[16];
    decode_bc1_colors(c0, c1, r, g, b, a, false);

    uint64_t alpha = ((uint64_t*)block_data)[0];
    for (int a_i=0; a_i < 16; a_i++) {
        a[a_i] = (((alpha >> 4*a_i) & 0x0F) << 4) * 0xFF / 0xF0;
    }

    uint32_t indices = ((uint32_t*)block_data)[3];
    write_block_to_texture(converted_data, indices,
                           i, j, width, height, z_pos_factor,
                           r, g, b, a, true);
}

static void decompress_dxt5_block(const uint8_t block_data[16],
                                  uint8_t *converted_data, int i, int j,
                                  int width, int height, int z_pos_factor)
{
    uint16_t c0 = ((uint16_t*)block_data)[4],
             c1 = ((uint16_t*)block_data)[5];
    uint8_t r[4], g[4], b[4], a[16];
    decode_bc1_colors(c0, c1, r, g, b, a, false);

    uint64_t alpha = ((uint64_t*)block_data)[0];
    uint8_t a0 = block_data[0];
    uint8_t a1 = block_data[1];
    uint8_t a_palette[8];
    a_palette[0] = a0;
    a_palette[1] = a1;
    if (a0 > a1) {
        a_palette[2] = (6*a0+1*a1+3)/7;
        a_palette[3] = (5*a0+2*a1+3)/7;
        a_palette[4] = (4*a0+3*a1+3)/7;
        a_palette[5] = (3*a0+4*a1+3)/7;
        a_palette[6] = (2*a0+5*a1+3)/7;
        a_palette[7] = (1*a0+6*a1+3)/7;
    } else {
        a_palette[2] = (4*a0+1*a1+2)/5;
        a_palette[3] = (3*a0+2*a1+2)/5;
        a_palette[4] = (2*a0+3*a1+2)/5;
        a_palette[5] = (1*a0+4*a1+2)/5;
        a_palette[6] = 0;
        a_palette[7] = 255;
    }
    for (int a_i = 0; a_i < 16; a_i++) {
        a[a_i] = a_palette[(alpha >> (16+3*a_i)) & 0x07];
    }

    uint32_t indices = ((uint32_t*)block_data)[3];
    write_block_to_texture(converted_data, indices,
                           i, j, width, height, z_pos_factor,
                           r, g, b, a, true);
}

void s3tc_decompress_3d_into(enum S3TC_DECOMPRESS_FORMAT color_format,
                             const uint8_t *data, unsigned int width,
                             unsigned int height, unsigned int depth,
                             uint8_t *converted_data)
{
    assert(width > 0);
    assert(height > 0);
    assert(depth > 0);
    unsigned int physical_width = (width + 3) & ~3,
                 physical_height = (height + 3) & ~3;
    int num_blocks_x = physical_width/4,
        num_blocks_y = physical_height/4,
        num_blocks_z = (depth + 3)/4;
    int cur_depth = 0;
    int sub_block_index = 0;
    for (int k = 0; k < num_blocks_z; k++) {
        int residual_depth = depth - cur_depth;
        int block_depth = MIN(residual_depth, 4);
        for (int j = 0; j < num_blocks_y; j++) {
            for (int i = 0; i < num_blocks_x; i++) {
                for (int slice = 0; slice < block_depth; slice++) {
                    int z_pos_factor = (cur_depth + slice) * width * height;
                    if (color_format == S3TC_DECOMPRESS_FORMAT_DXT1) {
                        decompress_dxt1_block(data + 8 * sub_block_index, converted_data,
                                              i, j, width, height, z_pos_factor);
                    } else if (color_format == S3TC_DECOMPRESS_FORMAT_DXT3) {
                        decompress_dxt3_block(data + 16 * sub_block_index, converted_data,
                                              i, j, width, height, z_pos_factor);
                    } else if (color_format == S3TC_DECOMPRESS_FORMAT_DXT5) {
                        decompress_dxt5_block(data + 16 * sub_block_index, converted_data,
                                              i, j, width, height, z_pos_factor);
                    } else {
                        assert(!"Invalid S3TC_DECOMPRESS_FORMAT");
                    }
                    sub_block_index++;
                }
            }
        }
        cur_depth += block_depth;
    }
}

uint8_t *s3tc_decompress_3d(enum S3TC_DECOMPRESS_FORMAT color_format,
                            const uint8_t *data, unsigned int width,
                            unsigned int height, unsigned int depth)
{
    uint8_t *converted_data = (uint8_t*)g_malloc(width * height * depth * 4);
    s3tc_decompress_3d_into(color_format, data, width, height, depth,
                            converted_data);
    return converted_data;
}

void s3tc_decompress_2d_into(enum S3TC_DECOMPRESS_FORMAT color_format,
                             const uint8_t *data, unsigned int width,
                             unsigned int height, uint8_t *converted_data)
{
    assert(width > 0);
    assert(height > 0);
    unsigned int physical_width = (width + 3) & ~3,
                 physical_height = (height + 3) & ~3;
    int num_blocks_x = physical_width / 4, num_blocks_y = physical_height / 4;
    for (int j = 0; j < num_blocks_y; j++) {
        for (int i = 0; i < num_blocks_x; i++) {
            int block_index = j * num_blocks_x + i;
            if (color_format == S3TC_DECOMPRESS_FORMAT_DXT1) {
                decompress_dxt1_block(data + 8 * block_index,
                                      converted_data, i, j, width, height, 0);
            } else if (color_format == S3TC_DECOMPRESS_FORMAT_DXT3) {
                decompress_dxt3_block(data + 16 * block_index,
                                      converted_data, i, j, width, height, 0);
            } else if (color_format == S3TC_DECOMPRESS_FORMAT_DXT5) {
                decompress_dxt5_block(data + 16 * block_index,
                                      converted_data, i, j, width, height, 0);
            } else {
                assert(!"Invalid S3TC_DECOMPRESS_FORMAT");
            }
        }
    }
}

uint8_t *s3tc_decompress_2d(enum S3TC_DECOMPRESS_FORMAT color_format,
                            const uint8_t *data, unsigned int width,
                            unsigned int height)
{
    uint8_t *converted_data = (uint8_t *)g_malloc(width * height * 4);
    s3tc_decompress_2d_into(color_format, data, width, height,
                            converted_data);
    return converted_data;
}
