/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "vulkan/vulkan_core.h"
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define ROUND_UP(n,a) (((n) + (a) - 1) & ~((a) - 1))
#define BUFFER_COMPUTE_DST 0
#define BUFFER_COMPUTE_SRC 1
#define NV2A_PROF_SURF_TO_TEX 0
#define RGBA_GREEN 0
#define nv2a_profile_inc_counter(c) ((void)(c))
#define trace_nv2a_pgraph_surface_render_to_texture(...) ((void)0)
#define pgraph_vk_begin_debug_marker(...) ((void)0)
typedef struct { VkFormat vk_format; } VkColorFormatInfo;
typedef struct { unsigned color_format; } TextureShape;
typedef struct {
    bool color;
    struct { VkFormat vk_format; unsigned host_bytes_per_pixel; VkImageAspectFlags aspect; } host_fmt;
    unsigned width, height;
} SurfaceBinding;
typedef struct { struct { TextureShape state; } key; } TextureBinding;
typedef struct { VkDeviceSize buffer_size; VkBuffer buffer; } StorageBuffer;
typedef struct { StorageBuffer storage_buffers[2]; VkPhysicalDeviceProperties device_props; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; unsigned surface_scale_factor; } PGRAPHState;
static bool scratch_available, ensured, recording, grown;
static VkDeviceSize requested;
static size_t planned_pixels;
static VkColorFormatInfo texture_vkf(PGRAPHVkState *r, unsigned format)
{ (void)r; return (VkColorFormatInfo){format}; }
static void pgraph_apply_scaling_factor(PGRAPHState *p, unsigned *w, unsigned *h)
{ *w *= p->surface_scale_factor; *h *= p->surface_scale_factor; }
static VkImage native_get_scratch(PGRAPHState *p, VkFormat f, unsigned w, unsigned h)
{
    (void)p; (void)f; (void)w; (void)h;
    assert(!ensured && !recording);
    return scratch_available ? (VkImage)(uintptr_t)1 : VK_NULL_HANDLE;
}
static void pgraph_vk_buffer_ensure_capacity(PGRAPHState *p, unsigned index, VkDeviceSize needed)
{
    assert(!recording && index == BUFFER_COMPUTE_DST);
    requested = needed;
    grown = needed > p->vk_renderer_state->storage_buffers[index].buffer_size;
    if (grown) p->vk_renderer_state->storage_buffers[index].buffer_size = needed;
    ensured = true;
}
static VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *p)
{ (void)p; assert(ensured && !recording); recording = true; return VK_NULL_HANDLE; }
/* PRODUCTION */
int main(void)
{
    const VkFormat formats[] = { VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT,
                                 VK_FORMAT_D32_SFLOAT_S8_UINT };
    const unsigned dimensions[][2] = {{1, 1}, {3, 5}, {7, 9}, {640, 480}, {1920, 1080}};
    const unsigned alignments[] = {1, 256, 4096};
    unsigned cases = 0, native_avoided = 0, tail_cases = 0;
    for (unsigned f = 0; f < 3; f++) for (unsigned d = 0; d < 5; d++)
    for (unsigned scale = 1; scale <= 8; scale *= 2)
    for (unsigned available = 0; available < 2; available++)
    for (unsigned a = 0; a < 3; a++) for (unsigned output_bpp = 2; output_bpp <= 4; output_bpp *= 2) {
        const VkDeviceSize capacity = 800ull * 1024 * 1024;
        PGRAPHVkState r = {.storage_buffers = {{capacity}}};
        r.device_props.limits.minStorageBufferOffsetAlignment = alignments[a];
        PGRAPHState pg = {&r, scale};
        SurfaceBinding surface = {.width = dimensions[d][0], .height = dimensions[d][1],
            .host_fmt = {formats[f], f == 0 ? 2 : f == 1 ? 4 : 8,
                VK_IMAGE_ASPECT_DEPTH_BIT | (f ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)}};
        TextureBinding texture = {.key.state.color_format =
            output_bpp == 2 ? VK_FORMAT_R16_UNORM : VK_FORMAT_R32_UINT};
        scratch_available = available;
        ensured = recording = grown = false;
        copy_zeta_surface_to_texture(&pg, &surface, &texture);
        size_t pixels = (size_t)surface.width * surface.height;
        if (!(scale > 1 && available)) pixels *= scale * scale;
        size_t planes = f ? ((pixels * 4 + alignments[a] - 1) / alignments[a]) * alignments[a] + ((pixels+3)/4)*4 : pixels * 2;
        size_t output = pixels * (f ? 4 : output_bpp);
        assert(requested == (planes > output ? planes : output));
        tail_cases+=f&&pixels%4!=0;
        assert(planned_pixels == pixels && recording && ensured);
        assert(grown == (requested > capacity));
        VkDeviceSize old_needed = (VkDeviceSize)surface.width * surface.height * scale * scale * 8;
        if (scale > 1 && available && old_needed > capacity && !grown) native_avoided++;
        cases++;
    }
    assert(native_avoided&&tail_cases);
    printf("Depth texture input: %u range checks, %u stencil tails include the complete final word\n",cases,tail_cases);
    printf("depth transfer capacity: %u format/extent/scale/scratch/alignment/output cases, %u unnecessary growth requests avoided\n",
           cases, native_avoided);
}
