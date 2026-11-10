/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_VK_S3TC_STREAM_H
#define NV2A_VK_S3TC_STREAM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Metadata only. The caller owns source RAM and the final mapped destination;
 * neither lend is retained here. Xbox volume blocks interleave up to four Z
 * slices per XY block. One block row can therefore be decoded independently,
 * including a bounded horizontal subset when the destination is small. */
typedef struct S3tcStream {
    unsigned width, height, depth, block_bytes;
    unsigned x, y, z;
} S3tcStream;

typedef struct S3tcStreamTile {
    unsigned x, y, z, width, height, depth;
    size_t source_offset, source_bytes, decoded_bytes;
} S3tcStreamTile;

static bool s3tc_stream_init(S3tcStream *s, unsigned width, unsigned height,
                            unsigned depth, unsigned block_bytes,
                            size_t source_size)
{
    /* Bound this streaming path to 65536 per axis. Unsupported dimensions
     * stay on the existing path; reject them before multiplication. */
    if (!width || !height || !depth || width > 65536 || height > 65536 ||
        depth > 65536 || (block_bytes != 8 && block_bytes != 16)) return false;
    uint64_t size = (uint64_t)((width + 3) / 4) * ((height + 3) / 4) *
                    depth * block_bytes;
    if (size > source_size) return false;
    *s = (S3tcStream){width, height, depth, block_bytes, 0, 0, 0};
    return true;
}

/* False leaves the cursor unchanged: exhausted, or insufficient capacity for
 * one block's visible pixels. No allocation, source read or payload copy. */
static bool s3tc_stream_next(S3tcStream *s, size_t capacity,
                            S3tcStreamTile *tile)
{
    if (s->z == s->depth) return false;
    unsigned h = s->height - s->y, d = s->depth - s->z;
    if (h > 4) h = 4;
    if (d > 4) d = 4;
    size_t pixel_column = (size_t)h * d * 4;
    size_t columns = capacity / pixel_column;
    unsigned w = s->width - s->x;
    if (columns < w) {
        columns -= columns % 4;
        if (!columns) return false;
        w = (unsigned)columns;
    }
    uint64_t blocks_x = (s->width + 3) / 4;
    uint64_t blocks_y = (s->height + 3) / 4;
    uint64_t offset = ((uint64_t)s->z * blocks_x * blocks_y +
                       ((uint64_t)(s->y / 4) * blocks_x + s->x / 4) * d) *
                      s->block_bytes;
    uint64_t bytes = (uint64_t)((w + 3) / 4) * d * s->block_bytes;
    if (offset > SIZE_MAX || bytes > SIZE_MAX - offset) return false;
    *tile = (S3tcStreamTile){s->x, s->y, s->z, w, h, d,
                           (size_t)offset, (size_t)bytes, w * pixel_column};
    s->x += w;
    if (s->x == s->width) {
        s->x = 0;
        s->y += h;
        if (s->y == s->height) {
            s->y = 0;
            s->z += d;
        }
    }
    return true;
}
#endif
