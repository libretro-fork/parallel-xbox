/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024 Matt Borgerson
 *
 * Based on GL implementation:
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2024 Matt Borgerson
 * Copyright (c) 2026 RÃ©da ChÃ©rif-Touil
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

#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/s3tc.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"

#include "qemu/fast-hash.h"
#include "data/segaboot_logo.rgba.h"
#include "qemu/lru.h"
#include "renderer.h"
#include "libretro/dsp-ownership.h"
#include "hw/xbox/nv2a/pgraph/vram-ranges.h"
#include "ui/xemu-settings.h"

static void texture_cache_release_node_resources(PGRAPHVkState *r, TextureBinding *snode);

static const VkImageType dimensionality_to_vk_image_type[] = {
    0,
    VK_IMAGE_TYPE_1D,
    VK_IMAGE_TYPE_2D,
    VK_IMAGE_TYPE_3D,
};
static const VkImageViewType dimensionality_to_vk_image_view_type[] = {
    0,
    VK_IMAGE_VIEW_TYPE_1D,
    VK_IMAGE_VIEW_TYPE_2D,
    VK_IMAGE_VIEW_TYPE_3D,
};

static VkSamplerAddressMode lookup_texture_address_mode(int idx)
{
    assert(0 < idx && idx < ARRAY_SIZE(pgraph_texture_addr_vk_map));
    return pgraph_texture_addr_vk_map[idx];
}

// FIXME: Move to common
/* A texture staged for upload: each level's bytes in the staging ring
 * (command.c pgraph_vk_staging_alloc), as vkCmdCopyBufferToImage reads them */
typedef struct TextureLevel {
    unsigned int width, height, depth;
    unsigned int row_length; /* texels a buffer row holds, 0 = width */
    VkDeviceSize offset;
    VkDeviceSize crop_offset; /* copy-only skip; offset/size retain the full allocation */
    size_t size;
    /* where the offset is: the staging ring, the Xbox's RAM
     * (r->vram_buffer), or the GPU unswizzle's output ring */
    enum { LEVEL_STAGING, LEVEL_VRAM, LEVEL_UNSWIZZLED } source;
} TextureLevel;

typedef struct TextureLayer {
    TextureLevel levels[16];
} TextureLayer;

typedef struct TextureLayout {
    TextureLayer layers[6];
} TextureLayout;

/* Staging allocations follow layer/level order. Keep a wrapped upload's
 * tail and head separate: the gap can still belong to an in-flight upload.
 * Cropping changes copy offsets, not the allocation that must be flushed. */
static void flush_texture_staging(PGRAPHState *pg, const TextureLayout *layout,
                                  int num_layers, int num_levels,
                                  unsigned skip_layers)
{
    VkDeviceSize lo = 0, hi = 0;
    for (int layer = 0; layer < num_layers; layer++) {
        if (skip_layers & (1u << layer)) continue;
        for (int mip = 0; mip < num_levels; mip++) {
            const TextureLevel *level = &layout->layers[layer].levels[mip];
            if (level->source != LEVEL_STAGING || !level->size) continue;
            if (hi && level->offset < lo) {
                pgraph_vk_staging_flush(pg, lo, hi - lo);
                hi = 0;
            }
            if (!hi) lo = level->offset;
            hi = MAX(hi, level->offset + level->size);
        }
    }
    if (hi) pgraph_vk_staging_flush(pg, lo, hi - lo);
}

/* The host format of a guest texture format: the map's, except the S3TC
 * formats, which stay compressed where the device samples BC */
static VkColorFormatInfo texture_vkf(PGRAPHVkState *r, int color_format)
{
    if (r->bc_native) {
        switch (color_format) {
        case NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5:
            return (VkColorFormatInfo){ VK_FORMAT_BC1_RGBA_UNORM_BLOCK };
        case NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8:
            return (VkColorFormatInfo){ VK_FORMAT_BC2_UNORM_BLOCK };
        case NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8:
            return (VkColorFormatInfo){ VK_FORMAT_BC3_UNORM_BLOCK };
        default:
            break;
        }
    }
    return kelvin_color_format_vk_map[color_format];
}

static unsigned int vk_format_texel_size(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_R8_UNORM:                return 1;
    case VK_FORMAT_R8G8_UNORM:              return 2;
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16:   return 2;
    case VK_FORMAT_R5G6B5_UNORM_PACK16:     return 2;
    case VK_FORMAT_A4R4G4B4_UNORM_PACK16:   return 2;
    case VK_FORMAT_R16_UNORM:               return 2;
    case VK_FORMAT_R8G8B8A8_SNORM:          return 4;
    case VK_FORMAT_B8G8R8A8_UNORM:          return 4;
    case VK_FORMAT_R8G8B8A8_UNORM:          return 4;
    case VK_FORMAT_R32_UINT:                return 4;
    default:                                return 0;
    }
}

// FIXME: Move to common
static enum S3TC_DECOMPRESS_FORMAT kelvin_format_to_s3tc_format(int color_format)
{
    switch (color_format) {
    case NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5:
        return S3TC_DECOMPRESS_FORMAT_DXT1;
    case NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8:
        return S3TC_DECOMPRESS_FORMAT_DXT3;
    case NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8:
        return S3TC_DECOMPRESS_FORMAT_DXT5;
    default:
        assert(!"Invalid texture color format");
    }
}

/* A level's bytes: a range of the staging ring, 16-byte aligned (a texel
 * or a block, as vkCmdCopyBufferToImage wants its offset) */
static uint8_t *stage_level(PGRAPHState *pg, TextureLevel *level,
                            unsigned int width, unsigned int height,
                            unsigned int depth, size_t size)
{
    uint8_t *ptr;
    level->width = width;
    level->height = height;
    level->depth = depth;
    level->row_length = 0;
    level->crop_offset = 0;
    level->size = size;
    level->source = LEVEL_STAGING;
    level->offset = pgraph_vk_staging_alloc(pg, size, 16, &ptr);
    return ptr;
}

/* A level taken as it lies in the Xbox's RAM, where the RAM is a buffer
 * and the level is aligned as a copy's source must be (its texel or
 * block, and 4 bytes) */
static bool level_from_vram(PGRAPHState *pg, TextureLevel *level,
                            const uint8_t *data, unsigned int width,
                            unsigned int height, unsigned int depth,
                            size_t size, size_t texel)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkDeviceSize offset = data - (const uint8_t *)d->vram_ptr;

    if (r->vram_buffer == VK_NULL_HANDLE || offset % texel || offset % 4 ||
        offset + size > memory_region_size(d->vram)) {
        return false;
    }
    level->width = width;
    level->height = height;
    level->depth = depth;
    level->row_length = 0;
    level->crop_offset = 0;
    level->size = size;
    level->source = LEVEL_VRAM;
    level->offset = offset;
    return true;
}

/* Direct VRAM reads retain guest pitch, but their ownership/bounds span
 * ends at the last active texel. Staged rows contain no pitch padding. */
static void stage_linear_2d(PGRAPHState *pg, TextureLevel *level,
                            const uint8_t *data, unsigned width,
                            unsigned height, unsigned pitch, unsigned bpp)
{
    size_t row_size = (size_t)width * bpp;
    size_t read_size = height ? (size_t)(height - 1) * pitch + row_size : 0;
    if (level_from_vram(pg, level, data, width, height, 1, read_size, bpp)) {
        level->row_length = pitch / bpp;
        return;
    }
    uint8_t *dst = stage_level(pg, level, width, height, 1, row_size * height);
    if (row_size == pitch) {
        memcpy(dst, data, row_size * height);
    } else {
        for (unsigned y = 0; y < height; y++) {
            memcpy(dst + (size_t)y * row_size, data + (size_t)y * pitch,
                   row_size);
        }
    }
}

/* S3TC blocks of a level, as they are, into the ring: the Xbox's 2D block
 * rows are a BC image's; its 3D blocks come in 4-deep groups, slice by
 * slice within each block, and are put into slice order. */
static void stage_blocks_2d(PGRAPHState *pg, TextureLevel *level,
                            const uint8_t *data, unsigned int width,
                            unsigned int height, size_t block_size)
{
    unsigned int bw = ((width + 3) & ~3) / 4, bh = ((height + 3) & ~3) / 4;
    size_t size = (size_t)bw * bh * block_size;
    if (level_from_vram(pg, level, data, width, height, 1, size,
                        block_size)) {
        return;
    }
    uint8_t *dst = stage_level(pg, level, width, height, 1, size);
    memcpy(dst, data, size);
}

/* Grouped direct copies need performance proof with cached staging memory.
 * Keep only source layouts that already match Vulkan's slice order. */
static bool volume_blocks_direct(unsigned width, unsigned height, unsigned depth)
{
    return depth == 1 || (width <= 4 && height <= 4);
}

static void stage_blocks_3d(PGRAPHState *pg, TextureLevel *level,
                            const uint8_t *data, unsigned int width,
                            unsigned int height, unsigned int depth,
                            size_t block_size)
{
    unsigned int bw = ((width + 3) & ~3) / 4, bh = ((height + 3) & ~3) / 4;
    size_t slice = (size_t)bw * bh * block_size;
    /* One slice/XY block already has Vulkan slice order. A single block
     * column can instead be copied one slice at a time with a strided
     * buffer row (copy_volume_block_slices), without CPU rearrangement.
     * Compact grids use one copy region per block and four-slice group. */
    bool ordered = depth == 1 || bw * bh == 1;
    if ((ordered || volume_blocks_direct(width, height, depth)) &&
        level_from_vram(pg, level, data, width, height, depth,
                        slice * depth, block_size)) {
        return;
    }
    uint8_t *dst = stage_level(pg, level, width, height, depth, slice * depth);
    if (ordered) {
        memcpy(dst, data, slice * depth);
        return;
    }
    /* Keep the previous write order for uncached host memory. */
    if (!(pg->vk_renderer_state->storage_buffers[BUFFER_STAGING_SRC].properties &
          VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) {
        const uint8_t *src = data;
        for (unsigned int z0 = 0; z0 < depth; z0 += 4) {
            unsigned int nz = MIN(depth - z0, 4);
            for (unsigned int b = 0; b < bw * bh; b++) {
                for (unsigned int z = 0; z < nz; z++) {
                    uint8_t *out = dst + (z0 + z) * slice + b * block_size;
                    /* BC1 uses eight bytes, BC2/3 sixteen. Constant sizes avoid
                     * generic copy-size dispatch, even when memcpy is inlined. */
                    if (block_size == 8) {
                        memcpy(out, src, 8);
                    } else if (block_size == 16) {
                        memcpy(out, src, 16);
                    } else {
                        memcpy(out, src, block_size);
                    }
                    src += block_size;
                }
            }
        }
        return;
    }
    /* Write each destination slice sequentially, keeping the source's
     * grouped layout while avoiding four destination streams per block. */
    unsigned blocks = bw * bh;
    for (unsigned z0 = 0; z0 < depth; z0 += 4) {
        unsigned nz = MIN(depth - z0, 4);
        for (unsigned z = 0; z < nz; z++) {
            for (unsigned b = 0; b < blocks; b++) {
                const uint8_t *src = data +
                    ((size_t)z0 * blocks + (size_t)b * nz + z) * block_size;
                uint8_t *out = dst + ((size_t)(z0 + z) * blocks + b) * block_size;
                if (block_size == 8) {
                    memcpy(out, src, 8);
                } else if (block_size == 16) {
                    memcpy(out, src, 16);
                } else {
                    memcpy(out, src, block_size);
                }
            }
        }
    }
}

/* A level decoded from S3TC into RGBA8 in the ring, where the device has
 * no BC */
static void decode_level_2d(PGRAPHState *pg, TextureLevel *level,
                            enum S3TC_DECOMPRESS_FORMAT fmt,
                            const uint8_t *data, unsigned int width,
                            unsigned int height)
{
    uint8_t *dst = stage_level(pg, level, width, height, 1,
                               (size_t)width * height * 4);
    s3tc_decompress_2d_into(fmt, data, width, height, dst);
}

static void decode_level_3d(PGRAPHState *pg, TextureLevel *level,
                            enum S3TC_DECOMPRESS_FORMAT fmt,
                            const uint8_t *data, unsigned int width,
                            unsigned int height, unsigned int depth)
{
    uint8_t *dst = stage_level(pg, level, width, height, depth,
                               (size_t)width * height * depth * 4);
    s3tc_decompress_3d_into(fmt, data, width, height, depth, dst);
}

/* A swizzled level, preferably converted on the GPU. CPU palette/RGB655 fallback
 * expands directly into its final staging range. YUV uses the linear path. */
static void stage_swizzled(PGRAPHState *pg, VkCommandBuffer cmd,
                           TextureLevel *level, const TextureShape *s,
                           const uint8_t *data, const uint8_t *palette,
                           unsigned int width, unsigned int height,
                           unsigned int depth, unsigned int bpp)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    size_t row_pitch = (size_t)width * bpp, slice_pitch = row_pitch * height;
    size_t raw = slice_pitch * depth;
    size_t converted = pgraph_converted_texture_size(*s, width, height, depth);
    bool palette_format =
        s->color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8;

    /* With at most one non-unit axis Morton order is already linear.
     * Copy straight from imported RAM instead of dispatching unswizzle or
     * staging the same bytes. Keep conversion and alignment fallbacks. */
    bool linear_order = width && height && depth &&
        ((width == 1 && height == 1) ||
         (width == 1 && depth == 1) ||
         (height == 1 && depth == 1));
    if (!converted && linear_order &&
        level_from_vram(pg, level, data, width, height, depth, raw, bpp)) {
        return;
    }

    /* by the GPU, from the RAM, where it can: as it is, or through the
     * palette */
    if (!converted || palette_format) {
        VkDeviceSize out;
        if (pgraph_vk_unswizzle(pg, cmd, data - (const uint8_t *)d->vram_ptr,
                                width, height, depth, bpp,
                                palette_format ?
                                    palette - (const uint8_t *)d->vram_ptr :
                                    0,
                                &out)) {
            level->width = width;
            level->height = height;
            level->depth = depth;
            level->row_length = 0;
            level->crop_offset = 0;
            level->size = converted ? converted : raw;
            level->source = LEVEL_UNSWIZZLED;
            level->offset = out;
            return;
        }
    }

    if (palette_format) {
        uint8_t *dst = stage_level(pg, level, width, height, depth, converted);
        unswizzle_box_palette(data, palette, width, height, depth, dst);
        return;
    }
    if (s->color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5) {
        assert(depth == 1); /* Same restriction as the linear converter. */
        uint8_t *dst = stage_level(pg, level, width, height, depth,
                                   (size_t)width * height * 4);
        pgraph_convert_swizzled_rgb655_into(data, width, height, true, dst);
        return;
    }
    /* All non-linear conversion formats returned above. */
    assert(!converted);
    uint8_t *dst = stage_level(pg, level, width, height, depth, raw);
    if (depth == 1) {
        unswizzle_rect(data, width, height, dst, row_pitch, bpp);
    } else {
        unswizzle_box(data, width, height, depth, dst, row_pitch,
                      slice_pitch, bpp);
    }
}

/* Match the existing GL base-face crop. Higher mip border origins remain
 * at their existing Vulkan location pending a hardware layout reference,
 * but their copy extents must fit the actual destination mip images. */
static void crop_cubemap_level(TextureLevel *level, unsigned width,
                                unsigned height, unsigned mip, size_t block_size)
{
    unsigned source_width = level->width, source_height = level->height;
    unsigned origin = mip ? 0 : 4;
    assert(origin + width <= source_width && origin + height <= source_height);
    if (block_size) {
        unsigned blocks_per_row = (source_width + 3) / 4;
        VkDeviceSize first_block = (VkDeviceSize)(origin / 4) * blocks_per_row + origin / 4;
        level->crop_offset = first_block * block_size;
        level->row_length = blocks_per_row * 4;
    } else {
        size_t bpp = level->size / ((size_t)source_width * source_height);
        assert(bpp && level->size == (size_t)source_width * source_height * bpp);
        level->crop_offset = ((VkDeviceSize)origin * source_width + origin) * bpp;
        level->row_length = source_width;
    }
    level->width = width;
    level->height = height;
}

static VkBufferImageCopy texture_level_copy(const TextureLevel *level,
                                             unsigned layer, unsigned mip)
{
    return (VkBufferImageCopy){
        .bufferOffset = level->offset + level->crop_offset,
        .bufferRowLength = level->row_length,
        .bufferImageHeight = 0,
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1 },
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { level->width, level->height, level->depth },
    };
}

/* Only imported native BC volumes have grouped source slices here. Other
 * levels retain the ordinary single-region copy. */
static size_t volume_block_slice_size(const TextureLevel *level, VkFormat format)
{
    bool column = level->width <= 4 && level->height > 4;
    bool blocks = level->width > 4 &&
        volume_blocks_direct(level->width, level->height, level->depth);
    if (level->source != LEVEL_VRAM || !(column || blocks) || level->depth <= 1) {
        return 0;
    }
    switch (format) {
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return 8;
    case VK_FORMAT_BC2_UNORM_BLOCK:
    case VK_FORMAT_BC3_UNORM_BLOCK: return 16;
    default: return 0;
    }
}

/* Xbox stores each XY block's slices together in groups of up to four.
 * Multi-column grids choose grouped blocks or columns by region count.
 * bufferRowLength spans all columns' grouped slices between block rows. */
static void copy_volume_block_slices(VkCommandBuffer cmd, VkBuffer source,
                                     VkImage image, const TextureLevel *level,
                                     unsigned layer, unsigned mip,
                                     size_t block_size)
{
    unsigned bw = (level->width + 3) / 4;
    unsigned bh = (level->height + 3) / 4;
    unsigned grouped_regions = bw * bh * ((level->depth + 3) / 4);
    /* A final single-slice group is a contiguous whole XY plane. */
    if (level->depth % 4 == 1) grouped_regions -= bw * bh - 1;
    unsigned column_regions = bw * level->depth;
    if (level->width > 4 && grouped_regions <= column_regions) {
        /* A single XY block's group of Z slices is contiguous in guest RAM.
         * Wider block grids are eligible only when the whole level fits in this
         * region batch; the original four-block route retains its limits. */
        unsigned blocks = bw * bh;
        VkBufferImageCopy regions[64];
        unsigned total = grouped_regions;
        for (unsigned first = 0; first < total;) {
            unsigned count = MIN(total - first, ARRAY_SIZE(regions));
            for (unsigned i = 0; i < count; i++, first++) {
                if (level->depth % 4 == 1 && first == total - 1) {
                    regions[i] = (VkBufferImageCopy){
                        .bufferOffset = level->offset +
                            (VkDeviceSize)(level->depth - 1) * blocks * block_size,
                        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1 },
                        .imageOffset = { 0, 0, level->depth - 1 },
                        .imageExtent = { level->width, level->height, 1 },
                    };
                    continue;
                }
                unsigned group = (first / blocks) * 4;
                unsigned block = first % blocks;
                unsigned x = (block % bw) * 4;
                unsigned y = (block / bw) * 4;
                unsigned nz = MIN(level->depth - group, 4);
                regions[i] = (VkBufferImageCopy){
                    .bufferOffset = level->offset +
                        ((VkDeviceSize)group * blocks + block * nz) * block_size,
                    .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1 },
                    .imageOffset = { x, y, group },
                    .imageExtent = { MIN(level->width - x, 4),
                                     MIN(level->height - y, 4), nz },
                };
            }
            vkCmdCopyBufferToImage(cmd, source, image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions);
        }
        return;
    }
    /* Each source block row advances over every column's grouped slices. */
    VkDeviceSize slice = (VkDeviceSize)bw * bh * block_size;
    VkBufferImageCopy regions[64];
    for (unsigned first = 0; first < column_regions;) {
        unsigned count = MIN(column_regions - first, ARRAY_SIZE(regions));
        for (unsigned i = 0; i < count; i++, first++) {
            unsigned z = first / bw;
            unsigned x = (first % bw) * 4;
            unsigned group = z & ~3u;
            unsigned group_depth = MIN(level->depth - group, 4);
            regions[i] = (VkBufferImageCopy){
                .bufferOffset = level->offset + group * slice +
                    ((first % bw) * group_depth + z - group) * block_size,
                .bufferRowLength = bw * group_depth * 4,
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1 },
                .imageOffset = { x, 0, z },
                .imageExtent = { MIN(level->width - x, 4), level->height, 1 },
            };
        }
        vkCmdCopyBufferToImage(cmd, source, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions);
    }
}

/* The texture's levels, from the guest's memory into the staging ring, in
 * the open single-time command buffer's range. No buffer of its own: a
 * level is decoded, unswizzled or copied straight into the ring. */
static void stage_texture(PGRAPHState *pg, VkCommandBuffer cmd,
                          int texture_idx, const TextureKey *key,
                          TextureLayout *layout, unsigned skip_layers)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    TextureShape s = key->state;
    BasicColorFormatInfo f = kelvin_color_format_info_map[s.color_format];

    NV2A_VK_DGROUP_BEGIN("Texture %d: cubemap=%d, dimensionality=%d, color_format=0x%x, levels=%d, width=%d, height=%d, depth=%d border=%d, min_mipmap_level=%d, max_mipmap_level=%d, pitch=%d",
        texture_idx,
        s.cubemap,
        s.dimensionality,
        s.color_format,
        s.levels,
        s.width,
        s.height,
        s.depth,
        s.border,
        s.min_mipmap_level,
        s.max_mipmap_level,
        s.pitch
        );

    // Sanity checks on below assumptions
    if (f.linear) {
        assert(s.dimensionality == 2);
    }
    if (s.cubemap) {
        assert(s.dimensionality == 2);
        assert(!f.linear);
    }
    assert(s.dimensionality > 1);

    const hwaddr texture_vram_offset = key->texture_vram_offset;
    const uint8_t *texture_data_ptr = (uint8_t *)d->vram_ptr + texture_vram_offset;

    /* The binding key already validated the source addresses. Only indexed
     * texels consume palette memory; other formats keep a NULL pointer. */
    const uint8_t *palette_data_ptr = NULL;
    if (s.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8) {
        palette_data_ptr = (uint8_t *)d->vram_ptr + key->palette_vram_offset;
    }

    unsigned int adjusted_width = s.width, adjusted_height = s.height,
                 adjusted_pitch = s.pitch, adjusted_depth = s.depth;

    if (!f.linear && s.border) {
        adjusted_width = MAX(16, adjusted_width * 2);
        adjusted_height = MAX(16, adjusted_height * 2);
        adjusted_pitch = adjusted_width * (s.pitch / s.width);
        adjusted_depth = MAX(16, s.depth * 2);
    }

    if (f.linear) {
        assert(s.pitch % f.bytes_per_pixel == 0 && "Can't handle strides unaligned to pixels");
        assert(s.levels == 1);
        TextureLevel *level = &layout->layers[0].levels[0];
        size_t converted = pgraph_converted_texture_size(
            s, adjusted_width, adjusted_height, 1);
        if (converted) {
            uint8_t *dst = stage_level(pg, level, adjusted_width,
                                       adjusted_height, 1, converted);
            pgraph_convert_texture_data_into(s, texture_data_ptr,
                                             palette_data_ptr, adjusted_width,
                                             adjusted_height, 1,
                                             adjusted_pitch, 0, dst);
        } else {
            /* the rows as they are, the pitch's texels to a row: from the
             * RAM itself where it can be */
            assert(adjusted_width <= s.width);
            stage_linear_2d(pg, level, texture_data_ptr, adjusted_width,
                             adjusted_height, adjusted_pitch, f.bytes_per_pixel);
        }
        NV2A_VK_DGROUP_END();
        return;
    }

    bool is_compressed = pgraph_is_texture_format_compressed(pg, s.color_format);
    size_t block_size = 0;
    enum S3TC_DECOMPRESS_FORMAT s3tc_fmt = S3TC_DECOMPRESS_FORMAT_DXT1;
    if (is_compressed) {
        bool is_dxt1 =
            s.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5;
        block_size = is_dxt1 ? 8 : 16;
        s3tc_fmt = kelvin_format_to_s3tc_format(s.color_format);
    }

    if (s.dimensionality == 2) {
        /* texture_length is six equally aligned faces for a cube key. */
        hwaddr layer_size = s.cubemap ? key->texture_length / 6 : 0;
        const int num_layers = s.cubemap ? 6 : 1;
        for (int layer = 0; layer < num_layers; layer++) {
            if (skip_layers & (1u << layer)) continue;
            unsigned int width = adjusted_width, height = adjusted_height;
            texture_data_ptr = (uint8_t *)d->vram_ptr + texture_vram_offset +
                               layer * layer_size;

            for (int level_idx = 0; level_idx < s.levels; level_idx++) {
                TextureLevel *level = &layout->layers[layer].levels[level_idx];
                NV2A_VK_DPRINTF("Layer %d Level %d @ %x", layer, level_idx, (int)((char*)texture_data_ptr - (char*)d->vram_ptr));

                width = MAX(width, 1);
                height = MAX(height, 1);
                if (is_compressed) {
                    // https://docs.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression#virtual-size-versus-physical-size
                    unsigned int physical_width = (width + 3) & ~3,
                                 physical_height = (height + 3) & ~3;

                    if (r->bc_native) {
                        stage_blocks_2d(pg, level, texture_data_ptr, width,
                                        height, block_size);
                    } else {
                        decode_level_2d(pg, level, s3tc_fmt, texture_data_ptr,
                                        width, height);
                    }
                    texture_data_ptr +=
                        physical_width / 4 * physical_height / 4 * block_size;
                } else {
                    stage_swizzled(pg, cmd, level, &s, texture_data_ptr,
                                   palette_data_ptr, width, height, 1,
                                   f.bytes_per_pixel);
                    texture_data_ptr += width * height * f.bytes_per_pixel;
                }
                if (s.cubemap && adjusted_width != s.width) {
                    crop_cubemap_level(level, MAX(s.width >> level_idx, 1),
                        MAX(s.height >> level_idx, 1), level_idx,
                        is_compressed && r->bc_native ? block_size : 0);
                }

                width /= 2;
                height /= 2;
            }
        }
    } else if (s.dimensionality == 3) {
        assert(!f.linear);
        unsigned int width = adjusted_width, height = adjusted_height,
                     depth = adjusted_depth;

        for (int level_idx = 0; level_idx < s.levels; level_idx++) {
            TextureLevel *level = &layout->layers[0].levels[level_idx];
            width = MAX(width, 1);
            height = MAX(height, 1);
            depth = MAX(depth, 1);
            if (is_compressed) {
                unsigned int physical_width = (width + 3) & ~3,
                             physical_height = (height + 3) & ~3;
                if (r->bc_native) {
                    stage_blocks_3d(pg, level, texture_data_ptr, width,
                                    height, depth, block_size);
                } else {
                    decode_level_3d(pg, level, s3tc_fmt, texture_data_ptr,
                                    width, height, depth);
                }
                texture_data_ptr += physical_width / 4 * physical_height / 4 * depth * block_size;
            } else {
                stage_swizzled(pg, cmd, level, &s, texture_data_ptr,
                               palette_data_ptr, width, height, depth,
                               f.bytes_per_pixel);
                texture_data_ptr += width * height * depth * f.bytes_per_pixel;
            }

            width /= 2;
            height /= 2;
            depth /= 2;
        }
    }

    NV2A_VK_DGROUP_END();
}

struct pgraph_texture_possibly_dirty_struct {
    hwaddr addr, size;
    bool force_revalidate;
};

static void mark_textures_possibly_dirty_visitor(Lru *lru, LruNode *node, void *opaque)
{
    struct pgraph_texture_possibly_dirty_struct *test = opaque;

    TextureBinding *tnode = container_of(node, TextureBinding, node);
    if (tnode->possibly_dirty && !test->force_revalidate) {
        return;
    }

    bool overlapping = pgraph_vram_ranges_overlap(
        test->addr, test->size, tnode->key.texture_vram_offset,
        tnode->key.texture_length);
    overlapping |= pgraph_vram_ranges_overlap(
        test->addr, test->size, tnode->key.palette_vram_offset,
        tnode->key.palette_length);

    tnode->possibly_dirty |= overlapping;
    if (overlapping && test->force_revalidate) {
        tnode->validation_frame = -1;
    }
}

static void mark_textures_possibly_dirty(NV2AState *d, hwaddr addr,
                                         hwaddr size, bool force_revalidate)
{
    if (!size) {
        return;
    }
    assert(pgraph_vram_range_valid(addr, size, memory_region_size(d->vram)));
    /* Dirty tracking clears whole pages, so invalidate every cached view
     * sharing those pages, including palettes. */
    hwaddr end = TARGET_PAGE_ALIGN(addr + size);
    addr &= TARGET_PAGE_MASK;
    assert(end <= memory_region_size(d->vram));

    struct pgraph_texture_possibly_dirty_struct test = {
        .addr = addr,
        .size = end - addr,
        .force_revalidate = force_revalidate,
    };

    lru_visit_active(&d->pgraph.vk_renderer_state->texture_cache,
                     mark_textures_possibly_dirty_visitor,
                     &test);
}

void pgraph_vk_mark_textures_possibly_dirty(NV2AState *d,
                                            hwaddr addr, hwaddr size)
{
    /* A newly observed dirty page can contain a CPU update after an
     * earlier sample in this same frame. Do not defer that revalidation. */
    mark_textures_possibly_dirty(d, addr, size, true);
}

/* A GPU writeback can supply new bytes between draws in the same frame.
 * Every overlapping cached view must bypass the RAM validation throttle. */
void pgraph_vk_invalidate_textures_from_gpu(NV2AState *d,
                                           hwaddr addr, hwaddr size)
{
    mark_textures_possibly_dirty(d, addr, size, true);
}

/* Surface writes leave RAM behind. Mark just the bound consumers here;
 * normal creation materializes and invalidates unbound cached views later. */
void pgraph_vk_invalidate_bound_textures(PGRAPHState *pg, hwaddr addr,
                                         hwaddr size)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    for (unsigned i = 0; i < NV2A_MAX_TEXTURES; i++) {
        TextureBinding *binding = r->texture_bindings[i];
        if (binding &&
            (pgraph_vram_ranges_overlap(addr, size,
                binding->key.texture_vram_offset, binding->key.texture_length) ||
             pgraph_vram_ranges_overlap(addr, size,
                binding->key.palette_vram_offset, binding->key.palette_length))) {
            binding->possibly_dirty = true;
            binding->validation_frame = (unsigned int)-1;
        }
    }
}

static bool check_texture_dirty(NV2AState *d, hwaddr addr, hwaddr size)
{
    if (!size) {
        return false;
    }
    assert(pgraph_vram_range_valid(addr, size, memory_region_size(d->vram)));
    hwaddr end = TARGET_PAGE_ALIGN(addr + size);
    addr &= TARGET_PAGE_MASK;
    assert(end <= memory_region_size(d->vram));
    return memory_region_test_and_clear_dirty(d->vram, addr, end - addr,
                                              DIRTY_MEMORY_NV2A_TEX);
}

// Check if any of the pages spanned by the a texture are dirty.
static bool check_texture_possibly_dirty(NV2AState *d,
                                         hwaddr texture_vram_offset,
                                         unsigned int length,
                                         hwaddr palette_vram_offset,
                                         unsigned int palette_length)
{
    bool possibly_dirty = false;
    if (check_texture_dirty(d, texture_vram_offset, length)) {
        possibly_dirty = true;
        pgraph_vk_mark_textures_possibly_dirty(d, texture_vram_offset, length);
    }
    if (palette_length && check_texture_dirty(d, palette_vram_offset,
                                                     palette_length)) {
        possibly_dirty = true;
        pgraph_vk_mark_textures_possibly_dirty(d, palette_vram_offset,
                                            palette_length);
    }
    return possibly_dirty;
}

#include "s3tc-decode.h"

/* GPU faces use draw stamps, not the stale guest bytes beneath them. Keep
 * RAM-face identity position-sensitive without reading those GPU spans. */
static uint64_t hash_mixed_cube_ram(const uint8_t *data, size_t face_length,
                                    unsigned gpu_mask)
{
    uint64_t faces[7] = { gpu_mask };
    for (unsigned f = 0; f < 6; f++) {
        if (!(gpu_mask & (1u << f))) {
            faces[f + 1] = fast_hash(data + f * face_length, face_length);
        }
    }
    return fast_hash((const uint8_t *)faces, sizeof(faces));
}

/* An upload leaves skipped cube layers untouched. Provenance belongs to the
 * current image only: fresh/COW images have no layers worth retaining. */
static void preserve_cube_upload_sources(TextureBinding *binding,
                                          unsigned skipped_layers)
{
    binding->cube_source_mask =
        binding->current_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
            binding->cube_source_mask & skipped_layers;
}

// FIXME: Make sure we update sampler when data matches. Should we add filtering
// options to the textureshape?
static void upload_texture_image(PGRAPHState *pg, int texture_idx,
                                 TextureBinding *binding, unsigned skip_layers)
{
    preserve_cube_upload_sources(binding, skip_layers);
    binding->depth_source_copy = false;
    /* An aux upload runs before the open recording. Submit first only if
     * that recording references the current image. A CoW replacement is
     * unreferenced; recorded descriptors retain the retired image. */
    {
        PGRAPHVkState *rr = pg->vk_renderer_state;
        if (binding->image_referenced && rr->in_command_buffer &&
            binding->submit_time == rr->submit_count) {
            pgraph_vk_finish(pg, VK_FINISH_REASON_TEXTURE_UP);
        }
    }
    PGRAPHVkState *r = pg->vk_renderer_state;
    TextureShape *state = &binding->key.state;
    VkColorFormatInfo vkf = texture_vkf(r, state->color_format);

    nv2a_profile_inc_counter(NV2A_PROF_TEX_UPLOAD);

    /* The recording first: the levels are staged in its range of the ring,
     * which it reads after flushing and auxiliary queue submission */
    VkCommandBuffer cmd = pgraph_vk_begin_single_time_commands(pg);
    pgraph_vk_begin_debug_marker(r, cmd, RGBA_GREEN, __func__);

    if (upload_s3tc_image_gpu(pg, cmd, binding, skip_layers)) {
        nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_4);
        pgraph_vk_end_debug_marker(r, cmd);
        pgraph_vk_end_single_time_commands_async(pg, cmd);
        return;
    }

    TextureLayout layout;
    if (binding->logo) {
        vkf = texture_vkf(r, NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8);
        uint8_t *dst = stage_level(pg, &layout.layers[0].levels[0], 2048,
                                   512, 1, segaboot_logo_size);
        memcpy(dst, segaboot_logo_data, segaboot_logo_size);
    } else {
        stage_texture(pg, cmd, texture_idx, &binding->key, &layout, skip_layers);
    }
    const int num_layers = state->cubemap ? 6 : 1;
    const int num_levels = binding->logo ? 1 : state->levels;

    /* the levels staged in the ring, those taken from the RAM, and those
     * the GPU unswizzled */
    VkBufferImageCopy regions[6 * 16], vram_regions[6 * 16],
        unswizzled_regions[6 * 16];
    int num_regions = 0, num_vram_regions = 0, num_unswizzled_regions = 0;
    bool volume_slices = false;
    VkDeviceSize vram_begin = 0, vram_end = 0;

    for (int layer_idx = 0; layer_idx < num_layers; layer_idx++) {
        if (skip_layers & (1u << layer_idx)) continue;
        TextureLayer *layer = &layout.layers[layer_idx];
        NV2A_VK_DPRINTF("Layer %d", layer_idx);
        for (int level_idx = 0; level_idx < num_levels; level_idx++) {
            TextureLevel *level = &layer->levels[level_idx];
            NV2A_VK_DPRINTF(" - Level %d, w=%d h=%d d=%d @ %08" HWADDR_PRIx,
                            level_idx, level->width, level->height,
                            level->depth, level->offset);
            if (level->source == LEVEL_VRAM) {
                lr_gpu_ram_claim((uintptr_t)cmd, level->offset, level->size);
                vram_begin = vram_end ? MIN(vram_begin, level->offset) :
                                        level->offset;
                vram_end = MAX(vram_end, level->offset + level->size);
            }
            if (volume_block_slice_size(level, vkf.vk_format)) {
                volume_slices = true;
                continue;
            }
            VkBufferImageCopy *region =
                level->source == LEVEL_VRAM ?
                    &vram_regions[num_vram_regions++] :
                level->source == LEVEL_UNSWIZZLED ?
                    &unswizzled_regions[num_unswizzled_regions++] :
                    &regions[num_regions++];
            *region = texture_level_copy(level, layer_idx, level_idx);
        }
    }
    if (!(r->storage_buffers[BUFFER_STAGING_SRC].properties &
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        flush_texture_staging(pg, &layout, num_layers, num_levels, skip_layers);
    }

    pgraph_vk_upload_source_barrier(pg, cmd, vram_end != 0,
                                    vram_begin, vram_end - vram_begin);
    if (num_unswizzled_regions) {
        pgraph_vk_unswizzle_done(pg, cmd);
    }

    pgraph_vk_transition_image_layout(pg, cmd, binding->image, vkf.vk_format,
                                      binding->current_layout,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    binding->current_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    if (num_regions) {
        vkCmdCopyBufferToImage(cmd,
                               r->storage_buffers[BUFFER_STAGING_SRC].buffer,
                               binding->image, binding->current_layout,
                               num_regions, regions);
    }
    if (num_vram_regions) {
        vkCmdCopyBufferToImage(cmd, r->vram_buffer, binding->image,
                               binding->current_layout, num_vram_regions,
                               vram_regions);
    }
    if (volume_slices) {
        for (int layer = 0; layer < num_layers; layer++) {
            if (skip_layers & (1u << layer)) continue;
            for (int mip = 0; mip < num_levels; mip++) {
                TextureLevel *level = &layout.layers[layer].levels[mip];
                size_t block_size = volume_block_slice_size(level, vkf.vk_format);
                if (block_size) {
                    copy_volume_block_slices(cmd, r->vram_buffer, binding->image,
                                             level, layer, mip, block_size);
                }
            }
        }
    }
    if (num_unswizzled_regions) {
        vkCmdCopyBufferToImage(
            cmd, r->storage_buffers[BUFFER_UNSWIZZLE_OUT].buffer,
            binding->image, binding->current_layout, num_unswizzled_regions,
            unswizzled_regions);
    }

    pgraph_vk_transition_image_layout(pg, cmd, binding->image, vkf.vk_format,
                                      binding->current_layout,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    binding->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_4);
    pgraph_vk_end_debug_marker(r, cmd);
    pgraph_vk_end_single_time_commands_async(pg, cmd);
}

/* The spec does not promise depth blits: RADV has no BLIT_DST for
 * D32_SFLOAT_S8_UINT and crashes when asked. Asked once per format. */
static bool format_blit_supported(PGRAPHVkState *r, VkFormat format)
{
    for (int i = 0; i < r->blit_formats_n; i++) {
        if (r->blit_formats[i].format == format) {
            return r->blit_formats[i].ok;
        }
    }
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(r->physical_device, format, &props);
    bool ok = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) &&
              (props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT);
    if (r->blit_formats_n < ARRAY_SIZE(r->blit_formats)) {
        r->blit_formats[r->blit_formats_n].format = format;
        r->blit_formats[r->blit_formats_n].ok = ok;
        r->blit_formats_n++;
    }
    if (!ok) {
        fprintf(stderr, "vk: format %d cannot be blitted on this driver,"
                " native-size transfer path off\n", format);
    }
    return ok;
}

/* Native-size transfer scratch, shared by depth sampling and packed color
 * aliases. NEAREST takes each native texel's centre sub-sample (source texel
 * floor((x + 0.5) * scale)) before the copy/pack chain. Kept per format/size. */
static VkImage native_get_scratch(PGRAPHState *pg, VkFormat format,
                                   unsigned int width, unsigned int height)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!format_blit_supported(r, format)) {
        return VK_NULL_HANDLE; /* caller falls back to the scaled path */
    }
    int free_slot = -1;
    for (int i = 0; i < ARRAY_SIZE(r->znative_scratch); i++) {
        if (r->znative_scratch[i].image == VK_NULL_HANDLE) {
            if (free_slot < 0) {
                free_slot = i;
            }
            continue;
        }
        if (r->znative_scratch[i].format == format &&
            r->znative_scratch[i].width == width &&
            r->znative_scratch[i].height == height) {
            return r->znative_scratch[i].image;
        }
    }
    if (free_slot < 0) {
        return VK_NULL_HANDLE; /* caller falls back to the scaled path */
    }
    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = format,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    XemuVkMemoryRequest aci = {
        .host = false,
    };
    VkImage image;
    XemuVkAllocation allocation;
    VK_CHECK(xemu_vk_create_image(r->allocator, &ici, &aci, &image, &allocation));
    r->znative_scratch[free_slot].image = image;
    r->znative_scratch[free_slot].allocation = allocation;
    r->znative_scratch[free_slot].format = format;
    r->znative_scratch[free_slot].width = width;
    r->znative_scratch[free_slot].height = height;
    return image;
}

/* Only RAM layers need an upload. Keep GPU faces at their rendered size:
 * upload native RAM faces once, then expand them on the GPU. The temporary
 * image belongs to this recording and is pooled only after it has retired;
 * a shared scratch could be overwritten while earlier draws still read it. */
static void upload_mixed_cube_ram(PGRAPHState *pg, int texture_idx,
                                  TextureBinding *binding, unsigned gpu_mask)
{
    if (binding->key.scale == 1) {
        upload_texture_image(pg, texture_idx, binding, gpu_mask);
        return;
    }
    preserve_cube_upload_sources(binding, gpu_mask);
    PGRAPHVkState *r = pg->vk_renderer_state;
    TextureBinding native = { 0 };
    native.key = binding->key;
    native.key.scale = 1;
    native.submit_time = binding->submit_time;
    native.image_ci = binding->image_ci;
    native.image_ci.extent.width = native.key.state.width;
    native.image_ci.extent.height = native.key.state.height;
    native.image_ci.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    XemuVkMemoryRequest aci = { .host = false };
    if (!pgraph_vk_image_pool_get(r, &native.image_ci, &native.image,
                                  &native.allocation)) {
        VK_CHECK(xemu_vk_create_image(r->allocator, &native.image_ci, &aci,
                                &native.image, &native.allocation));
    }
    upload_texture_image(pg, texture_idx, &native, gpu_mask);

    VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(pg);
    VkFormat format = native.image_ci.format;
    pgraph_vk_transition_image_layout(pg, cmd, native.image, format,
        native.current_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    pgraph_vk_transition_image_layout(pg, cmd, binding->image, format,
        binding->current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit regions[6];
    unsigned count = 0;
    for (unsigned face = 0; face < 6; face++) {
        if (gpu_mask & (1u << face)) continue;
        regions[count++] = (VkImageBlit) {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, face, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, face, 1 },
            .srcOffsets[1] = { native.image_ci.extent.width,
                               native.image_ci.extent.height, 1 },
            .dstOffsets[1] = { binding->image_ci.extent.width,
                               binding->image_ci.extent.height, 1 },
        };
    }
    vkCmdBlitImage(cmd, native.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        binding->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, count, regions,
        VK_FILTER_NEAREST);
    /* The caller fills GPU layers next, then transitions the whole cube
     * once for sampling. Avoid a redundant read-layout round trip here. */
    binding->current_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    binding->depth_source_copy = false;
    struct XemuVkTrashEntry trash = {
        .image = native.image, .allocation = native.allocation,
        .image_ci = native.image_ci, .pool_ok = true,
    };
    pgraph_vk_trash_push(r, &trash);
}

static void copy_zeta_surface_to_texture(PGRAPHState *pg, SurfaceBinding *surface,
                                         TextureBinding *texture)
{
    assert(!surface->color);

    PGRAPHVkState *r = pg->vk_renderer_state;
    TextureShape *state = &texture->key.state;
    VkColorFormatInfo vkf = texture_vkf(r, state->color_format);
    bool use_compute_to_convert_depth_stencil =
        surface->host_fmt.vk_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        surface->host_fmt.vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT;

    nv2a_profile_inc_counter(NV2A_PROF_SURF_TO_TEX);

    trace_nv2a_pgraph_surface_render_to_texture(
        surface->vram_addr, surface->width, surface->height);

    unsigned int scaled_width = surface->width,
                 scaled_height = surface->height;
    pgraph_apply_scaling_factor(pg, &scaled_width, &scaled_height);
    /* Native depth: the copy/pack/upload chain below runs at cw x ch
     * (native when the scratch blit is in play, scaled otherwise). */
    VkImage znative_scratch = VK_NULL_HANDLE;
    if (pg->surface_scale_factor > 1) {
        znative_scratch = native_get_scratch(pg, surface->host_fmt.vk_format,
                                              surface->width, surface->height);
    }
    unsigned int cw = znative_scratch ? surface->width : scaled_width;
    unsigned int ch = znative_scratch ? surface->height : scaled_height;

    size_t copied_image_size =
        (size_t)cw * ch * surface->host_fmt.host_bytes_per_pixel;
    size_t stencil_buffer_offset = 0;
    size_t stencil_buffer_size = 0;

    int num_regions = 0;
    VkBufferImageCopy regions[2];
    regions[num_regions++] = (VkBufferImageCopy){
        .bufferOffset = 0,
        .bufferRowLength = 0, // Tightly packed
        .bufferImageHeight = 0, // Tightly packed
        .imageSubresource.aspectMask = surface->color ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT,
        .imageSubresource.mipLevel = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount = 1,
        .imageOffset = (VkOffset3D){0, 0, 0},
        .imageExtent = (VkExtent3D){cw, ch, 1},
    };

    if (surface->host_fmt.aspect & VK_IMAGE_ASPECT_STENCIL_BIT) {
        stencil_buffer_offset =
            ROUND_UP((size_t)cw * ch * 4,
                     r->device_props.limits.minStorageBufferOffsetAlignment);
        stencil_buffer_size = (size_t)cw * ch;
        /* The depth plane copies as 32-bit texels whatever the image texel
         * size (8 bytes for D32_SFLOAT_S8_UINT, the AMD fallback): the
         * copy ends with the stencil plane. Summing the image texel size
         * and the stencil plane claimed 9 bytes a pixel against a buffer
         * sized for 8 (House of the Dead 3 on AMD Windows, assertion). */
        copied_image_size = stencil_buffer_offset + stencil_buffer_size;

        regions[num_regions++] = (VkBufferImageCopy){
            .bufferOffset = stencil_buffer_offset,
            .bufferRowLength = 0, // Tightly packed
            .bufferImageHeight = 0, // Tightly packed
            .imageSubresource.aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .imageSubresource.mipLevel = 0,
            .imageSubresource.baseArrayLayer = 0,
            .imageSubresource.layerCount = 1,
            .imageOffset = (VkOffset3D){0, 0, 0},
            .imageExtent = (VkExtent3D){cw, ch, 1},
        };
    }
    /* The pack shader reads complete stencil words, including its tail. */
    VkDeviceSize input_size = use_compute_to_convert_depth_stencil ?
        ROUND_UP((VkDeviceSize)copied_image_size, 4) : copied_image_size;
    /* Choose native/scaled extents before sizing. Growing drains GPU work,
     * so request only the copied planes and their packed/texture output,
     * including alignment before the stencil plane. Grow before recording. */
    VkDeviceSize output_size = (VkDeviceSize)cw * ch *
        vk_format_texel_size(vkf.vk_format);
    if (use_compute_to_convert_depth_stencil) {
        output_size = MAX(output_size, (VkDeviceSize)cw * ch * 4);
    }
    pgraph_vk_buffer_ensure_capacity(pg, BUFFER_COMPUTE_DST,
                                     MAX(input_size, output_size));
    StorageBuffer *dst_storage_buffer = &r->storage_buffers[BUFFER_COMPUTE_DST];
    assert(dst_storage_buffer->buffer_size >= input_size);

    VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(pg);
    pgraph_vk_begin_debug_marker(r, cmd, RGBA_GREEN, __func__);

    pgraph_vk_transition_image_layout(
        pg, cmd, surface->image, surface->host_fmt.vk_format,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkImage copy_src_image = surface->image;
    if (znative_scratch) {
        pgraph_vk_transition_image_layout(
            pg, cmd, znative_scratch, surface->host_fmt.vk_format,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit blits[2];
        int nb = 0;
        VkImageAspectFlags aspects[2] = { VK_IMAGE_ASPECT_DEPTH_BIT,
                                          VK_IMAGE_ASPECT_STENCIL_BIT };
        for (int a = 0; a < 2; a++) {
            if (!(surface->host_fmt.aspect & aspects[a])) {
                continue;
            }
            blits[nb++] = (VkImageBlit){
                .srcSubresource.aspectMask = aspects[a],
                .srcSubresource.mipLevel = 0,
                .srcSubresource.baseArrayLayer = 0,
                .srcSubresource.layerCount = 1,
                .srcOffsets[0] = (VkOffset3D){ 0, 0, 0 },
                .srcOffsets[1] = (VkOffset3D){ scaled_width, scaled_height, 1 },
                .dstSubresource.aspectMask = aspects[a],
                .dstSubresource.mipLevel = 0,
                .dstSubresource.baseArrayLayer = 0,
                .dstSubresource.layerCount = 1,
                .dstOffsets[0] = (VkOffset3D){ 0, 0, 0 },
                .dstOffsets[1] = (VkOffset3D){ cw, ch, 1 },
            };
        }
        /* Depth/stencil blits must be NEAREST and same-format (spec):
         * exactly the point-downsample wanted. */
        vkCmdBlitImage(cmd, surface->image,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       znative_scratch, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       nb, blits, VK_FILTER_NEAREST);
        pgraph_vk_transition_image_layout(
            pg, cmd, znative_scratch, surface->host_fmt.vk_format,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        copy_src_image = znative_scratch;
    }
    vkCmdCopyImageToBuffer(
        cmd, copy_src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dst_storage_buffer->buffer,
        num_regions, regions);

    pgraph_vk_transition_image_layout(
        pg, cmd, surface->image, surface->host_fmt.vk_format,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    VkBuffer texture_source_buffer;

    if (use_compute_to_convert_depth_stencil) {
        size_t packed_image_size = cw * ch * 4;

        VkBufferMemoryBarrier pre_pack_src_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = r->storage_buffers[BUFFER_COMPUTE_DST].buffer,
            .size = input_size
        };
        VkBufferMemoryBarrier pre_pack_dst_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = r->storage_buffers[BUFFER_COMPUTE_SRC].buffer,
            .size = packed_image_size
        };
        const VkBufferMemoryBarrier pre_pack[] = {
            pre_pack_src_barrier, pre_pack_dst_barrier
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 2,
                             pre_pack, 0, NULL);

        pgraph_vk_pack_depth_stencil_dims(
            pg, surface, cmd,
            r->storage_buffers[BUFFER_COMPUTE_DST].buffer,
            r->storage_buffers[BUFFER_COMPUTE_SRC].buffer, cw, ch, cw, ch);

        VkBufferMemoryBarrier post_pack_src_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = r->storage_buffers[BUFFER_COMPUTE_DST].buffer,
            .size = input_size
        };
        VkBufferMemoryBarrier post_pack_dst_barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = r->storage_buffers[BUFFER_COMPUTE_SRC].buffer,
            .size = packed_image_size
        };
        const VkBufferMemoryBarrier post_pack[] = {
            post_pack_src_barrier, post_pack_dst_barrier
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 2,
                             post_pack, 0, NULL);

        texture_source_buffer = r->storage_buffers[BUFFER_COMPUTE_SRC].buffer;
    } else {
        VkBufferMemoryBarrier barrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = dst_storage_buffer->buffer,
            .size = copied_image_size
        };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
                             1, &barrier, 0, NULL);

        texture_source_buffer = dst_storage_buffer->buffer;
    }

    pgraph_vk_transition_image_layout(pg, cmd, texture->image, vkf.vk_format,
                                      texture->current_layout,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    texture->current_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

    regions[0] = (VkBufferImageCopy){
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount = 1,
        .imageOffset = (VkOffset3D){ 0, 0, 0 },
        .imageExtent = (VkExtent3D){ cw, ch, 1 },
    };
    vkCmdCopyBufferToImage(
        cmd, texture_source_buffer, texture->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, regions);

    VkBufferMemoryBarrier post_copy_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = texture_source_buffer,
        .size = (VkDeviceSize)cw * ch * vk_format_texel_size(vkf.vk_format)
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                         &post_copy_barrier, 0, NULL);

    pgraph_vk_transition_image_layout(pg, cmd, texture->image, vkf.vk_format,
                                      texture->current_layout,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    texture->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    pgraph_vk_end_debug_marker(r, cmd);
    pgraph_vk_end_nondraw_commands(pg, cmd);

    texture->draw_time = surface->draw_time;
    texture->depth_source_copy = true;
}

/* Compatible render surfaces are sampled in place, not copied per bind:
 * the transition to shader-read is the snapshot point, and the one back
 * at the next write orders the reads first (WAR). Feedback (the bound
 * render target) keeps the copy, as upstream does. */
static void s2t_borrow_sync(PGRAPHState *pg, SurfaceBinding *surface)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (surface->in_shader_read) {
        return;
    }
    VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(pg);
    pgraph_vk_transition_image_layout(
        pg, cmd, surface->image, surface->host_fmt.vk_format,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    pgraph_vk_end_nondraw_commands(pg, cmd);
    surface->in_shader_read = true;
    surface->borrow_submit = r->submit_count;
}

/* Leaving shader-read (read-back, upload, blit, retarget): units sampling
 * the image in place bind again, so s2t_borrow_sync records the transition
 * before the next draw. */
void pgraph_vk_surface_left_shader_read(PGRAPHState *pg,
                                        SurfaceBinding *surface)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    surface->in_shader_read = false;
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        TextureBinding *b = r->texture_bindings[i];
        if (b && b->s2t_borrowed && b->image == surface->image) {
            pg->texture_dirty[i] = true;
        }
    }
}

struct DropBorrowedCtx {
    PGRAPHState *pg;
    PGRAPHVkState *r;
    VkImage image;
};

static void drop_borrowed_visitor(Lru *lru, LruNode *node, void *opaque)
{
    struct DropBorrowedCtx *ctx = opaque;
    TextureBinding *snode = container_of(node, TextureBinding, node);

    if (!snode->s2t_borrowed || snode->image != ctx->image) {
        return;
    }
    texture_cache_release_node_resources(ctx->r, snode);
    /* A unit still bound to it binds again at the next draw. */
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (ctx->r->texture_bindings[i] == snode) {
            ctx->r->texture_bindings[i] = NULL;
            ctx->pg->texture_dirty[i] = true;
        }
    }
}

void pgraph_vk_texture_drop_borrowed(NV2AState *d, SurfaceBinding *surface)
{
    PGRAPHState *pg = &d->pgraph;
    struct DropBorrowedCtx ctx = {
        .pg = pg, .r = pg->vk_renderer_state, .image = surface->image
    };

    lru_visit_active(&ctx.r->texture_cache, drop_borrowed_visitor, &ctx);
}

/* Layouts are established by the color-copy batch: source TRANSFER_SRC,
 * destination TRANSFER_DST. Record one destination layer's pixel copy. */
static void record_surface_copy(PGRAPHState *pg, VkCommandBuffer cmd,
                                SurfaceBinding *surface,
                                TextureBinding *texture, int layer)
{
    VkImageCopy region = {
        .srcSubresource.aspectMask = surface->host_fmt.aspect,
        .srcSubresource.layerCount = 1,
        .dstSubresource.aspectMask = surface->host_fmt.aspect,
        .dstSubresource.baseArrayLayer = layer,
        .dstSubresource.layerCount = 1,
        .extent.width = surface->width,
        .extent.height = surface->height,
        .extent.depth = 1,
    };
    pgraph_apply_scaling_factor(pg, &region.extent.width,
                                &region.extent.height);
    vkCmdCopyImage(cmd, surface->image,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture->image,
                   texture->current_layout, 1, &region);

}

/* Reinterpret equal-sized rows of native color pixels as a linear texture.
 * Transfers pack active bytes through a device buffer, omitting guest row
 * padding without host readback, arithmetic conversion or a CPU upload.
 * Keep the ordinary CoW snapshot. */
static void copy_surface_to_linear_texture(PGRAPHState *pg,
                                           SurfaceBinding *surface,
                                           TextureBinding *texture)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    const TextureShape *shape = &texture->key.state;
    VkFormat format = texture_vkf(r, shape->color_format).vk_format;
    VkBuffer buffer = r->storage_buffers[BUFFER_COMPUTE_DST].buffer;
    VkImage scratch = pg->surface_scale_factor > 1 ?
        native_get_scratch(pg, surface->host_fmt.vk_format,
                            surface->width, surface->height) : VK_NULL_HANDLE;
    assert(pg->surface_scale_factor == 1 || scratch != VK_NULL_HANDLE);
    VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(pg);
    VkBufferMemoryBarrier storage = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .size = (VkDeviceSize)surface->width * surface->height *
                vk_format_texel_size(surface->host_fmt.vk_format),
    };
    /* Also orders reuse of the native scratch after earlier transfer reads. */
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &storage, 0, NULL);
    pgraph_vk_transition_image_layout(pg, cmd, surface->image,
        surface->host_fmt.vk_format, pgraph_vk_surface_base_layout(surface),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    pgraph_vk_surface_left_shader_read(pg, surface);
    VkImage source = surface->image;
    if (scratch) {
        pgraph_vk_transition_image_layout(pg, cmd, scratch,
            surface->host_fmt.vk_format, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit region = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .srcOffsets[1] = { surface->width * pg->surface_scale_factor,
                               surface->height * pg->surface_scale_factor, 1 },
            .dstOffsets[1] = { surface->width, surface->height, 1 },
        };
        vkCmdBlitImage(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            scratch, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
            VK_FILTER_NEAREST);
        pgraph_vk_transition_image_layout(pg, cmd, scratch,
            surface->host_fmt.vk_format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        source = scratch;
    }
    VkBufferImageCopy region = {
        .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
        .imageExtent = { surface->width, surface->height, 1 },
    };
    vkCmdCopyImageToBuffer(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer, 1, &region);
    storage.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    storage.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &storage, 0, NULL);
    pgraph_vk_transition_image_layout(pg, cmd, texture->image,
        format, texture->current_layout,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    region.imageExtent.width = shape->width;
    region.imageExtent.height = shape->height;
    vkCmdCopyBufferToImage(cmd, buffer, texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    storage.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    storage.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 1, &storage, 0, NULL);
    pgraph_vk_transition_image_layout(pg, cmd, surface->image,
        surface->host_fmt.vk_format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    pgraph_vk_transition_image_layout(pg, cmd, texture->image,
        format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    texture->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texture->draw_time = surface->draw_time;
    nv2a_profile_inc_counter(NV2A_PROF_SURF_TO_TEX_REINTERPRET);
    pgraph_vk_end_nondraw_commands(pg, cmd);
}

/* Batch source and destination transitions around per-face image copies.
 * A source image appearing in several layers needs only one transition pair. */
static void record_color_surface_copies(PGRAPHState *pg, VkCommandBuffer cmd,
                                        SurfaceBinding *faces[6],
                                        TextureBinding *texture, unsigned mask)
{
    VkImageMemoryBarrier before[7], after[7];
    unsigned count = 0;
    VkPipelineStageFlags stages = 0;
    for (unsigned f = 0; f < 6; f++) {
        if (!(mask & (1u << f))) continue;
        SurfaceBinding *surface = faces[f];
        assert(surface && surface->color && surface->image != texture->image);
        bool seen = false;
        for (unsigned i = 0; i < count; i++) seen |= before[i].image == surface->image;
        if (!seen) {
            VkImageLayout old = pgraph_vk_surface_base_layout(surface);
            bool sampled = old == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            assert(sampled || old == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            stages |= sampled ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT :
                                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            before[count] = (VkImageMemoryBarrier){
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = sampled ? VK_ACCESS_SHADER_READ_BIT :
                                          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                .oldLayout = old, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = surface->image,
                .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
                    VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS },
            };
            after[count] = before[count];
            after[count].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            after[count].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            after[count].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            after[count].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            count++;
        }
        pgraph_vk_surface_left_shader_read(pg, surface);
    }
    assert(count);
    VkImageMemoryBarrier destination = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0,
            VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS },
    };
    after[count] = destination;
    unsigned before_count = count;
    if (texture->current_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        bool fresh = texture->current_layout == VK_IMAGE_LAYOUT_UNDEFINED;
        assert(fresh || texture->current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        destination.oldLayout = texture->current_layout;
        destination.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        destination.srcAccessMask = fresh ? 0 : VK_ACCESS_SHADER_READ_BIT;
        destination.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        before[before_count++] = destination;
        stages |= fresh ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT :
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cmd, stages, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, before_count, before);
    texture->current_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    for (unsigned f = 0; f < 6; f++) {
        if (mask & (1u << f)) record_surface_copy(pg, cmd, faces[f], texture, f);
    }
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, count + 1, after);
}

// FIXME: Should be able to skip the copy and sample the original surface image
static void copy_surface_to_texture(PGRAPHState *pg, SurfaceBinding *surface,
                                    TextureBinding *texture)
{
    if (!surface->color) {
        copy_zeta_surface_to_texture(pg, surface, texture);
        return;
    }
    if (texture->key.byte_reinterpret) {
        copy_surface_to_linear_texture(pg, surface, texture);
        return;
    }

    PGRAPHVkState *r = pg->vk_renderer_state;

    nv2a_profile_inc_counter(NV2A_PROF_SURF_TO_TEX);

    trace_nv2a_pgraph_surface_render_to_texture(
        surface->vram_addr, surface->width, surface->height);

    VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(pg);
    pgraph_vk_begin_debug_marker(r, cmd, RGBA_GREEN, __func__);

    SurfaceBinding *sources[6] = { surface };
    record_color_surface_copies(pg, cmd, sources, texture, 1);
    texture->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    pgraph_vk_end_debug_marker(r, cmd);
    pgraph_vk_end_nondraw_commands(pg, cmd);

    texture->draw_time = surface->draw_time;
}

/* Cube faces: six per-face render surfaces feed one cubemap, which
 * the hardware samples in place: one GPU batch of layer copies with
 * copy_surface_to_texture's snapshot semantics, not six trips via RAM. */
static void copy_cube_faces_to_texture(PGRAPHState *pg,
                                       SurfaceBinding *faces[6],
                                       TextureBinding *texture)
{
    bool fresh_image = texture->current_layout == VK_IMAGE_LAYOUT_UNDEFINED;
    int max_draw_time = 0;
    unsigned source_mask = 0, copy_mask = 0;
    for (int f = 0; f < 6; f++) {
        if (!faces[f]) continue;
        source_mask |= 1u << f;
        /* Skipped upload layers retain provenance only in the same image. */
        if (fresh_image || !(texture->cube_source_mask & (1u << f)) ||
            texture->cube_source_draw_time[f] != faces[f]->draw_time) {
            copy_mask |= 1u << f;
        }
        texture->cube_source_draw_time[f] = faces[f]->draw_time;
        if (faces[f]->draw_time > max_draw_time) {
            max_draw_time = faces[f]->draw_time;
        }
    }
    texture->draw_time = max_draw_time;
    texture->cube_source_mask = source_mask;
    if (!copy_mask &&
        texture->current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        return;
    }

    PGRAPHVkState *r = pg->vk_renderer_state;
    if (copy_mask) nv2a_profile_inc_counter(NV2A_PROF_SURF_TO_TEX);
    VkCommandBuffer cmd = pgraph_vk_begin_nondraw_commands(pg);
    pgraph_vk_begin_debug_marker(r, cmd, RGBA_GREEN, __func__);

    if (copy_mask) {
        record_color_surface_copies(pg, cmd, faces, texture, copy_mask);
    } else {
        /* Scaled RAM blits still need their final sampling dependency. */
        VkColorFormatInfo vkf = texture_vkf(r, texture->key.state.color_format);
        pgraph_vk_transition_image_layout(
            pg, cmd, texture->image, vkf.vk_format, texture->current_layout,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    texture->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    pgraph_vk_end_debug_marker(r, cmd);
    pgraph_vk_end_nondraw_commands(pg, cmd);
}

static bool check_surface_to_texture_compatiblity(const SurfaceBinding *surface,
                                                  const TextureShape *shape)
{
    /* The expanded host format does not make the guest's packed RGB655
     * bytes compatible with a four-byte render target. */
    if (shape->color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5) {
        return false;
    }
    if ((!surface->swizzle && surface->pitch != shape->pitch) ||
        surface->width != shape->width ||
        surface->height != shape->height ||
        shape->cubemap ||
        shape->levels > 1) {
        return false;
    }

    if (!surface->color) {
        return true;
    }

    VkColorFormatInfo tex_vkf = kelvin_color_format_vk_map[shape->color_format];
    return tex_vkf.vk_format &&
           surface->host_fmt.host_bytes_per_pixel == vk_format_texel_size(tex_vkf.vk_format);
}

/* Return whether index bytes were materialized too. Only combine the reads
 * when the indexed texture cannot take the existing surface-to-texture path.
 * Palette materialization can change dirty/owed state used by cube selection,
 * so possible GPU-only cases retain the original palette-first ordering. */
static bool download_indexed_texture_inputs(
    PGRAPHState *pg, const TextureShape *shape, hwaddr texture_addr,
    size_t texture_size, hwaddr palette_addr, size_t palette_size)
{
    assert(shape->color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8);
    bool needs_ram = shape->levels > 1;
    if (!needs_ram && !shape->cubemap) {
        SurfaceBinding *surface = pgraph_vk_surface_get(
            container_of(pg, NV2AState, pgraph), texture_addr);
        needs_ram = !surface ||
            !check_surface_to_texture_compatiblity(surface, shape);
    }
    if (needs_ram) {
        const MemorySyncRequirement ranges[] = {
            { palette_addr, palette_size }, { texture_addr, texture_size },
        };
        pgraph_vk_download_surface_ranges_if_dirty(pg, ranges,
                                                   ARRAY_SIZE(ranges));
    } else {
        pgraph_vk_download_surfaces_in_range_if_dirty(pg, palette_addr,
                                                     palette_size);
    }
    return needs_ram;
}

static bool can_reinterpret_surface_as_linear(PGRAPHState *pg,
                                              const SurfaceBinding *surface,
                                              const TextureShape *shape)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    unsigned source_bpp, texture_bpp;
    VkFormat source_format;
    /* Only native host representations whose transfer bytes are the guest
     * bytes. XRGB/depth/expanded formats keep their materialization rules. */
    switch (surface->shape.color_format) {
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8:
        source_bpp = 4;
        source_format = VK_FORMAT_B8G8R8A8_UNORM;
        break;
    case NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5:
        source_bpp = 2;
        source_format = VK_FORMAT_R5G6B5_UNORM_PACK16;
        break;
    default:
        return false;
    }
    switch (shape->color_format) {
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8:
        texture_bpp = 1;
        break;
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8:
        texture_bpp = 2;
        break;
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8:
    case NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8:
        texture_bpp = 4;
        break;
    default:
        return false;
    }
    if (!surface->color || surface->swizzle || surface->native_resolution ||
        surface->shape.anti_aliasing != NV097_SET_SURFACE_FORMAT_ANTI_ALIASING_CENTER_1 ||
        surface->host_fmt.vk_format != source_format ||
        shape->dimensionality != 2 || shape->cubemap || shape->border ||
        shape->levels != 1 || !surface->width || !surface->height ||
        (uint64_t)shape->width * texture_bpp !=
            (uint64_t)surface->width * source_bpp ||
        shape->height != surface->height ||
        (uint64_t)surface->pitch < (uint64_t)surface->width * source_bpp ||
        shape->pitch != surface->pitch ||
        r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size <
            (uint64_t)surface->width * source_bpp * surface->height) {
        return false;
    }
    /* A single image must own the whole range. Do not bypass the ordered
     * RAM materialization when another live/parked dirty view overlaps it. */
    SurfaceBinding *other;
    QTAILQ_FOREACH(other, &r->surfaces, entry) {
        if (other != surface && other->draw_dirty &&
            pgraph_vram_ranges_overlap(surface->vram_addr, surface->size,
                other->vram_addr, other->size)) {
            return false;
        }
    }
    QTAILQ_FOREACH(other, &r->invalid_surfaces, entry) {
        if (other != surface && (other->writeback_owed || other->draw_dirty) &&
            pgraph_vram_ranges_overlap(surface->vram_addr, surface->size,
                other->vram_addr, other->size)) {
            return false;
        }
    }
    return pg->surface_scale_factor == 1 ||
        native_get_scratch(pg, surface->host_fmt.vk_format,
                            surface->width, surface->height) != VK_NULL_HANDLE;
}

/* Texture images retired by the CoW rule below or by cache eviction, and
 * surface images (surface.c destroy_surface_image), are kept for reuse
 * instead of freed. A feedback pass retires the same few
 * images many times per frame, and at high scales each one occupies a large
 * part of the fixed image reservation. Semantics of a fresh image: contents
 * undefined, layout UNDEFINED. The pool only keeps what the device can
 * spare: over its heap's budget (VK_EXT_memory_budget, or the heap-size estimate
 * without it), a retired image gives the pool's oldest ones of that heap
 * back first, then itself. */
#define IMAGE_POOL_MAX_BYTES ((size_t)1536 << 20)

static bool image_ci_equal(const VkImageCreateInfo *a,
                           const VkImageCreateInfo *b)
{
    return a->imageType == b->imageType && a->format == b->format &&
           a->extent.width == b->extent.width &&
           a->extent.height == b->extent.height &&
           a->extent.depth == b->extent.depth &&
           a->mipLevels == b->mipLevels && a->arrayLayers == b->arrayLayers &&
           a->samples == b->samples && a->tiling == b->tiling &&
           a->usage == b->usage && a->flags == b->flags &&
           a->sharingMode == b->sharingMode;
}

static void image_pool_drop(PGRAPHVkState *r, int i)
{
    struct XemuVkPooledImage *p = &r->image_pool[i];
    if (p->view) {
        vkDestroyImageView(r->device, p->view, NULL);
    }
    xemu_vk_destroy_image(r->allocator, p->image, p->allocation);
    r->image_pool_bytes -= p->bytes;
    r->image_pool[i] = r->image_pool[--r->image_pool_n];
}

static bool image_pool_heap_over_budget(PGRAPHVkState *r, uint32_t heap)
{
    XemuVkBudget budgets[VK_MAX_MEMORY_HEAPS];

    xemu_vk_get_heap_budgets(r->allocator, budgets);
    return budgets[heap].usage > budgets[heap].budget;
}

bool pgraph_vk_image_pool_put_with_view(PGRAPHVkState *r, VkImage image,
    XemuVkAllocation allocation, const VkImageCreateInfo *ci, VkImageView view)
{
    if (allocation == VK_NULL_HANDLE) {
        return false;
    }
    XemuVkAllocationInfo ai;
    xemu_vk_get_allocation_info(r->allocator, allocation, &ai);
    if (ai.size > IMAGE_POOL_MAX_BYTES / 4) {
        return false;
    }
    const VkPhysicalDeviceMemoryProperties *props;
    xemu_vk_get_memory_properties(r->allocator, &props);
    uint32_t heap = props->memoryTypes[ai.memoryType].heapIndex;
    while (image_pool_heap_over_budget(r, heap)) {
        int oldest = -1;
        for (int i = 0; i < r->image_pool_n; i++) {
            if (r->image_pool[i].heap == heap &&
                (oldest < 0 ||
                 r->image_pool[i].seq < r->image_pool[oldest].seq)) {
                oldest = i;
            }
        }
        if (oldest < 0) {
            return false;
        }
        image_pool_drop(r, oldest);
    }
    while (r->image_pool_n > 0 &&
           (r->image_pool_n == ARRAY_SIZE(r->image_pool) ||
            r->image_pool_bytes + ai.size > IMAGE_POOL_MAX_BYTES)) {
        int oldest = 0;
        for (int i = 1; i < r->image_pool_n; i++) {
            if (r->image_pool[i].seq < r->image_pool[oldest].seq) {
                oldest = i;
            }
        }
        image_pool_drop(r, oldest);
    }
    struct XemuVkPooledImage *p = &r->image_pool[r->image_pool_n++];
    p->image = image;
    p->view = view;
    p->allocation = allocation;
    p->ci = *ci;
    p->bytes = ai.size;
    p->heap = heap;
    p->seq = ++r->image_pool_seq;
    r->image_pool_bytes += ai.size;
    return true;
}

bool pgraph_vk_image_pool_get_with_view(PGRAPHVkState *r,
    const VkImageCreateInfo *ci, VkImage *image, XemuVkAllocation *allocation,
    VkImageView *view)
{
    for (int i = 0; i < r->image_pool_n; i++) {
        if (image_ci_equal(&r->image_pool[i].ci, ci)) {
            *image = r->image_pool[i].image;
            *view = r->image_pool[i].view;
            *allocation = r->image_pool[i].allocation;
            r->image_pool_bytes -= r->image_pool[i].bytes;
            r->image_pool[i] = r->image_pool[--r->image_pool_n];
            return true;
        }
    }
    return false;
}

/* Texture/surface callers create views with their own component mapping and
 * shape. Only overlay callers may reuse the pool's canonical overlay view. */
bool pgraph_vk_image_pool_put(PGRAPHVkState *r, VkImage image,
    XemuVkAllocation allocation, const VkImageCreateInfo *ci)
{
    return pgraph_vk_image_pool_put_with_view(r, image, allocation, ci,
                                             VK_NULL_HANDLE);
}

bool pgraph_vk_image_pool_get(PGRAPHVkState *r, const VkImageCreateInfo *ci,
    VkImage *image, XemuVkAllocation *allocation)
{
    VkImageView view;
    if (!pgraph_vk_image_pool_get_with_view(r, ci, image, allocation, &view)) {
        return false;
    }
    if (view) {
        vkDestroyImageView(r->device, view, NULL);
    }
    return true;
}

void pgraph_vk_image_pool_flush(PGRAPHVkState *r)
{
    while (r->image_pool_n > 0) {
        image_pool_drop(r, r->image_pool_n - 1);
    }
}

/* The sampled view of a texture node's image, from its creation info; the
 * view type follows the guest shape, which a doge logo node keeps. */
static void create_texture_view(PGRAPHVkState *r, TextureBinding *snode,
                                const VkColorFormatInfo *vkf)
{
    const TextureShape *state = &snode->key.state;
    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = snode->image,
        .viewType = state->cubemap ?
            VK_IMAGE_VIEW_TYPE_CUBE :
            dimensionality_to_vk_image_view_type[state->dimensionality],
        .format = snode->image_ci.format,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = snode->image_ci.mipLevels,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = snode->image_ci.arrayLayers,
        .components = vkf->component_map,
    };
    VK_CHECK(vkCreateImageView(r->device, &vci, NULL, &snode->image_view));
}

/* Copy-on-write: while a recorded reference is not retired (the open
 * recording, or a submission past the settled mark), an update moves the
 * image to the trash, where recorded descriptors keep their content, and
 * goes on in a fresh one. open_recording_counts: an aux upload runs ahead
 * of the open recording, so a reference there counts; a surface copy,
 * recorded behind those draws and ordered by its barrier, refills in place.
 * Returns true when a replacement image was allocated. */
static bool texture_cow_if_referenced(PGRAPHState *pg, TextureBinding *snode,
                                      bool open_recording_counts)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (snode->image == VK_NULL_HANDLE) {
        return false;
    }
    bool in_open_recording =
        r->in_command_buffer && snode->submit_time == r->submit_count;
    bool referenced = snode->image_referenced &&
        ((snode->submit_time > r->settled_submit && !in_open_recording) ||
         (open_recording_counts && in_open_recording));
    if (!referenced) {
        return false;
    }

    struct XemuVkTrashEntry e = { .image_view = snode->image_view,
                                  .image = snode->image,
                                  .allocation = snode->allocation,
                                  .image_ci = snode->image_ci,
                                  .pool_ok = true,
                                  .aux_after = r->aux_submitted };
    pgraph_vk_trash_push(r, &e);

    /* The new image is the retired one's twin: its own creation info, not
     * one rebuilt from the guest shape (a doge logo node is larger). */
    TextureShape *state = &snode->key.state;
    VkColorFormatInfo vkf = texture_vkf(
        r, snode->logo ? NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8 :
                         state->color_format);
    VkImageCreateInfo ici = snode->image_ci;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    XemuVkMemoryRequest aci = {
        .host = false,
    };
    if (!pgraph_vk_image_pool_get(r, &ici, &snode->image,
                                  &snode->allocation)) {
        VK_CHECK(xemu_vk_create_image(r->allocator, &ici, &aci, &snode->image,
                                &snode->allocation));
    }
    snode->image_ci = ici;
    create_texture_view(r, snode, &vkf);

    snode->current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    /* Recorded descriptors still refer to the retired image, whose trash
     * entry retains its lifetime. The replacement has no draw references. */
    snode->image_referenced = false;
    snode->seq = ++r->texture_binding_seq;
    return true;
}

static void create_dummy_texture(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent.width = 16,
        .extent.height = 16,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = VK_FORMAT_R8_UNORM,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .flags = 0,
    };

    XemuVkMemoryRequest alloc_create_info = {
        .host = false,
    };

    VkImage texture_image;
    XemuVkAllocation texture_allocation;

    VK_CHECK(xemu_vk_create_image(r->allocator, &image_create_info,
                            &alloc_create_info, &texture_image,
                            &texture_allocation));

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8_UNORM,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = image_create_info.mipLevels,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = image_create_info.arrayLayers,
        .components = (VkComponentMapping){ VK_COMPONENT_SWIZZLE_R,
                                            VK_COMPONENT_SWIZZLE_R,
                                            VK_COMPONENT_SWIZZLE_R,
                                            VK_COMPONENT_SWIZZLE_R },
    };
    VkImageView texture_image_view;
    VK_CHECK(vkCreateImageView(r->device, &image_view_create_info, NULL,
                               &texture_image_view));

    VkSamplerCreateInfo sampler_create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_FALSE,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    };

    VkSampler texture_sampler;
    VK_CHECK(vkCreateSampler(r->device, &sampler_create_info, NULL,
                             &texture_sampler));

    // Copy texture data to the staging ring
    size_t texture_data_size =
        image_create_info.extent.width * image_create_info.extent.height;

    VkCommandBuffer cmd = pgraph_vk_begin_single_time_commands(pg);
    pgraph_vk_begin_debug_marker(r, cmd, RGBA_GREEN, __func__);

    uint8_t *staged;
    VkDeviceSize staged_offset =
        pgraph_vk_staging_alloc(pg, texture_data_size, 16, &staged);
    memset(staged, 0xff, texture_data_size);
    pgraph_vk_staging_flush(pg, staged_offset, texture_data_size);

    /* Host staging writes become visible at the auxiliary queue submission. */

    pgraph_vk_transition_image_layout(
        pg, cmd, texture_image, VK_FORMAT_R8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region = {
        .bufferOffset = staged_offset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount = 1,
        .imageOffset = (VkOffset3D){ 0, 0, 0 },
        .imageExtent = (VkExtent3D){ image_create_info.extent.width,
                                     image_create_info.extent.height, 1 },
    };
    vkCmdCopyBufferToImage(cmd, r->storage_buffers[BUFFER_STAGING_SRC].buffer,
                           texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &region);

    pgraph_vk_transition_image_layout(pg, cmd, texture_image,
                                      VK_FORMAT_R8_UNORM,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    pgraph_vk_end_debug_marker(r, cmd);
    pgraph_vk_end_single_time_commands_async(pg, cmd);

    r->dummy_texture = (TextureBinding){
        .key.scale = 1.0,
        .image = texture_image,
        .current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .allocation = texture_allocation,
        .image_view = texture_image_view,
        .sampler = texture_sampler,
    };
}

static void destroy_dummy_texture(PGRAPHVkState *r)
{
    texture_cache_release_node_resources(r, &r->dummy_texture);
}

static void set_texture_label(PGRAPHState *pg, TextureBinding *texture)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    char label[96];
    g_snprintf(label, sizeof(label),
               "Texture %" HWADDR_PRIx "h fmt:%02xh %dx%dx%d lvls:%d",
               texture->key.texture_vram_offset,
               texture->key.state.color_format, texture->key.state.width,
               texture->key.state.height, texture->key.state.depth,
               texture->key.state.levels);

    VkDebugUtilsObjectNameInfoEXT name_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VK_OBJECT_TYPE_IMAGE,
        .objectHandle = (uint64_t)texture->image,
        .pObjectName = label,
    };

    if (r->debug_utils_extension_enabled) {
        vkSetDebugUtilsObjectNameEXT(r->device, &name_info);
    }
}

static bool is_linear_filter_supported_for_format(PGRAPHVkState *r,
                                                  int kelvin_format)
{
    return r->texture_format_properties[kelvin_format].optimalTilingFeatures &
           VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
}


static VkSampler create_sampler_for_state(PGRAPHState *pg,
                                          const TextureShape *state,
                                          uint32_t filter, uint32_t address,
                                          uint32_t border_color_pack32,
                                          uint32_t max_anisotropy);

static VkSampler sampler_cache_get(PGRAPHState *pg, const TextureShape *state,
                                   uint32_t filter, uint32_t address,
                                   uint32_t border_color_pack32,
                                   uint32_t max_anisotropy)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* memset first: an initializer may leave the padding unspecified, and
     * the memcmp below compares every byte. */
    VkSamplerCacheKey k;
    memset(&k, 0, sizeof(k));
    k.filter = filter;
    k.address = address;
    k.border_color = border_color_pack32;
    k.max_anisotropy = max_anisotropy;
    k.color_format = state->color_format;
    k.dimensionality = state->dimensionality;
    k.levels = state->levels;
    k.min_mip = state->min_mipmap_level;
    k.max_mip = state->max_mipmap_level;
    unsigned h = (unsigned)(fast_hash((const uint8_t *)&k, sizeof(k)) %
                            VK_SAMPLER_CACHE_SIZE);
    for (unsigned i = 0; i < VK_SAMPLER_CACHE_SIZE; i++) {
        unsigned s = (h + i) % VK_SAMPLER_CACHE_SIZE;
        if (!r->sampler_cache[s].used) {
            r->sampler_cache[s].used = true;
            r->sampler_cache[s].key = k;
            r->sampler_cache[s].sampler = create_sampler_for_state(
                pg, state, filter, address, border_color_pack32,
                max_anisotropy);
            r->sampler_cache_n++;
            return r->sampler_cache[s].sampler;
        }
        if (memcmp(&r->sampler_cache[s].key, &k, sizeof(k)) == 0) {
            return r->sampler_cache[s].sampler;
        }
    }
    /* Table full (not expected): a one-off sampler would leak, so the home
     * slot's sampler is reused, degraded, with one warning. */
    static bool warned;
    if (!warned) {
        warned = true;
        fprintf(stderr, "nv2a: vk sampler cache full (%u)\n",
                r->sampler_cache_n);
    }
    return r->sampler_cache[h].sampler;
}

static void sampler_cache_destroy(PGRAPHVkState *r)
{
    for (unsigned i = 0; i < VK_SAMPLER_CACHE_SIZE; i++) {
        if (r->sampler_cache[i].used) {
            vkDestroySampler(r->device, r->sampler_cache[i].sampler, NULL);
            r->sampler_cache[i].used = false;
            r->sampler_cache[i].sampler = VK_NULL_HANDLE;
        }
    }
    r->sampler_cache_n = 0;
}

/* Resolve the unit's sampler for the texture just bound. Both exits of
 * create_texture must call it, the cache hit too, or the unit keeps the
 * sampler of the last texture created on it. */
static void resolve_unit_sampler(PGRAPHState *pg, int texture_idx,
                                 const TextureShape *state, uint32_t filter,
                                 uint32_t address,
                                 uint32_t border_color_pack32,
                                 uint32_t max_anisotropy)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* Sampler state (filter, address, border, anisotropy) stays out of the
     * image key, as on GL: one image serves every sampler it is read with. */
    r->texture_unit_sampler[texture_idx] = sampler_cache_get(
        pg, state, filter, address, border_color_pack32, max_anisotropy);
}

/* Every input comes from the shape and the four sampler registers. */
static VkSampler create_sampler_for_state(PGRAPHState *pg,
                                          const TextureShape *state_in,
                                          uint32_t filter, uint32_t address,
                                          uint32_t border_color_pack32,
                                          uint32_t max_anisotropy)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    const TextureShape state = *state_in;
    BasicColorFormatInfo f_basic =
        kelvin_color_format_info_map[state.color_format];
    VkColorFormatInfo vkf = texture_vkf(r, state.color_format);
    uint32_t mip_levels = f_basic.linear ? 1 : state.levels;

    void *sampler_next_struct = NULL;

    VkSamplerCustomBorderColorCreateInfoEXT custom_border_color_create_info;
    VkBorderColor vk_border_color;

    bool is_integer_type = vkf.vk_format == VK_FORMAT_R32_UINT;

    /* Integer custom colors use UINT32_MAX for a packed channel of 255;
     * Vulkan's integer opaque white uses 1, so it is not equivalent.
     * Transparent black is exact for both types; opaque enums are exact
     * for normalized/float textures only. Keep image and PSO keys unchanged. */
    bool standard_border_color = true;
    if (border_color_pack32 == 0) {
        vk_border_color = is_integer_type ?
            VK_BORDER_COLOR_INT_TRANSPARENT_BLACK :
            VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    } else if (!is_integer_type && border_color_pack32 == 0xff000000) {
        vk_border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    } else if (!is_integer_type && border_color_pack32 == 0xffffffff) {
        vk_border_color = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    } else {
        standard_border_color = false;
    }

    if (!standard_border_color && r->custom_border_color_extension_enabled) {
        vk_border_color = is_integer_type ? VK_BORDER_COLOR_INT_CUSTOM_EXT :
                                            VK_BORDER_COLOR_FLOAT_CUSTOM_EXT;
        custom_border_color_create_info =
            (VkSamplerCustomBorderColorCreateInfoEXT){
                .sType =
                    VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT,
                .format = vkf.vk_format,
                .pNext = sampler_next_struct
            };
        if (is_integer_type) {
            float rgba[4];
            pgraph_argb_pack32_to_rgba_float(border_color_pack32, rgba);
            for (int i = 0; i < 4; i++) {
                custom_border_color_create_info.customBorderColor.uint32[i] =
                    (uint32_t)((double)rgba[i] * (double)0xffffffff);
            }
        } else {
            pgraph_argb_pack32_to_rgba_float(
                border_color_pack32,
                custom_border_color_create_info.customBorderColor.float32);
        }
        sampler_next_struct = &custom_border_color_create_info;
    } else if (!standard_border_color) {
        // FIXME: Handle custom color in shader
        vk_border_color = is_integer_type ?
            VK_BORDER_COLOR_INT_TRANSPARENT_BLACK :
            VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }

    if (filter & NV_PGRAPH_TEXFILTER0_ASIGNED)
        NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_ASIGNED");
    if (filter & NV_PGRAPH_TEXFILTER0_RSIGNED)
        NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_RSIGNED");
    if (filter & NV_PGRAPH_TEXFILTER0_GSIGNED)
        NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_GSIGNED");
    if (filter & NV_PGRAPH_TEXFILTER0_BSIGNED)
        NV2A_UNIMPLEMENTED("NV_PGRAPH_TEXFILTER0_BSIGNED");

    VkFilter vk_min_filter, vk_mag_filter;
    unsigned int mag_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MAG);
    assert(mag_filter < ARRAY_SIZE(pgraph_texture_mag_filter_vk_map));

    unsigned int min_filter = GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIN);
    assert(min_filter < ARRAY_SIZE(pgraph_texture_min_filter_vk_map));

    if (is_linear_filter_supported_for_format(r, state.color_format)) {
        vk_mag_filter = pgraph_texture_min_filter_vk_map[mag_filter];
        vk_min_filter = pgraph_texture_min_filter_vk_map[min_filter];
    } else {
        vk_mag_filter = vk_min_filter = VK_FILTER_NEAREST;
    }

    bool mipmap_en =
        !f_basic.linear &&
        !(min_filter == NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0 ||
          min_filter == NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0 ||
          min_filter == NV_PGRAPH_TEXFILTER0_MIN_CONVOLUTION_2D_LOD0);

    bool mipmap_nearest =
        f_basic.linear || mip_levels == 1 ||
        min_filter == NV_PGRAPH_TEXFILTER0_MIN_BOX_NEARESTLOD ||
        min_filter == NV_PGRAPH_TEXFILTER0_MIN_TENT_NEARESTLOD;

    float lod_bias = pgraph_convert_lod_bias_to_float(
        GET_MASK(filter, NV_PGRAPH_TEXFILTER0_MIPMAP_LOD_BIAS));
    if (lod_bias > r->device_props.limits.maxSamplerLodBias) {
        lod_bias = r->device_props.limits.maxSamplerLodBias;
    } else if (lod_bias < -r->device_props.limits.maxSamplerLodBias) {
        lod_bias = -r->device_props.limits.maxSamplerLodBias;
    }
    if (pgraph_texture_force_anisotropy(&state, filter, address) &&
        is_linear_filter_supported_for_format(r, state.color_format)) {
        max_anisotropy = 16;
    }
    uint32_t sampler_max_anisotropy =
        MIN(r->device_props.limits.maxSamplerAnisotropy, max_anisotropy);

    /* A zero maximum forces magnification in Vulkan. Allow a positive LOD
     * for mixed G8B8 LOD0 filters, while nearest mip selection stays at zero. */
    bool mixed_g8b8_lod0 = mip_levels == 1 && state.dimensionality == 2 &&
        !state.cubemap && !state.border &&
        (min_filter == NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0 ||
         min_filter == NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0 ||
         (state.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8 &&
          min_filter >= NV_PGRAPH_TEXFILTER0_MIN_BOX_NEARESTLOD &&
          min_filter <= NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD)) &&
        vk_min_filter != vk_mag_filter &&
        (state.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8 ||
         state.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8);
    bool mixed_g8b8_clamped = mipmap_en && state.max_mipmap_level == 0 &&
        state.min_mipmap_level == 0 && state.dimensionality == 2 &&
        !state.cubemap && !state.border &&
        vk_min_filter != vk_mag_filter &&
        min_filter >= NV_PGRAPH_TEXFILTER0_MIN_BOX_NEARESTLOD &&
        min_filter <= NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD &&
        state.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8 &&
        GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRU) >= NV_PGRAPH_TEXADDRESS0_ADDRU_WRAP &&
        GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRU) <= NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER &&
        GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRV) >= NV_PGRAPH_TEXADDRESS0_ADDRU_WRAP &&
        GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRV) <= NV_PGRAPH_TEXADDRESS0_ADDRU_BORDER;
    /* Nonmip filters and mip chains clamped to zero must retain the
     * original footprint's min/mag choice while reading only level zero.
     * Fixed D16 uses a filterable image before the shader depth comparison. */
    bool mixed_color_lod0 = (!mipmap_en ||
        (MIN(state.min_mipmap_level, state.levels - 1) == 0 &&
         MIN(state.max_mipmap_level, state.levels - 1) == 0)) &&
        (state.dimensionality == 2 ||
         (state.dimensionality == 3 &&
          !f_basic.linear && !state.cubemap)) &&
        (!state.cubemap || !f_basic.linear) && !state.border &&
        (!f_basic.depth || (state.dimensionality == 2 && !state.cubemap &&
         (state.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FIXED ||
          state.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED))) &&
        vk_min_filter != vk_mag_filter &&
        min_filter >= NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0 &&
        min_filter <= NV_PGRAPH_TEXFILTER0_MIN_TENT_TENT_LOD &&
        state.color_format != NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8 &&
        state.color_format != NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8;
    /* With an intact mip chain, nearest selection is also required: a
     * positive ceiling must not interpolate encoded bytes from mip one. */
    if (mixed_g8b8_clamped || mixed_color_lod0) {
        mipmap_nearest = true;
    }

    VkSamplerCreateInfo sampler_create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = vk_mag_filter,
        .minFilter = vk_min_filter,
        .addressModeU = lookup_texture_address_mode(
            GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRU)),
        .addressModeV = lookup_texture_address_mode(
            GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRV)),
        .addressModeW = (state.dimensionality > 2) ? lookup_texture_address_mode(
            GET_MASK(address, NV_PGRAPH_TEXADDRESS0_ADDRP)) : 0,
        .anisotropyEnable =
            r->enabled_physical_device_features.samplerAnisotropy &&
            sampler_max_anisotropy > 1,
        .maxAnisotropy = sampler_max_anisotropy,
        .borderColor = vk_border_color,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .mipmapMode = mipmap_nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST :
                                       VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .minLod = mipmap_en ? MIN(state.min_mipmap_level, state.levels - 1) : 0.0,
        .maxLod = mixed_g8b8_lod0 || mixed_g8b8_clamped || mixed_color_lod0 ? 0.25f :
                  mipmap_en ? MIN(state.max_mipmap_level, state.levels - 1) : 0.0f,
        .mipLodBias = lod_bias,
        .pNext = sampler_next_struct,
    };

    VkSampler sampler;
    VK_CHECK(vkCreateSampler(r->device, &sampler_create_info, NULL, &sampler));
    return sampler;
}

static void create_texture(PGRAPHState *pg, int texture_idx)
{
    NV2A_VK_DGROUP_BEGIN("Creating texture %d", texture_idx);

    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    TextureShape state = pgraph_get_texture_shape(pg, texture_idx); // FIXME: Check for pad issues
    BasicColorFormatInfo f_basic = kelvin_color_format_info_map[state.color_format];

    const hwaddr texture_vram_offset = pgraph_get_texture_phys_addr(pg, texture_idx);
    size_t texture_length = pgraph_get_texture_length(pg, &state);
    hwaddr texture_palette_vram_offset = 0;
    size_t texture_palette_data_size = 0;

    uint32_t filter =
        pgraph_reg_r(pg, NV_PGRAPH_TEXFILTER0 + texture_idx * 4);
    uint32_t address =
        pgraph_reg_r(pg, NV_PGRAPH_TEXADDRESS0 + texture_idx * 4);
    uint32_t border_color_pack32 =
        pgraph_reg_r(pg, NV_PGRAPH_BORDERCOLOR0 + texture_idx * 4);
    bool is_indexed = (state.color_format ==
            NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8);
    uint32_t max_anisotropy =
        1 << (GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_TEXCTL0_0 + texture_idx*4),
                       NV_PGRAPH_TEXCTL0_0_MAX_ANISOTROPY));

    TextureKey key;
    memset(&key, 0, sizeof(key));
    key.state = state;
    key.texture_vram_offset = texture_vram_offset;
    key.texture_length = texture_length;
    bool texture_ram_materialized = false;
    if (is_indexed) {
        texture_palette_vram_offset =
            pgraph_get_texture_palette_phys_addr_length(
                pg, texture_idx, &texture_palette_data_size);
        key.palette_vram_offset = texture_palette_vram_offset;
        key.palette_length = texture_palette_data_size;
        /* CPU hashing and conversion fallbacks require materialized palette
         * bytes, even when the GPU performs expansion from imported RAM. */
        texture_ram_materialized = download_indexed_texture_inputs(
            pg, &state, texture_vram_offset, texture_length,
            texture_palette_vram_offset, texture_palette_data_size);
    }
    key.scale = 1;
    /* Split samplers: the sampler fields stay zero in the key; the
     * unit's sampler comes from the sampler cache at the end. */

    bool possibly_dirty = false;
    bool possibly_dirty_checked = false;
    bool surface_to_texture = false;
    bool surface_to_texture_cube = false;
    bool mixed_cube = false;
    unsigned cube_mask = 0;
    bool surface_to_texture_bytes = false;
    SurfaceBinding *cube_faces[6] = { 0 };

    // Check active surfaces to see if this texture was a render target
    SurfaceBinding *surface = pgraph_vk_surface_get(d, texture_vram_offset);
    if (surface && state.color_format ==
            NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT &&
        surface->upload_pending && !surface->draw_dirty) {
        surface = NULL;
    }

    if (surface && state.levels == 1) {
        surface_to_texture =
            check_surface_to_texture_compatiblity(surface, &state);
        if (!surface_to_texture) {
            surface_to_texture_bytes =
                can_reinterpret_surface_as_linear(pg, surface, &state);
            surface_to_texture |= surface_to_texture_bytes;
        }

        if (!surface_to_texture && surface->color) {
            trace_nv2a_pgraph_surface_texture_compat_failed(
                surface->shape.color_format,
                state.color_format);
        }

        if (surface_to_texture && surface->upload_pending) {
            pgraph_vk_upload_surface_data(d, surface, false);
        }
    }

    /* Native ARGB cubes can combine disjoint live faces with RAM faces.
     * Other layouts retain the existing all-GPU or RAM fallback paths. */
    if (!surface_to_texture && state.cubemap && state.levels == 1) {
        size_t face_len = texture_length / 6;
        TextureShape flat = state;
        flat.cubemap = false;
        for (unsigned f = 0; f < 6; f++) {
            SurfaceBinding *fs = pgraph_vk_surface_get(
                d, texture_vram_offset + f * face_len);
            if (fs && fs->color &&
                check_surface_to_texture_compatiblity(fs, &flat)) {
                cube_faces[f] = fs;
                cube_mask |= 1u << f;
            }
        }
        bool safe_mixed = cube_mask && cube_mask != 0x3f &&
            state.dimensionality == 2 && !state.border &&
            state.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8 &&
            (pg->surface_scale_factor == 1 ||
             (nv2a_get_force_texture_lod0() && state.levels == 1 &&
              (uint64_t)state.width * pg->surface_scale_factor <=
                  r->device_props.limits.maxImageDimensionCube &&
              (uint64_t)state.height * pg->surface_scale_factor <=
                  r->device_props.limits.maxImageDimensionCube &&
              format_blit_supported(r, texture_vkf(r, state.color_format).vk_format)));
        if (safe_mixed) {
            SurfaceBinding *other;
            QTAILQ_FOREACH(other, &r->surfaces, entry) {
                if (!other->draw_dirty || !pgraph_vram_ranges_overlap(
                        texture_vram_offset, texture_length, other->vram_addr,
                        other->size)) continue;
                bool selected = false;
                for (unsigned f = 0; f < 6; f++) selected |= cube_faces[f] == other;
                if (!selected) safe_mixed = false;
            }
            QTAILQ_FOREACH(other, &r->invalid_surfaces, entry) {
                if (other->writeback_owed && pgraph_vram_ranges_overlap(
                        texture_vram_offset, texture_length, other->vram_addr,
                        other->size)) safe_mixed = false;
            }
            for (unsigned f = 0; f < 6; f++) {
                if (cube_faces[f] &&
                    (cube_faces[f]->size != face_len ||
                     cube_faces[f]->native_resolution ||
                     cube_faces[f]->host_fmt.vk_format !=
                         texture_vkf(r, state.color_format).vk_format))
                    safe_mixed = false;
            }
        }
        mixed_cube = safe_mixed;
        surface_to_texture_cube = cube_mask == 0x3f || mixed_cube;
        if (surface_to_texture_cube) {
            for (unsigned f = 0; f < 6; f++) {
                if (cube_faces[f] && cube_faces[f]->upload_pending)
                    pgraph_vk_upload_surface_data(d, cube_faces[f], false);
            }
        }
    }

    if (!surface_to_texture && !surface_to_texture_cube &&
        !texture_ram_materialized) {
        // FIXME: Restructure to support rendering surfaces to cubemap faces

        // Writeback any surfaces which this texture may index
        pgraph_vk_download_surfaces_in_range_if_dirty(
            pg, texture_vram_offset, texture_length);
    }

    /* Native depth: a sampled zeta surface is served at native 1x:
     * unscaled key and image, texScale 1 for linear formats, as on GL. */
    /* The native depth image exists only if the upload path will have a
     * scratch image to shrink into (a blittable format and a free slot):
     * taking the slot here is what makes the two decisions agree, since a
     * slot, once taken, stays. */
    bool znative_zeta = surface_to_texture && !surface->color &&
                        pg->surface_scale_factor > 1 &&
                        native_get_scratch(pg, surface->host_fmt.vk_format,
                                            surface->width, surface->height) !=
                            VK_NULL_HANDLE;
    if ((surface_to_texture || surface_to_texture_cube) &&
        pg->surface_scale_factor > 1 && !znative_zeta && !surface_to_texture_bytes) {
        key.scale = pg->surface_scale_factor;
    }
    key.byte_reinterpret = surface_to_texture_bytes;

    /* The same eligibility applies to a cache hit: a texture first made
     * from RAM or for feedback need not keep a private copy after its
     * surface becomes available for in-place sampling. */
    bool s2t_borrow =
        surface_to_texture && !surface_to_texture_bytes &&
        surface != r->color_binding &&
        state.dimensionality == 2 && !state.cubemap &&
        texture_vkf(r, state.color_format).vk_format ==
            surface->host_fmt.vk_format;

    uint64_t key_hash = fast_hash((void*)&key, sizeof(key));
    LruNode *node = lru_lookup(&r->texture_cache, key_hash, &key);
    TextureBinding *snode = container_of(node, TextureBinding, node);
    bool binding_found = snode->image != VK_NULL_HANDLE;

    /* In-place sampling: a borrowed node holds only while it aliases the
     * live surface here, not bound as the color target; else rebuilt.
     * A private image can in turn become an alias when it is eligible. */
    if (binding_found &&
        ((snode->s2t_borrowed &&
          (!s2t_borrow || snode->image != surface->image)) ||
         (!snode->s2t_borrowed && s2t_borrow))) {
        /* Recorded draws retain the old view/image through deferred
         * retirement. Invalidate descriptors in either direction, even
         * if the driver recycles a view handle. */
        texture_cache_release_node_resources(r, snode);
        snode->seq = ++r->texture_binding_seq;
        binding_found = false;
    }

    if (binding_found) {
        NV2A_VK_DPRINTF("Cache hit");
        r->texture_bindings[texture_idx] = snode;
        possibly_dirty |= snode->possibly_dirty;
    } else {
        possibly_dirty = true;
    }

    if (!surface_to_texture && (!surface_to_texture_cube || mixed_cube) &&
        !possibly_dirty_checked) {
        possibly_dirty |= check_texture_possibly_dirty(
            d, texture_vram_offset, texture_length, texture_palette_vram_offset,
            texture_palette_data_size);
    }

    // Calculate hash of texture data, if necessary
    void *texture_data = (char*)d->vram_ptr + texture_vram_offset;
    void *palette_data = (char*)d->vram_ptr + texture_palette_vram_offset;

    /* Without a newly observed dirty page or GPU writeback, content hashing
     * runs at most once per frame per binding: bind_textures stamps
     * validation_frame after this call (-1 on a fresh node, which always
     * hashes). The sampler is still resolved on every bind below. */
    bool f16_to_ram = !surface_to_texture &&
        state.color_format == NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT &&
        snode->depth_source_copy;
    uint64_t content_hash = 0;
    bool content_hashed = false;
    bool cube_hash_mask_changed = mixed_cube && cube_mask != snode->cube_source_mask;
    if (!surface_to_texture && (!surface_to_texture_cube || mixed_cube) &&
        (possibly_dirty || f16_to_ram || cube_hash_mask_changed) &&
        (snode->validation_frame != pg->frame_time || f16_to_ram || cube_hash_mask_changed)) {
        content_hash = mixed_cube ?
            hash_mixed_cube_ram(texture_data, texture_length / 6, cube_mask) :
            fast_hash(texture_data, pgraph_texture_hash_length(state, texture_length));
        if (is_indexed) {
            content_hash ^= fast_hash(palette_data, texture_palette_data_size);
        }
        content_hashed = true;
    }

    /* A doge logo node holds only while the guest data is the logo's: new
     * data makes it an ordinary texture again, rebuilt below. */
    if (binding_found && snode->logo && content_hash &&
        content_hash != snode->hash) {
        /* A new seq makes every unit bind it again. */
        texture_cache_release_node_resources(r, snode);
        snode->seq = ++r->texture_binding_seq;
        binding_found = false;
        possibly_dirty = true;
    }

    if (binding_found) {
        if (surface_to_texture_cube) {
            unsigned int maxdt = 0;
            for (int f = 0; f < 6; f++) {
                if (cube_faces[f] && (unsigned int)cube_faces[f]->draw_time > maxdt) {
                    maxdt = cube_faces[f]->draw_time;
                }
            }
            bool ram_changed = mixed_cube && possibly_dirty &&
                snode->validation_frame != pg->frame_time && content_hash != snode->hash;
            bool mask_changed = cube_mask != snode->cube_source_mask;
            bool faces_changed = mask_changed;
            for (unsigned f = 0; f < 6; f++) {
                if (cube_faces[f] && cube_faces[f]->draw_time !=
                    snode->cube_source_draw_time[f]) faces_changed = true;
            }
            if (maxdt != snode->draw_time || faces_changed || ram_changed) {
                bool upload_ram = mixed_cube && (mask_changed || ram_changed);
                bool replaced = texture_cow_if_referenced(pg, snode, upload_ram);
                /* A GPU-only in-place update preserves RAM layers. A fresh
                 * image must have those layers restored before GPU copies. */
                upload_ram |= mixed_cube && replaced;
                if (upload_ram) {
                    upload_mixed_cube_ram(pg, texture_idx, snode, cube_mask);
                    /* A GPU-only COW restore did not rehash unchanged RAM. */
                    if (content_hashed) snode->hash = content_hash;
                }
                copy_cube_faces_to_texture(pg, cube_faces, snode);
            }
        } else if (surface_to_texture && snode->s2t_borrowed) {
            /* Live alias, nothing to copy: record the barrier again after
             * new draws, or if the image left shader-read without one. */
            if (!surface->in_shader_read ||
                surface->draw_time != snode->draw_time) {
                s2t_borrow_sync(pg, surface);
                snode->draw_time = surface->draw_time;
            }
        } else if (surface_to_texture) {
            // FIXME: Add draw time tracking
            if (surface->draw_time != snode->draw_time) {
                texture_cow_if_referenced(pg, snode, false);
                copy_surface_to_texture(pg, surface, snode);
            }
        } else if (snode->validation_frame != pg->frame_time || f16_to_ram) {
            if (f16_to_ram || (possibly_dirty && content_hash != snode->hash)) {
                texture_cow_if_referenced(pg, snode, true);
                upload_texture_image(pg, texture_idx, snode, 0);
                snode->hash = content_hash;
            }
            /* Validated for this frame: dirty pages found later in its RAM
             * mark it again (mark_textures_possibly_dirty). */
            snode->possibly_dirty = false;
        }

        if (surface_to_texture || surface_to_texture_cube) {
            snode->possibly_dirty = false;
        }
        resolve_unit_sampler(pg, texture_idx, &state, filter, address,
                             border_color_pack32, max_anisotropy);

        NV2A_VK_DGROUP_END();
        return;
    }

    NV2A_VK_DPRINTF("Cache miss");

    memcpy(&snode->key, &key, sizeof(key));
    snode->current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    snode->possibly_dirty = false;
    snode->hash = content_hash;

    VkColorFormatInfo vkf = texture_vkf(r, state.color_format);
    assert(vkf.vk_format != 0);
    assert(0 < state.dimensionality);
    assert(state.dimensionality < ARRAY_SIZE(dimensionality_to_vk_image_type));
    assert(state.dimensionality <
           ARRAY_SIZE(dimensionality_to_vk_image_view_type));

    /* DOGE (not upstream): SEGABOOT's "Chihiro" logo becomes the embedded
     * image, the same picture at four times the texel count (see the GL
     * renderer). The cache key keeps the guest's shape; only this image
     * and its upload change. */
    snode->logo = false;
    if (xemu_doge_mode() && content_hash == 0xec2bbfd4787e2b7dULL &&
        !surface_to_texture &&
        state.dimensionality == 2 && !state.cubemap &&
        state.width == 512 && state.height == 128) {
        snode->logo = true;
        state.color_format = NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8;
        state.width = 2048;
        state.height = 512;
        state.levels = 1;
        vkf = texture_vkf(r, state.color_format);
        fprintf(stderr, "nv2a: SEGABOOT logo replaced (vk)\n");
    }

    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = dimensionality_to_vk_image_type[state.dimensionality],
        .extent.width = state.width, // FIXME: Use adjusted size?
        .extent.height = state.height,
        .extent.depth = state.depth,
        .mipLevels = f_basic.linear ? 1 : state.levels,
        .arrayLayers = state.cubemap ? 6 : 1,
        .format = vkf.vk_format,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .flags = (state.cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0),
    };

    if ((surface_to_texture || surface_to_texture_cube) &&
        !znative_zeta && !surface_to_texture_bytes) {
        pgraph_apply_scaling_factor(pg, &image_create_info.extent.width,
                                        &image_create_info.extent.height);
    }

    if (!r->bc_native && !snode->logo && !s2t_borrow &&
        (!state.border || state.cubemap) &&
        image_create_info.extent.width == state.width &&
        image_create_info.extent.height == state.height &&
        pgraph_is_texture_format_compressed(pg, state.color_format) &&
        (size_t)state.width * state.height * state.depth >= 65536 &&
        s3tc_storage_image(r, &image_create_info)) {
        image_create_info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    if (s2t_borrow) {
        snode->image = surface->image;
        snode->allocation = VK_NULL_HANDLE;
        snode->s2t_borrowed = true;
        snode->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else {
        XemuVkMemoryRequest alloc_create_info = {
            .host = false,
        };

        if (!pgraph_vk_image_pool_get(r, &image_create_info, &snode->image,
                                      &snode->allocation)) {
            VK_CHECK(xemu_vk_create_image(r->allocator, &image_create_info,
                                    &alloc_create_info, &snode->image,
                                    &snode->allocation));
        }
    }
    snode->image_ci = image_create_info;
    snode->image_referenced = false;
    create_texture_view(r, snode, &vkf);

    snode->sampler = create_sampler_for_state(pg, &state, filter, address,
                                              border_color_pack32,
                                              max_anisotropy);

    set_texture_label(pg, snode);

    r->texture_bindings[texture_idx] = snode;

    if (surface_to_texture_cube) {
        if (mixed_cube) upload_mixed_cube_ram(pg, texture_idx, snode, cube_mask);
        copy_cube_faces_to_texture(pg, cube_faces, snode);
    } else if (s2t_borrow) {
        s2t_borrow_sync(pg, surface);
        snode->draw_time = surface->draw_time;
    } else if (surface_to_texture) {
        copy_surface_to_texture(pg, surface, snode);
    } else {
        upload_texture_image(pg, texture_idx, snode, 0);
        snode->draw_time = 0;
    }

    resolve_unit_sampler(pg, texture_idx, &state, filter, address,
                         border_color_pack32, max_anisotropy);

    NV2A_VK_DGROUP_END();
}

static bool check_textures_dirty(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (!r->texture_bindings[i] || pg->texture_dirty[i]) {
            return true;
        }
        /* Newly observed dirty pages reset validation_frame, including CPU
         * changes found after an earlier bind in the same frame. */
        if (r->texture_bindings[i]->possibly_dirty &&
            r->texture_bindings[i]->validation_frame != pg->frame_time) {
            return true;
        }
        /* CPU writes can change texels without any texture method being
         * resent. Checking pages also marks other cached views sharing them
         * before their dirty flags are cleared. Clean pages need no hash. */
        if (pgraph_is_texture_enabled(pg, i)) {
            const TextureKey *key = &r->texture_bindings[i]->key;
            if (check_texture_possibly_dirty(d, key->texture_vram_offset,
                    key->texture_length, key->palette_vram_offset,
                    key->palette_length)) {
                return true;
            }
        }
    }
    return false;
}

static void update_timestamps(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    for (int i = 0; i < ARRAY_SIZE(r->texture_bindings); i++) {
        if (r->texture_bindings[i]) {
            r->texture_bindings[i]->submit_time = r->submit_count;
            r->texture_bindings[i]->image_referenced = true;
            r->texture_bindings[i]->sampled_frame = pg->frame_time;
        }
    }
}

void pgraph_vk_bind_textures(NV2AState *d)
{
    NV2A_VK_DGROUP_BEGIN("%s", __func__);

    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->texture_bindings_changed = false;

    if (!check_textures_dirty(d)) {
        NV2A_VK_DPRINTF("Not dirty");
        NV2A_VK_DGROUP_END();
        update_timestamps(pg);
        return;
    }

    /* Games re-send identical texture state before every draw: report a
     * change only if the bound instance differs. Node pointers and Vulkan
     * handles are both recycled, so the creation sequence id is compared. */
    TextureBinding *old_bindings[NV2A_MAX_TEXTURES];
    uint64_t old_seqs[NV2A_MAX_TEXTURES];
    VkSampler old_samplers[NV2A_MAX_TEXTURES];
    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        old_bindings[i] = r->texture_bindings[i];
        old_seqs[i] = r->texture_bindings[i] ? r->texture_bindings[i]->seq : 0;
        /* Split samplers: a filter/wrap change alone keeps the image
         * node (same seq) but must still rewrite the descriptors. */
        old_samplers[i] = r->texture_unit_sampler[i];
    }

    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (!pgraph_is_texture_enabled(pg, i)) {
            r->texture_bindings[i] = &r->dummy_texture;
            r->texture_unit_sampler[i] = r->dummy_texture.sampler;
            /* Cleared so a unit left off does not rerun this loop. */
            pg->texture_dirty[i] = false;
            continue;
        }

        create_texture(pg, i);
        r->texture_bindings[i]->validation_frame = pg->frame_time;

        pg->texture_dirty[i] = false; // FIXME: Move to renderer?
    }

    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        if (r->texture_bindings[i] != old_bindings[i] ||
            r->texture_bindings[i]->seq != old_seqs[i] ||
            r->texture_unit_sampler[i] != old_samplers[i]) {
            r->texture_bindings_changed = true;
            break;
        }
    }
    update_timestamps(pg);
    NV2A_VK_DGROUP_END();
}

static void texture_cache_entry_init(Lru *lru, LruNode *node, const void *state)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, texture_cache);
    TextureBinding *snode = container_of(node, TextureBinding, node);

    snode->image = VK_NULL_HANDLE;
    snode->allocation = VK_NULL_HANDLE;
    snode->image_view = VK_NULL_HANDLE;
    snode->sampler = VK_NULL_HANDLE;
    /* Entries come from a non-zeroed g_malloc_n, and the release path
     * reads this flag even on a node that never borrowed. */
    snode->s2t_borrowed = false;
    snode->submit_time = 0;
    snode->image_referenced = false;
    snode->seq = ++r->texture_binding_seq;
    snode->validation_frame = (unsigned int)-1;
    snode->sampled_frame = 0;
    snode->cube_source_mask = 0;
    snode->depth_source_copy = false;
}

static void texture_cache_release_node_resources(PGRAPHVkState *r, TextureBinding *snode)
{
    /* In-place sampling: a borrowed image belongs to its surface; only the
     * view and sampler are released (a borrowed node has no allocation). */
    snode->cube_source_mask = 0;
    snode->depth_source_copy = false;
    bool borrowed = snode->s2t_borrowed &&
                    snode->allocation == VK_NULL_HANDLE;
    snode->s2t_borrowed = false;

    /* In-flight draws or auxiliary uploads may still use this binding:
     * retain its resources until both submission domains have retired. */
    struct XemuVkTrashEntry e = { .sampler = snode->sampler,
                                  .image_view = snode->image_view,
                                  .image = borrowed ? VK_NULL_HANDLE :
                                                      snode->image,
                                  .allocation = borrowed ?
                                      VK_NULL_HANDLE : snode->allocation,
                                  .image_ci = snode->image_ci,
                                  .pool_ok = !borrowed,
                                  .aux_after = r->aux_submitted };
    pgraph_vk_trash_push(r, &e);
    snode->sampler = VK_NULL_HANDLE;
    snode->image_view = VK_NULL_HANDLE;
    snode->image = VK_NULL_HANDLE;
    snode->allocation = VK_NULL_HANDLE;
    snode->image_referenced = false;
}

static bool texture_cache_entry_pre_evict(Lru *lru, LruNode *node)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, texture_cache);
    TextureBinding *snode = container_of(node, TextureBinding, node);

    // FIXME: Simplify. We don't really need to check bindings


    // Currently bound
    for (int i = 0; i < ARRAY_SIZE(r->texture_bindings); i++) {
        if (r->texture_bindings[i] == snode) {
            return false;
        }
    }

    // Used in command buffer
    if (snode->image_referenced && r->in_command_buffer &&
        snode->submit_time == r->submit_count) {
        return false;
    }

    return true;
}

static void texture_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, texture_cache);
    TextureBinding *snode = container_of(node, TextureBinding, node);
    texture_cache_release_node_resources(r, snode);
}

static bool texture_cache_entry_compare(Lru *lru, LruNode *node,
                                        const void *key)
{
    TextureBinding *snode = container_of(node, TextureBinding, node);
    return memcmp(&snode->key, key, sizeof(TextureKey));
}

static void texture_cache_init(PGRAPHVkState *r)
{
    const size_t texture_cache_size = 1024;
    lru_init(&r->texture_cache);
    r->texture_cache_entries = g_malloc_n(texture_cache_size, sizeof(TextureBinding));
    assert(r->texture_cache_entries != NULL);
    for (int i = 0; i < texture_cache_size; i++) {
        lru_add_free(&r->texture_cache, &r->texture_cache_entries[i].node);
    }
    r->texture_cache.init_node = texture_cache_entry_init;
    r->texture_cache.compare_nodes = texture_cache_entry_compare;
    r->texture_cache.pre_node_evict = texture_cache_entry_pre_evict;
    r->texture_cache.post_node_evict = texture_cache_entry_post_evict;
}

static void texture_cache_finalize(PGRAPHVkState *r)
{
    lru_flush(&r->texture_cache);
    g_free(r->texture_cache_entries);
    r->texture_cache_entries = NULL;
}

void pgraph_vk_trim_texture_cache(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    // FIXME: Allow specifying some amount to trim by

    int num_to_evict = r->texture_cache.num_used / 4;
    int num_evicted = 0;

    while (num_to_evict-- && lru_try_evict_one(&r->texture_cache)) {
        num_evicted += 1;
    }

    NV2A_VK_DPRINTF("Evicted %d textures, %d remain", num_evicted, r->texture_cache.num_used);
}

void pgraph_vk_init_textures(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    texture_cache_init(r);

    /* BC where the device has it for all three formats */
    r->bc_native = r->enabled_physical_device_features.textureCompressionBC;
    static const VkFormat bc_formats[] = { VK_FORMAT_BC1_RGBA_UNORM_BLOCK,
                                           VK_FORMAT_BC2_UNORM_BLOCK,
                                           VK_FORMAT_BC3_UNORM_BLOCK };
    for (int i = 0; i < ARRAY_SIZE(bc_formats) && r->bc_native; i++) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(r->physical_device, bc_formats[i],
                                            &props);
        const VkFormatFeatureFlags need =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((props.optimalTilingFeatures & need) != need) {
            r->bc_native = false;
        }
    }

    r->s3tc_decode.enabled = !r->bc_native &&
        r->device_props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU;

    create_dummy_texture(pg);

    /* the host formats' properties, by guest format */
    r->texture_format_properties = g_malloc0_n(
        ARRAY_SIZE(kelvin_color_format_vk_map), sizeof(VkFormatProperties));
    for (int i = 0; i < ARRAY_SIZE(kelvin_color_format_vk_map); i++) {
        vkGetPhysicalDeviceFormatProperties(
            r->physical_device, texture_vkf(r, i).vk_format,
            &r->texture_format_properties[i]);
    }
}

void pgraph_vk_finalize_textures(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(!r->in_command_buffer);

    for (int i = 0; i < NV2A_MAX_TEXTURES; i++) {
        r->texture_bindings[i] = NULL;
    }

    s3tc_decode_finalize(pg);
    destroy_dummy_texture(r);
    texture_cache_finalize(r);
    sampler_cache_destroy(r);
    for (int i = 0; i < ARRAY_SIZE(r->znative_scratch); i++) {
        if (r->znative_scratch[i].image != VK_NULL_HANDLE) {
            xemu_vk_destroy_image(r->allocator, r->znative_scratch[i].image,
                            r->znative_scratch[i].allocation);
            r->znative_scratch[i].image = VK_NULL_HANDLE;
        }
    }

    assert(r->texture_cache.num_used == 0);

    g_free(r->texture_format_properties);
    r->texture_format_properties = NULL;
}
