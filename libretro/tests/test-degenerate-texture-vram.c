/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vulkan/vulkan_core.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef uint64_t hwaddr;
typedef struct { VkBuffer vram_buffer; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; uint8_t *arena; size_t head; } PGRAPHState;
typedef struct { uint8_t *vram_ptr; size_t *vram; PGRAPHState pgraph; } NV2AState;
typedef struct { unsigned color_format; } TextureShape;
#define memory_region_size(p) (*(p))
static unsigned staging_calls, compute_calls;
static bool gpu_available;
static VkDeviceSize pgraph_vk_staging_alloc(PGRAPHState *pg, size_t bytes,
                                           size_t align, uint8_t **out)
{
    size_t offset = (pg->head + align - 1) & ~(align - 1);
    pg->head = offset + bytes; *out = pg->arena + offset;
    staging_calls++;
    return offset;
}
static size_t pgraph_converted_texture_size(TextureShape s, unsigned w, unsigned h, unsigned d)
{ (void)s; (void)w; (void)h; (void)d; return 0; }
static bool pgraph_vk_unswizzle(PGRAPHState *p, VkCommandBuffer c, hwaddr src,
    unsigned w, unsigned h, unsigned d, unsigned bpp, hwaddr palette, VkDeviceSize *out)
{
    (void)p; (void)c; (void)src; (void)w; (void)h; (void)d; (void)bpp; (void)palette;
    compute_calls++;
    *out = 77;
    return gpu_available;
}
static void pgraph_convert_swizzled_rgb655_into(const uint8_t *s, unsigned w,
    unsigned h, bool vk, uint8_t *dst)
{ (void)s; (void)w; (void)h; (void)vk; (void)dst; abort(); }
/* PRODUCTION */
int main(void)
{
    const unsigned dimensions[] = {1, 2, 4, 16};
    unsigned cases = 0, direct_cases = 0;
    for (unsigned x = 0; x < 4; x++) for (unsigned y = 0; y < 4; y++)
    for (unsigned z = 0; z < 4; z++) for (unsigned bpp = 1; bpp <= 4; bpp *= 2)
    for (unsigned kind = 0; kind < 5; kind++) for (unsigned gpu = 0; gpu < 2; gpu++) {
        unsigned w = dimensions[x], h = dimensions[y], d = dimensions[z];
        size_t bytes = (size_t)w * h * d * bpp;
        uint8_t *guest = malloc(bytes + 128), *linear = malloc(bytes), *staged = malloc(bytes + 64);
        assert(guest && linear && staged);
        memset(guest, 0xab, bytes + 128); memset(staged, 0xcd, bytes + 64);
        for (size_t n = 0; n < bytes; n++) linear[n] = n * 37 + n / 11;
        unsigned offset = kind == 2 ? 33 : 32;
        swizzle_box(linear, w, h, d, guest + offset, w * bpp, w * h * bpp, bpp);
        size_t vram_size = kind == 3 ? offset + bytes - 1 :
                           kind == 4 ? offset + bytes : bytes + 128;
        PGRAPHVkState r = {.vram_buffer = kind ? (VkBuffer)(uintptr_t)1 : VK_NULL_HANDLE};
        NV2AState dev = {.vram_ptr = guest, .vram = &vram_size, .pgraph = {&r, staged, 3}};
        TextureShape shape = {.color_format = bpp == 1 ? NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8 :
            bpp == 2 ? NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5 : NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8};
        TextureLevel level;
        staging_calls = compute_calls = 0;
        gpu_available = gpu && kind != 0 && kind != 3;
        stage_swizzled(&dev.pgraph, VK_NULL_HANDLE, &level, &shape, guest + offset,
                        NULL, w, h, d, bpp);
        bool linear_order = (x != 0) + (y != 0) + (z != 0) <= 1;
        bool direct = linear_order && kind && kind != 2 && kind != 3;
        assert(level.width == w && level.height == h && level.depth == d && level.size == bytes);
        assert(!level.row_length && !level.crop_offset);
        if (direct) {
            assert(level.source == LEVEL_VRAM && level.offset == offset);
            assert(!staging_calls && !compute_calls && dev.pgraph.head == 3);
            assert(!memcmp(guest + level.offset, linear, bytes));
            direct_cases++;
        } else if (gpu_available) {
            assert(level.source == LEVEL_UNSWIZZLED && level.offset == 77);
            assert(!staging_calls && compute_calls == 1 && dev.pgraph.head == 3);
        } else {
            assert(level.source == LEVEL_STAGING && level.offset == 16);
            assert(staging_calls == 1 && compute_calls == 1 && dev.pgraph.head == bytes + 16);
            assert(!memcmp(staged + 16, linear, bytes));
        }
        for (unsigned n = 0; n < 16; n++) assert(staged[n] == 0xcd);
        for (size_t n = bytes + 16; n < bytes + 64; n++) assert(staged[n] == 0xcd);
        free(guest); free(linear); free(staged); cases++;
    }
    printf("degenerate swizzle: %u cases, %u direct VRAM levels without staging or compute\n", cases, direct_cases);
}
