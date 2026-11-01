/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "vulkan/vulkan_core.h"
#include "hw/xbox/nv2a/nv2a_regs.h"

typedef struct { unsigned color_format, width, height; } TextureShape;
typedef struct {
    struct { VkFormat vk_format; } host_fmt;
    unsigned width, height;
    VkImage image;
    int draw_time;
} SurfaceBinding;
typedef struct {
    struct { TextureShape state; } key;
    VkImage image;
    VkImageLayout current_layout;
    int draw_time;
} TextureBinding;
typedef struct { struct { VkBuffer buffer; } storage_buffers[1]; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; unsigned surface_scale_factor; } PGRAPHState;
typedef struct { VkFormat vk_format; } VkColorFormatInfo;
#define BUFFER_COMPUTE_DST 0
#define NV2A_PROF_SURF_TO_TEX_REINTERPRET 0
#define HANDLE(type, n) ((type)(uintptr_t)(n))
static VkFormat expected_format;
static unsigned stage, blits, transitions, expected_scale;
static VkColorFormatInfo texture_vkf(PGRAPHVkState *r, unsigned f)
{
    (void)r;
    return (VkColorFormatInfo){ f == NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5 ?
        VK_FORMAT_R5G6B5_UNORM_PACK16 : VK_FORMAT_A1R5G5B5_UNORM_PACK16 };
}
static VkImage native_get_scratch(PGRAPHState *p, VkFormat f, unsigned w, unsigned h)
{ (void)p; (void)f; (void)w; (void)h; return HANDLE(VkImage, 3); }
static VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *p)
{ (void)p; return HANDLE(VkCommandBuffer, 1); }
static void pgraph_vk_end_nondraw_commands(PGRAPHState *p, VkCommandBuffer c)
{ (void)p; (void)c; assert(stage == 5); }
static VkImageLayout pgraph_vk_surface_base_layout(SurfaceBinding *s)
{ (void)s; return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; }
static void pgraph_vk_surface_left_shader_read(PGRAPHState *p, SurfaceBinding *s)
{ (void)p; (void)s; }
static void nv2a_profile_inc_counter(int c) { (void)c; }
static void pgraph_vk_transition_image_layout(PGRAPHState *p, VkCommandBuffer c,
    VkImage image, VkFormat format, VkImageLayout old, VkImageLayout next)
{
    (void)p; (void)c; (void)old;
    if (image == HANDLE(VkImage, 2)) {
        assert(format == expected_format);
        assert(next == (transitions++ ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL :
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL));
    }
}
static void barrier(VkCommandBuffer c, VkPipelineStageFlags src,
    VkPipelineStageFlags dst, VkDependencyFlags dep, unsigned nm,
    const VkMemoryBarrier *m, unsigned nb, const VkBufferMemoryBarrier *b,
    unsigned ni, const VkImageMemoryBarrier *i)
{
    (void)c; (void)src; (void)dst; (void)dep; (void)nm; (void)m; (void)ni; (void)i;
    assert(nb == 1 && b->buffer == HANDLE(VkBuffer, 1));
    assert(stage == 0 || stage == 2 || stage == 4);
    if (stage == 2) assert(b->srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT &&
                           b->dstAccessMask == VK_ACCESS_TRANSFER_READ_BIT);
    stage++;
}
static void image_to_buffer(VkCommandBuffer c, VkImage image, VkImageLayout layout,
    VkBuffer buffer, unsigned count, const VkBufferImageCopy *region)
{
    (void)c; (void)image; (void)layout; (void)buffer;
    assert(stage++ == 1 && count == 1 && region->bufferOffset == 0);
    assert(region->imageExtent.width == 32 && region->imageExtent.height == 16);
}
static void buffer_to_image(VkCommandBuffer c, VkBuffer buffer, VkImage image,
    VkImageLayout layout, unsigned count, const VkBufferImageCopy *region)
{
    (void)c; (void)buffer; (void)image; (void)layout;
    assert(stage++ == 3 && count == 1 && region->bufferOffset == 0);
    assert(region->imageExtent.width == 64 && region->imageExtent.height == 16);
}
static void blit(VkCommandBuffer c, VkImage src, VkImageLayout sl, VkImage dst,
    VkImageLayout dl, unsigned count, const VkImageBlit *r, VkFilter filter)
{
    (void)c; (void)src; (void)sl; (void)dst; (void)dl;
    assert(count == 1 && filter == VK_FILTER_NEAREST);
    assert(r->srcOffsets[1].x == 32 * (int)expected_scale);
    assert(r->dstOffsets[1].x == 32 && r->dstOffsets[1].y == 16);
    blits++;
}
#define vkCmdPipelineBarrier barrier
#define vkCmdCopyImageToBuffer image_to_buffer
#define vkCmdCopyBufferToImage buffer_to_image
#define vkCmdBlitImage blit
/* PRODUCTION */
int main(void)
{
    const unsigned formats[] = { NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5,
                                 NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5 };
    PGRAPHVkState r = { .storage_buffers = {{ HANDLE(VkBuffer, 1) }} };
    SurfaceBinding surface = { .host_fmt = { VK_FORMAT_B8G8R8A8_UNORM },
        .width = 32, .height = 16, .image = HANDLE(VkImage, 1), .draw_time = 42 };
    for (unsigned f = 0; f < 2; f++) for (unsigned scale = 1; scale <= 4; scale *= 2) {
        PGRAPHState pg = { &r, scale };
        TextureBinding texture = { .key.state = { formats[f], 64, 16 },
            .image = HANDLE(VkImage, 2), .current_layout = VK_IMAGE_LAYOUT_UNDEFINED };
        expected_format = texture_vkf(&r, formats[f]).vk_format;
        stage = transitions = blits = 0; expected_scale = scale;
        copy_argb_to_packed16_texture(&pg, &surface, &texture);
        assert(stage == 5 && transitions == 2 && blits == (scale > 1));
        assert(texture.current_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        assert(texture.draw_time == 42);
    }
    puts("packed16 transfer: both formats, scale 1/2/4, extents, layouts and barriers passed");
}
