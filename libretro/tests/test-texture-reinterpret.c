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
int main(void)
{
    SurfaceBinding source = {.color=true, .shape.color_format=NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8,
        .host_fmt.vk_format=VK_FORMAT_B8G8R8A8_UNORM, .width=32, .height=16, .pitch=128,
        .vram_addr=0x10000, .size=2048};
    TextureShape shape = {.color_format=NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5,
        .dimensionality=2, .levels=1, .width=64, .height=16, .pitch=128};
    PGRAPHVkState renderer = {.storage_buffers={{2048}}, .surfaces={&source}};
    PGRAPHState pg = {&renderer,1};
    assert(can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
#define REJECT_FIELD(object,field,value) do { __typeof__((object).field) old=(object).field; (object).field=(value); assert(!can_reinterpret_argb_as_rgb565(&pg,&source,&shape)); (object).field=old; } while(0)
    REJECT_FIELD(source,color,false);
    REJECT_FIELD(source,swizzle,true);
    REJECT_FIELD(source,native_resolution,true);
    REJECT_FIELD(source,shape.color_format,0);
    REJECT_FIELD(source,shape.anti_aliasing,1);
    REJECT_FIELD(source,host_fmt.vk_format,VK_FORMAT_R8G8B8A8_UNORM);
    REJECT_FIELD(shape,color_format,NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8);
    REJECT_FIELD(shape,dimensionality,3);
    REJECT_FIELD(shape,cubemap,true);
    REJECT_FIELD(shape,border,true);
    REJECT_FIELD(shape,levels,2);
    REJECT_FIELD(source,width,0);
    REJECT_FIELD(source,height,0);
    REJECT_FIELD(source,width,UINT32_MAX);
    REJECT_FIELD(source,pitch,132);
    REJECT_FIELD(shape,width,63);
    REJECT_FIELD(shape,height,15);
    REJECT_FIELD(shape,pitch,132);
    renderer.storage_buffers[0].buffer_size=2047;
    assert(!can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    renderer.storage_buffers[0].buffer_size=2048;
    for (pg.surface_scale_factor=1; pg.surface_scale_factor<=4; pg.surface_scale_factor*=2)
        assert(can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    pg.surface_scale_factor=2; scratch_available=false;
    assert(!can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    pg.surface_scale_factor=1;
    assert(can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    SurfaceBinding overlap = {.draw_dirty=true,.vram_addr=source.vram_addr+2047,.size=32};
    source.next=&overlap;
    assert(!can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    overlap.vram_addr++;
    assert(can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    overlap.vram_addr=source.vram_addr-1;
    assert(!can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    overlap.draw_dirty=false;
    assert(can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    source.next=NULL; renderer.invalid_surfaces.first=&overlap;
    overlap.writeback_owed=true;
    assert(!can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    overlap.writeback_owed=false;
    assert(can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    overlap.draw_dirty=true;
    assert(!can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    overlap.vram_addr = source.vram_addr + 1;
    overlap.size = 0;
    assert(can_reinterpret_argb_as_rgb565(&pg,&source,&shape));
    puts("texture reinterpretation gate: formats, layouts, bounds, capacity, scratch and dirty-overlap cases passed");
    return 0;
}
