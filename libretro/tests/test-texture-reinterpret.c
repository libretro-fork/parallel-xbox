/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "deps/vulkan-headers/vulkan/vulkan_core.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/vram-ranges.h"
typedef struct SurfaceBinding {
    bool color, swizzle, native_resolution, draw_dirty, writeback_owed;
    struct { unsigned color_format, anti_aliasing; } shape;
    struct { VkFormat vk_format; } host_fmt;
    unsigned width, height, pitch;
    uint64_t vram_addr, size;
    struct SurfaceBinding *next;
} SurfaceBinding;
typedef struct { SurfaceBinding *first; } List;
typedef struct { struct { uint64_t buffer_size; } storage_buffers[1]; List surfaces, invalid_surfaces; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; unsigned surface_scale_factor; } PGRAPHState;
typedef struct { unsigned color_format, dimensionality, levels, width, height, pitch; bool cubemap, border; } TextureShape;
#define BUFFER_COMPUTE_DST 0
#define QTAILQ_FOREACH(v,h,f) for ((v)=(h)->first; (v); (v)=(v)->next)
static bool scratch_available = true;
static VkImage native_get_scratch(PGRAPHState *pg, VkFormat f, unsigned w, unsigned h)
{
    (void)pg; (void)f; (void)w; (void)h;
    return scratch_available ? (VkImage)(uintptr_t)1 : VK_NULL_HANDLE;
}
/* PRODUCTION */
static void check_format(unsigned format, unsigned source_bpp, unsigned texture_bpp)
{
    scratch_available = true;
    SurfaceBinding source = {.color=true, .shape.color_format=source_bpp == 4 ? NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8 : NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5,
        .host_fmt.vk_format=source_bpp == 4 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R5G6B5_UNORM_PACK16, .width=32, .height=16, .pitch=32 * source_bpp,
        .vram_addr=0x10000, .size=32 * source_bpp * 16};
    TextureShape shape = {.color_format=format,
        .dimensionality=2, .levels=1, .width=32 * source_bpp / texture_bpp, .height=16, .pitch=32 * source_bpp};
    PGRAPHVkState renderer = {.storage_buffers={{2048}}, .surfaces={&source}};
    PGRAPHState pg = {&renderer,1};
    assert(can_reinterpret_surface_as_linear(&pg,&source,&shape));
#define REJECT_FIELD(object,field,value) do { __typeof__((object).field) old=(object).field; (object).field=(value); assert(!can_reinterpret_surface_as_linear(&pg,&source,&shape)); (object).field=old; } while(0)
    REJECT_FIELD(source,color,false);
    REJECT_FIELD(source,swizzle,true);
    REJECT_FIELD(source,native_resolution,true);
    REJECT_FIELD(source,shape.color_format,0);
    REJECT_FIELD(source,shape.color_format,NV097_SET_SURFACE_FORMAT_COLOR_LE_X8R8G8B8_Z8R8G8B8);
    REJECT_FIELD(source,shape.color_format,NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5);
    REJECT_FIELD(source,shape.anti_aliasing,1);
    REJECT_FIELD(source,host_fmt.vk_format,VK_FORMAT_R8G8B8A8_UNORM);
    REJECT_FIELD(shape,color_format,NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED);
    REJECT_FIELD(shape,color_format,NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5);
    REJECT_FIELD(shape,color_format,NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8);
    REJECT_FIELD(shape,dimensionality,3);
    REJECT_FIELD(shape,cubemap,true);
    REJECT_FIELD(shape,border,true);
    REJECT_FIELD(shape,levels,2);
    REJECT_FIELD(source,width,0);
    REJECT_FIELD(source,height,0);
    REJECT_FIELD(source,width,UINT32_MAX);
    REJECT_FIELD(source,pitch,132);
    REJECT_FIELD(shape,width,shape.width - 1);
    REJECT_FIELD(shape,height,15);
    REJECT_FIELD(shape,pitch,132);
#ifdef TEST_VULKAN_LINEAR
    /* Both views omit the same padding; only active pixels need storage. */
    source.pitch = shape.pitch = 256;
    source.size = 4096;
    assert(can_reinterpret_surface_as_linear(&pg,&source,&shape));
    source.pitch = shape.pitch = 32 * source_bpp - 1;
    assert(!can_reinterpret_surface_as_linear(&pg,&source,&shape));
    source.pitch = shape.pitch = 32 * source_bpp;
    source.size = 32 * source_bpp * 16;
#endif
    renderer.storage_buffers[0].buffer_size=32 * source_bpp * 16 - 1;
    assert(!can_reinterpret_surface_as_linear(&pg,&source,&shape));
    renderer.storage_buffers[0].buffer_size=2048;
    for (pg.surface_scale_factor=1; pg.surface_scale_factor<=4; pg.surface_scale_factor*=2)
        assert(can_reinterpret_surface_as_linear(&pg,&source,&shape));
    pg.surface_scale_factor=2; scratch_available=false;
    assert(!can_reinterpret_surface_as_linear(&pg,&source,&shape));
    pg.surface_scale_factor=1;
    assert(can_reinterpret_surface_as_linear(&pg,&source,&shape));
    SurfaceBinding overlap = {.draw_dirty=true,.vram_addr=source.vram_addr+source.size-1,.size=32};
    source.next=&overlap;
    assert(!can_reinterpret_surface_as_linear(&pg,&source,&shape));
    overlap.vram_addr++;
    assert(can_reinterpret_surface_as_linear(&pg,&source,&shape));
    overlap.vram_addr=source.vram_addr-1;
    assert(!can_reinterpret_surface_as_linear(&pg,&source,&shape));
    overlap.draw_dirty=false;
    assert(can_reinterpret_surface_as_linear(&pg,&source,&shape));
    source.next=NULL; renderer.invalid_surfaces.first=&overlap;
    overlap.writeback_owed=true;
    assert(!can_reinterpret_surface_as_linear(&pg,&source,&shape));
    overlap.writeback_owed=false;
    assert(can_reinterpret_surface_as_linear(&pg,&source,&shape));
    overlap.draw_dirty=true;
    assert(!can_reinterpret_surface_as_linear(&pg,&source,&shape));
    overlap.vram_addr = source.vram_addr + 1;
    overlap.size = 0;
    assert(can_reinterpret_surface_as_linear(&pg,&source,&shape));
}

int main(void)
{
    check_format(NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5, 4, 2);
#ifdef TEST_VULKAN_LINEAR
    const unsigned formats[] = {
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8,
        NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8,
    };
    for (unsigned source_bpp = 2; source_bpp <= 4; source_bpp *= 2)
    for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); f++)
        check_format(formats[f], source_bpp, f < 3 ? 1 : f < 9 ? 2 : 4);
#endif
    puts("texture reinterpretation gate: formats, layouts, bounds, capacity, scratch and dirty-overlap cases passed");
    return 0;
}
