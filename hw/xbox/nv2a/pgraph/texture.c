/*
 * QEMU Geforce NV2A implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2025 Matt Borgerson
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

#include "hw/xbox/nv2a/nv2a_int.h"
#include "texture.h"
#include "util.h"
#include "swizzle.h"

/* Core option, fixed for the lifetime of the running machine. */
static bool force_texture_lod0;
static bool force_quality_filtering;

void nv2a_set_force_texture_lod0(bool enabled)
{
    force_texture_lod0 = enabled;
}

void nv2a_set_force_quality_filtering(bool enabled)
{
    force_quality_filtering = enabled;
}

bool nv2a_get_force_texture_lod0(void)
{
    return force_texture_lod0;
}

/* AF footprints can cross cube faces, borders and clamped atlas tiles.
 * Restrict the override to wrapping 2D color textures; preserve other game
 * filtering and address modes rather than widening their sample footprint. */
bool pgraph_texture_force_anisotropy(const TextureShape *state, uint32_t filter,
                                     uint32_t address)
{
    unsigned min = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIN);
    unsigned mag = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MAG);
    unsigned u = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRU);
    unsigned v = GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRV);
    return force_quality_filtering && state->dimensionality == 2 &&
        !state->cubemap && !state->border &&
        /* G8B8 can carry displacement data even without signed flags. Keep
         * the game's footprint: AF on its unsigned representation can cross
         * the signed-byte discontinuity used by dependent texture stages. */
        state->color_format != NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8 &&
        state->color_format != NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8 &&
        u >= NV_PGRAPH_TEXADDRESS0_ADDRU_WRAP &&
        u <= NV_PGRAPH_TEXADDRESS0_ADDRU_MIRROR &&
        v >= NV_PGRAPH_TEXADDRESS0_ADDRU_WRAP &&
        v <= NV_PGRAPH_TEXADDRESS0_ADDRU_MIRROR &&
        !kelvin_color_format_info_map[state->color_format].depth &&
        mag == 2 /* tent */ &&
        (min == NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0 ||
         min == NV_PGRAPH_TEXFILTER0_MIN_TENT_NEARESTLOD ||
         min == NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD);
}

const BasicColorFormatInfo kelvin_color_format_info_map[66] = {
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8] = { 1, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8] = { 1, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5] = { 2, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5] = { 2, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4] = { 2, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5] = { 2, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8] = { 4, false },

    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8] = { 1, false },

    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5] = { 2, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5] = { 2, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8] = { 4, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8] = { 1, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8] = { 2, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8] = { 1, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8] = { 2, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8] = { 1, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5] = { 2, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4] = { 2, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8] = { 4, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8] = { 1, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8] = { 2, true },

    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5] = { 2, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8] = { 2, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8B8] = { 2, false },

    [NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8] = { 2, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8] = { 2, true },

    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FIXED] = { 2, false, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FIXED] = { 4, true,
                                                                     true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FLOAT] = { 4, true,
                                                                     true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED] = { 2, true,
                                                                  true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT] = { 2, true,
                                                                  true },

    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y16] = { 2, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_B8G8R8A8] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8] = { 4, false },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8] = { 4, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8] = { 4, true },
    [NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8] = { 4, true },
};

hwaddr pgraph_get_texture_phys_addr(PGRAPHState *pg, int texture_idx)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    int i = texture_idx;

    uint32_t fmt = pgraph_reg_r(pg, NV_PGRAPH_TEXFMT0 + i*4);
    unsigned int dma_select =
        GET_MASK(fmt, NV_PGRAPH_TEXFMT0_CONTEXT_DMA);

    hwaddr offset = pgraph_reg_r(pg, NV_PGRAPH_TEXOFFSET0 + i*4);

    hwaddr dma_len;
    uint8_t *texture_data;
    if (dma_select) {
        texture_data = (uint8_t*)nv_dma_map(d, pg->dma_b, &dma_len);
    } else {
        texture_data = (uint8_t*)nv_dma_map(d, pg->dma_a, &dma_len);
    }
    assert(offset < dma_len);
    texture_data += offset;

    return texture_data - d->vram_ptr;
}

hwaddr pgraph_get_texture_palette_phys_addr_length(PGRAPHState *pg, int texture_idx, size_t *length)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    int i = texture_idx;

    uint32_t palette = pgraph_reg_r(pg, NV_PGRAPH_TEXPALETTE0 + i*4);
    bool palette_dma_select =
        GET_MASK(palette, NV_PGRAPH_TEXPALETTE0_CONTEXT_DMA);
    unsigned int palette_length_index =
        GET_MASK(palette, NV_PGRAPH_TEXPALETTE0_LENGTH);
    unsigned int palette_offset =
        palette & NV_PGRAPH_TEXPALETTE0_OFFSET;

    unsigned int palette_length = 0;
    switch (palette_length_index) {
    case NV_PGRAPH_TEXPALETTE0_LENGTH_256: palette_length = 256; break;
    case NV_PGRAPH_TEXPALETTE0_LENGTH_128: palette_length = 128; break;
    case NV_PGRAPH_TEXPALETTE0_LENGTH_64: palette_length = 64; break;
    case NV_PGRAPH_TEXPALETTE0_LENGTH_32: palette_length = 32; break;
    default: assert(!"Invalid palette length"); break;
    }
    if (length) {
        *length = palette_length * 4;
    }

    hwaddr palette_dma_len;
    uint8_t *palette_data;
    if (palette_dma_select) {
        palette_data = (uint8_t*)nv_dma_map(d, pg->dma_b, &palette_dma_len);
    } else {
        palette_data = (uint8_t*)nv_dma_map(d, pg->dma_a, &palette_dma_len);
    }
    assert(palette_offset < palette_dma_len);
    palette_data += palette_offset;

    return palette_data - d->vram_ptr;
}

/* Guest bytes consumed by the renderers' mip walkers, before any host
 * conversion. Compressed sub-block mips still occupy a complete block. */
static size_t texture_mip_chain_size(PGRAPHState *pg, TextureShape shape)
{
    BasicColorFormatInfo f = kelvin_color_format_info_map[shape.color_format];
    unsigned w = shape.width, h = shape.height;
    unsigned d = shape.dimensionality >= 3 ? shape.depth : 1;
    if (shape.border) {
        w = MAX(16, w * 2);
        h = MAX(16, h * 2);
        if (shape.dimensionality >= 3) {
            d = MAX(16, d * 2);
        }
    }
    bool compressed = pgraph_is_texture_format_compressed(pg, shape.color_format);
    unsigned block_size = shape.color_format ==
        NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5 ? 8 : 16;
    size_t length = 0;
    for (unsigned level = 0; level < shape.levels; level++) {
        w = MAX(w, 1);
        h = MAX(h, 1);
        d = MAX(d, 1);
        if (compressed) {
            length += (size_t)((w + 3) / 4) * ((h + 3) / 4) * d * block_size;
        } else {
            length += (size_t)w * h * d * f.bytes_per_pixel;
        }
        w /= 2;
        h /= 2;
        d /= 2;
    }
    return length;
}

size_t pgraph_get_cubemap_face_length(PGRAPHState *pg, TextureShape shape)
{
    assert(shape.cubemap && shape.dimensionality == 2);
    assert(!kelvin_color_format_info_map[shape.color_format].linear);
    size_t length = texture_mip_chain_size(pg, shape);
    return (length + NV2A_CUBEMAP_FACE_ALIGNMENT - 1) &
           ~(size_t)(NV2A_CUBEMAP_FACE_ALIGNMENT - 1);
}

size_t pgraph_get_texture_length(PGRAPHState *pg, TextureShape *shape)
{
    BasicColorFormatInfo f = kelvin_color_format_info_map[shape->color_format];
    if (f.linear) {
        assert(!shape->cubemap && shape->dimensionality == 2);
        return (size_t)shape->height * shape->pitch;
    }
    if (shape->cubemap) {
        return pgraph_get_cubemap_face_length(pg, *shape) * 6;
    }
    return shape->dimensionality >= 2 ? texture_mip_chain_size(pg, *shape) : 0;
}

/* Ignore only the unread final-row tail. Dirty tracking and cache keys
 * retain the full guest span. Converted formats may consume whole pairs. */
size_t pgraph_texture_hash_length(TextureShape shape, size_t length)
{
    BasicColorFormatInfo f = kelvin_color_format_info_map[shape.color_format];
    if (!f.linear || !shape.width || !shape.height ||
        pgraph_converted_texture_size(shape, shape.width, shape.height, 1)) {
        return length;
    }
    size_t row = (size_t)shape.width * f.bytes_per_pixel;
    if (row <= shape.pitch && shape.pitch - row <= length) {
        return length - (shape.pitch - row);
    }
    return length;
}

TextureShape pgraph_get_texture_shape(PGRAPHState *pg, int texture_idx)
{
    int i = texture_idx;

    uint32_t ctl_0 = pgraph_reg_r(pg, NV_PGRAPH_TEXCTL0_0 + i*4);
    uint32_t ctl_1 = pgraph_reg_r(pg, NV_PGRAPH_TEXCTL1_0 + i*4);
    uint32_t fmt = pgraph_reg_r(pg, NV_PGRAPH_TEXFMT0 + i*4);

#if DEBUG_NV2A
    uint32_t filter = pgraph_reg_r(pg, NV_PGRAPH_TEXFILTER0 + i*4);
    uint32_t address = pgraph_reg_r(pg, NV_PGRAPH_TEXADDRESS0 + i*4);
#endif

    unsigned int min_mipmap_level =
        GET_MASK(ctl_0, NV_PGRAPH_TEXCTL0_0_MIN_LOD_CLAMP);
    unsigned int max_mipmap_level =
        GET_MASK(ctl_0, NV_PGRAPH_TEXCTL0_0_MAX_LOD_CLAMP);

    unsigned int pitch =
        GET_MASK(ctl_1, NV_PGRAPH_TEXCTL1_0_IMAGE_PITCH);

    bool cubemap =
        GET_MASK(fmt, NV_PGRAPH_TEXFMT0_CUBEMAPENABLE);
    unsigned int dimensionality =
        GET_MASK(fmt, NV_PGRAPH_TEXFMT0_DIMENSIONALITY);

    int tex_mode = (pgraph_reg_r(pg, NV_PGRAPH_SHADERPROG) >> (texture_idx * 5)) & 0x1F;
    if (tex_mode == 0x02) {
        assert(pgraph_is_texture_enabled(pg, texture_idx));
        // assert(state.dimensionality == 3);

        // OVERRIDE
        // dimensionality = 3;
    }

    unsigned int color_format = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_COLOR);
    unsigned int levels = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS);
    unsigned int log_width = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_U);
    unsigned int log_height = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_V);
    unsigned int log_depth = GET_MASK(fmt, NV_PGRAPH_TEXFMT0_BASE_SIZE_P);

    unsigned int rect_width =
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_TEXIMAGERECT0 + i*4),
                 NV_PGRAPH_TEXIMAGERECT0_WIDTH);
    unsigned int rect_height =
        GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_TEXIMAGERECT0 + i*4),
                 NV_PGRAPH_TEXIMAGERECT0_HEIGHT);
#if DEBUG_NV2A
    unsigned int lod_bias =
        GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIPMAP_LOD_BIAS);
#endif
    unsigned int border_source = GET_MASK(fmt,
                                          NV_PGRAPH_TEXFMT0_BORDER_SOURCE);

    NV2A_DPRINTF(" texture %d is format 0x%x, "
                    "off 0x%" HWADDR_PRIx " (r %d, %d or %d, %d, %d; %d%s),"
                    " filter %x %x, levels %d-%d %d bias %d\n",
                 i, color_format, address,
                 rect_width, rect_height,
                 1 << log_width, 1 << log_height, 1 << log_depth,
                 pitch,
                 cubemap ? "; cubemap" : "",
                 GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIN),
                 GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MAG),
                 min_mipmap_level, max_mipmap_level, levels,
                 lod_bias);

    assert(color_format < ARRAY_SIZE(kelvin_color_format_info_map));
    BasicColorFormatInfo f = kelvin_color_format_info_map[color_format];
    if (f.bytes_per_pixel == 0) {
        fprintf(stderr, "nv2a: unimplemented texture color format 0x%x\n",
                color_format);
        abort();
    }

    unsigned int width, height, depth;
    if (f.linear) {
        assert(dimensionality == 2);
        width = rect_width;
        height = rect_height;
        depth = 1;

        /* Linear storage has one image, regardless of stale mip fields. */
        levels = 1;
        min_mipmap_level = max_mipmap_level = 0;
    } else {
        width = 1 << log_width;
        height = 1 << log_height;
        depth = 1 << log_depth;
        pitch = 0;

        levels = MIN(levels, max_mipmap_level + 1);

        /* Discard mipmap levels that would be smaller than 1x1.
         * FIXME: Is this actually needed?
         *
         * >> Level 0: 32 x 4
         *    Level 1: 16 x 2
         *    Level 2: 8 x 1
         *    Level 3: 4 x 1
         *    Level 4: 2 x 1
         *    Level 5: 1 x 1
         */
        levels = MIN(levels, MAX(log_width, log_height) + 1);
        assert(levels > 0);

        if (dimensionality == 3) {
            /* FIXME: What about 3D mipmaps? */
            if (log_width < 2 || log_height < 2) {
                /* Base level is smaller than 4x4... */
                levels = 1;
            } else {
                levels = MIN(levels, MIN(log_width, log_height) - 1);
            }
        }
        min_mipmap_level = MIN(levels-1, min_mipmap_level);
        max_mipmap_level = MIN(levels-1, max_mipmap_level);
    }

    /* Keep the mip chain intact: its size also determines cubemap face
     * offsets. Override sampling clamps only, for both renderers. */
    if (force_texture_lod0) {
        min_mipmap_level = 0;
        max_mipmap_level = 0;
    }

    TextureShape shape;

    // We will hash it, so make sure any padding is zero
    memset(&shape, 0, sizeof(shape));

    shape.cubemap = cubemap;
    shape.dimensionality = dimensionality;
    shape.color_format = color_format;
    shape.levels = levels;
    shape.width = width;
    shape.height = height;
    shape.depth = depth;
    shape.min_mipmap_level = min_mipmap_level;
    shape.max_mipmap_level = max_mipmap_level;
    shape.pitch = pitch;
    shape.border = border_source != NV_PGRAPH_TEXFMT0_BORDER_SOURCE_COLOR;
    return shape;
}

size_t pgraph_converted_texture_size(const TextureShape s, unsigned int width,
                                     unsigned int height, unsigned int depth)
{
    switch (s.color_format) {
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8:
        return (size_t)width * height * depth * 4;
    case NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8:
        return (size_t)width * height * 4;
    case NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5:
        return (size_t)width * height * 3;
    default:
        return 0;
    }
}

static void convert_r6g5b5_pixel(uint16_t rgb655, uint8_t *out)
{
    /* Exactly r*127/63 and ((v^16)*255/31)-128, respectively. The small
     * tables preserve the old integer rounding, including signed endpoints. */
    static const uint8_t red6[64] = {
        0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30,
        32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62,
        64, 66, 68, 70, 72, 74, 76, 78, 80, 82, 84, 86, 88, 90, 92, 94,
        96, 98, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 127
    };
    static const uint8_t signed5[32] = {
        3, 11, 20, 28, 36, 44, 52, 61, 69, 77, 85, 94, 102, 110, 118, 127,
        128, 136, 144, 152, 160, 169, 177, 185, 193, 202, 210, 218, 226, 234, 243, 251
    };
    out[0] = red6[rgb655 >> 10];
    out[1] = signed5[(rgb655 >> 5) & 31];
    out[2] = signed5[rgb655 & 31];
}

void pgraph_convert_swizzled_rgb655_into(const uint8_t *data,
                                        unsigned int width,
                                        unsigned int height, bool rgba,
                                        uint8_t *out)
{
    uint32_t mask_x, mask_y, mask_z;
    generate_swizzle_masks(width, height, 1, &mask_x, &mask_y, &mask_z);
    uint32_t off_y = 0;
    for (unsigned int y = 0; y < height; y++) {
        uint32_t off_x = 0;
        for (unsigned int x = 0; x < width; x++) {
            uint16_t pixel;
            memcpy(&pixel, data + (size_t)(off_x | off_y) * 2, sizeof(pixel));
            convert_r6g5b5_pixel(pixel, out);
            if (rgba) {
                out[3] = 127; /* SNORM alpha one, as for RGB host images. */
            }
            out += rgba ? 4 : 3;
            off_x = (off_x - mask_x) & mask_x;
        }
        off_y = (off_y - mask_y) & mask_y;
    }
}

void pgraph_convert_texture_data_into(const TextureShape s,
                                      const uint8_t *data,
                                      const uint8_t *palette_data,
                                      unsigned int width, unsigned int height,
                                      unsigned int depth,
                                      unsigned int row_pitch,
                                      unsigned int slice_pitch,
                                      uint8_t *converted_data)
{
    if (s.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8) {
        const uint8_t *src = data;
        uint32_t *dst = (uint32_t *)converted_data;
        for (int z = 0; z < depth; z++) {
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    uint8_t index = src[y * row_pitch + x];
                    uint32_t color = *(uint32_t *)(palette_data + index * 4);
                    *dst++ = color;
                }
            }
            src += slice_pitch;
        }
    } else if (s.color_format ==
                   NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8 ||
               s.color_format ==
                   NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8) {
        // TODO: Investigate whether a non-1 depth is possible.
        // Generally the hardware asserts when attempting to use volumetric
        // textures in linear formats.
        assert(depth == 1); /* FIXME */
        // FIXME: only valid if control0 register allows for colorspace
        // conversion
        bool uyvy = s.color_format ==
                    NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8;
        for (int y = 0; y < height; y++) {
            pgraph_yuv422_to_rgba(converted_data + (size_t)y * width * 4,
                                 data + (size_t)y * row_pitch * depth,
                                 width, uyvy);
        }
    } else if (s.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5) {
        assert(depth == 1); /* FIXME */
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                uint16_t rgb655;
                memcpy(&rgb655, data + y * row_pitch + x * 2, sizeof(rgb655));
                convert_r6g5b5_pixel(rgb655,
                    &converted_data[(y * width + x) * 3]);
            }
        }
    } else {
        assert(!"no conversion for this format");
    }
}
