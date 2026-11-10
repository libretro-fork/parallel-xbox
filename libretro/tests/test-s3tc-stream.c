/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/s3tc.h"
#include "hw/xbox/nv2a/pgraph/vk/s3tc-stream.h"

static uint8_t source[1 << 20], reference[1 << 21], decoded[4096 + 32];
static uint8_t covered[1 << 19];
static unsigned cases, tiles;

static void run(unsigned w, unsigned h, unsigned d, unsigned fmt, size_t capacity)
{
    unsigned block = fmt == S3TC_DECOMPRESS_FORMAT_DXT1 ? 8 : 16;
    size_t bytes = (size_t)((w + 3) / 4) * ((h + 3) / 4) * d * block;
    size_t pixels = (size_t)w * h * d;
    assert(bytes <= sizeof(source) && pixels * 4 <= sizeof(reference));
    assert(pixels <= sizeof(covered));
    s3tc_decompress_3d_into(fmt, source, w, h, d, reference);
    memset(covered, 0, pixels);
    S3tcStream s;
    assert(s3tc_stream_init(&s, w, h, d, block, bytes));
    S3tcStreamTile t;
    while (s3tc_stream_next(&s, capacity, &t)) {
        assert(t.decoded_bytes <= capacity && t.source_offset <= bytes);
        assert(t.source_bytes <= bytes - t.source_offset);
        memset(decoded, 0xa7, sizeof(decoded));
        s3tc_decompress_3d_into(fmt, source + t.source_offset,
                               t.width, t.height, t.depth, decoded + 16);
        for (unsigned i = 0; i < 16; i++) {
            assert(decoded[i] == 0xa7 && decoded[16 + t.decoded_bytes + i] == 0xa7);
        }
        for (unsigned z = 0; z < t.depth; z++) {
            for (unsigned y = 0; y < t.height; y++) {
                size_t dst = ((size_t)(t.z + z) * h + t.y + y) * w + t.x;
                size_t src = ((size_t)z * t.height + y) * t.width;
                assert(!memcmp(reference + dst * 4, decoded + 16 + src * 4, t.width * 4));
                for (unsigned x = 0; x < t.width; x++) assert(!covered[dst + x]++);
            }
        }
        tiles++;
    }
    assert(s.z == d);
    for (size_t i = 0; i < pixels; i++) assert(covered[i] == 1);
    S3tcStream before = s;
    assert(!s3tc_stream_next(&s, capacity, &t) && !memcmp(&s, &before, sizeof(s)));
    cases++;
}

int main(void)
{
    uint32_t random = 0x1979;
    for (size_t i = 0; i < sizeof(source); i++) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        source[i] = (uint8_t)random;
    }
    const unsigned widths[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 65, 129};
    const unsigned heights[] = {1, 2, 3, 4, 5, 7, 8, 9, 19};
    const unsigned depths[] = {1, 2, 3, 4, 5, 7, 8, 9};
    const unsigned capacities[] = {256, 272, 1024, 4096};
    for (unsigned f = 0; f < 3; f++)
        for (unsigned w = 0; w < sizeof(widths)/sizeof(*widths); w++)
            for (unsigned h = 0; h < sizeof(heights)/sizeof(*heights); h++)
                for (unsigned d = 0; d < sizeof(depths)/sizeof(*depths); d++)
                    for (unsigned c = 0; c < sizeof(capacities)/sizeof(*capacities); c++)
                        run(widths[w], heights[h], depths[d], f, capacities[c]);
    S3tcStream s, before; S3tcStreamTile t;
    assert(!s3tc_stream_init(&s, 0, 4, 4, 8, 32));
    assert(!s3tc_stream_init(&s, 65537, 4, 4, 8, SIZE_MAX));
    assert(!s3tc_stream_init(&s, 4, 4, 4, 12, 48));
    assert(!s3tc_stream_init(&s, 4, 4, 4, 8, 31));
    assert(s3tc_stream_init(&s, 4096, 4096, 4096, 16, (size_t)1 << 36));
    assert(s3tc_stream_next(&s, 256, &t) && t.decoded_bytes == 256);
    before = s;
    assert(!s3tc_stream_next(&s, 255, &t) && !memcmp(&s, &before, sizeof(s)));
    printf("S3TC bounded stream: %u exact full-level comparisons, %u tiles; guards, coverage, capacity and source bounds PASS\n", cases, tiles);
    return 0;
}
