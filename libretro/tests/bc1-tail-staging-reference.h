/* Frozen stage_blocks_3d from 3b8b00a; LGPL-2.1-or-later. */
static void baseline_stage_blocks_3d(PGRAPHState *pg, TextureLevel *level,
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
    if (block_size == 8 && blocks >= 4096) {
        /* Large cached BC1 slices amortize specialization. Keep the block
         * size decision and source/destination bases outside the copy loop. */
        for (unsigned z0 = 0; z0 < depth; z0 += 4) {
            unsigned nz = MIN(depth - z0, 4);
            size_t stride = (size_t)nz * 8;
            for (unsigned z = 0; z < nz; z++) {
                const uint8_t *src = data + ((size_t)z0 * blocks + z) * 8;
                uint8_t *out = dst + (size_t)(z0 + z) * slice;
                for (unsigned b = 0; b < blocks; b++) {
                    memcpy(out + (size_t)b * 8, src + (size_t)b * stride, 8);
                }
            }
        }
        return;
    }
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
