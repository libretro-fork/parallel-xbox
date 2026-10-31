/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
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

#include "renderer.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "ui/libretro-crosshair.h"
#include <math.h>

/* On the device the libretro core shares with its frontend, the display image
 * is a plain image: it is not exported to GL or to a second device */
static inline bool on_frontend_device(void)
{
    return xemu_libretro_vk.active;
}

static uint8_t *convert_texture_data__CR8YB8CB8YA8(uint8_t *data_out,
                                                   const uint8_t *data_in,
                                                   unsigned int width,
                                                   unsigned int height,
                                                   unsigned int pitch)
{
    int x, y;
    for (y = 0; y < height; y++) {
        const uint8_t *line = &data_in[y * pitch];
        const uint32_t row_offset = y * width;
        for (x = 0; x < width; x++) {
            uint8_t *pixel = &data_out[(row_offset + x) * 4];
            convert_yuy2_to_rgb(line, x, &pixel[0], &pixel[1], &pixel[2]);
            pixel[3] = 255;
        }
    }
    return data_out;
}

static float pvideo_calculate_scale(unsigned int din_dout,
                                    unsigned int output_size)
{
    float calculated_in = din_dout * (output_size - 1);
    calculated_in = floorf(calculated_in / (1 << 20) + 0.5f);
    return (calculated_in + 1.0f) / output_size;
}

static void destroy_pvideo_image(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *d = &r->display;

    if (d->pvideo.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(r->device, d->pvideo.sampler, NULL);
        d->pvideo.sampler = VK_NULL_HANDLE;
    }

    if (d->pvideo.image_view != VK_NULL_HANDLE) {
        vkDestroyImageView(r->device, d->pvideo.image_view, NULL);
        d->pvideo.image_view = VK_NULL_HANDLE;
    }

    if (d->pvideo.image != VK_NULL_HANDLE) {
        vmaDestroyImage(r->allocator, d->pvideo.image, d->pvideo.allocation);
        d->pvideo.image = VK_NULL_HANDLE;
        d->pvideo.allocation = VK_NULL_HANDLE;
    }
}

static bool create_pvideo_image(PGRAPHState *pg, int width, int height)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *d = &r->display;

    if (d->pvideo.image != VK_NULL_HANDLE && d->pvideo.width == width &&
        d->pvideo.height == height) {
        return false;
    }
    if (d->pvideo.image != VK_NULL_HANDLE) {
        /* Only a size change: cover prior display reads before retiring
         * their input image and sampler. Ordinary updates reuse both. */
        pgraph_vk_finish(pg, VK_FINISH_REASON_FLUSH);
        VkCommandBuffer drain = pgraph_vk_begin_single_time_commands(pg);
        pgraph_vk_end_single_time_commands(pg, drain);
        destroy_pvideo_image(pg);
    }

    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .flags = 0,
    };
    VmaAllocationCreateInfo alloc_create_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    VK_CHECK(vmaCreateImage(r->allocator, &image_create_info,
                            &alloc_create_info, &d->pvideo.image,
                            &d->pvideo.allocation, NULL));

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = d->pvideo.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = image_create_info.mipLevels,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = image_create_info.arrayLayers,
    };
    VK_CHECK(vkCreateImageView(r->device, &image_view_create_info, NULL,
                               &d->pvideo.image_view));

    VkSamplerCreateInfo sampler_create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    };
    VK_CHECK(vkCreateSampler(r->device, &sampler_create_info, NULL,
                             &d->pvideo.sampler));
    d->pvideo.seq = ++r->texture_binding_seq;
    d->pvideo.width = width;
    d->pvideo.height = height;
    return true;
}

static void upload_pvideo_image(PGRAPHState *pg, PvideoState state)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *disp = &r->display;

    bool created = create_pvideo_image(pg, state.in_width, state.in_height);

    // FIXME: Dirty tracking. We don't necessarily need to upload so much.

    // FIXME: Merge with display renderer command buffer

    VkCommandBuffer cmd = pgraph_vk_begin_single_time_commands(pg);

    /* the picture, converted straight into the staging ring */
    uint8_t *staged;
    size_t staged_size = (size_t)state.in_width * state.in_height * 4;
    VkDeviceSize staged_offset =
        pgraph_vk_staging_alloc(pg, staged_size, 16, &staged);
    convert_texture_data__CR8YB8CB8YA8(
        staged, d->vram_ptr + state.base + state.offset,
        state.in_width, state.in_height, state.pitch);
    pgraph_vk_staging_flush(pg, staged_offset, staged_size);

    VkBufferMemoryBarrier host_barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = r->storage_buffers[BUFFER_STAGING_SRC].buffer,
        .size = VK_WHOLE_SIZE
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1,
                         &host_barrier, 0, NULL);

    pgraph_vk_transition_image_layout(
        pg, cmd, disp->pvideo.image, VK_FORMAT_R8G8B8A8_UNORM,
        created ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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
        .imageExtent = (VkExtent3D){ state.in_width, state.in_height, 1 },
    };
    vkCmdCopyBufferToImage(cmd, r->storage_buffers[BUFFER_STAGING_SRC].buffer,
                           disp->pvideo.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    pgraph_vk_transition_image_layout(pg, cmd, disp->pvideo.image,
                                      VK_FORMAT_R8G8B8A8_UNORM,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    /* Upload only: the following display submission consumes it in queue
     * order; the aux fence protects staging reuse without a CPU drain. */
    pgraph_vk_end_single_time_commands_async(pg, cmd);
}

static const char *display_frag_glsl =
    "#version 450\n"
    "const bool byte_scanout = true;\n"
    "layout(binding = 0) uniform sampler2D tex;\n"
    "layout(binding = 1) uniform sampler2D pvideo_tex;\n"
    "layout(push_constant, std430) uniform PushConstants {\n"
    "    float line_offset;\n"
    "    vec2 display_size;\n"
    "    bool pvideo_enable;\n"
    "    vec2 pvideo_in_pos;\n"
    "    vec4 pvideo_pos;\n"
    "    vec4 pvideo_scale;\n"
    "    bool pvideo_color_key_enable;\n"
    "    vec3 pvideo_color_key;\n"
    "    vec4 source_transform;\n"
    "    uvec4 source_bytes; /* relative address, pitch/mask X, mask Y, formats */\n"
    "};\n"
    "layout(location = 0) out vec4 out_Color;\n"
    "uint compact_bits(uint v, uint mask) {\n"
    "    uint r = 0u, bit = 1u;\n"
    "    while (mask != 0u) {\n"
    "        uint low = mask & (0u - mask);\n"
    "        if ((v & low) != 0u) r |= bit;\n"
    "        mask &= ~low; bit <<= 1u;\n"
    "    }\n"
    "    return r;\n"
    "}\n"
    "uint source_bpp() {\n"
    "    uint kind = source_bytes.w & 255u;\n"
    "    return kind == 1u ? 4u : kind == 4u ? 1u : 2u;\n"
    "}\n"
    "vec4 byte_texel(uint off) {\n"
    "    uint bpp = source_bpp();\n"
    "    uvec2 xy;\n"
    "    if ((source_bytes.w & 65536u) != 0u) {\n"
    "        uint t = off / bpp;\n"
    "        xy = uvec2(compact_bits(t, source_bytes.y), compact_bits(t, source_bytes.z));\n"
    "    } else xy = uvec2((off % source_bytes.y) / bpp, off / source_bytes.y);\n"
    "    int scale = int(source_transform.z);\n"
    "    return texelFetch(tex, ivec2(xy) * scale + ivec2(scale / 2), 0);\n"
    "}\n"
    "uint byte_word(uint off) {\n"
    "    uint kind = source_bytes.w & 255u;\n"
    "    vec4 c = byte_texel(off);\n"
    "    if (kind == 1u) return packUnorm4x8(c.bgra);\n"
    "    if (kind == 2u) {\n"
    "        uvec3 v = uvec3(round(clamp(c.rgb, 0.0, 1.0) * vec3(31,63,31)));\n"
    "        return (v.r << 11u) | (v.g << 5u) | v.b;\n"
    "    }\n"
    "    if (kind == 3u) {\n"
    "        uvec4 v = uvec4(round(clamp(c, 0.0, 1.0) * vec4(31,31,31,1)));\n"
    "        return (v.a << 15u) | (v.r << 10u) | (v.g << 5u) | v.b;\n"
    "    }\n"
    "    uvec2 v = uvec2(round(clamp(c.rg, 0.0, 1.0) * 255.0));\n"
    "    return kind == 4u ? v.r : v.r | (v.g << 8u);\n"
    "}\n"
    "vec4 byte_color() {\n"
    "    uint src_kind = source_bytes.w & 255u, dst_kind = (source_bytes.w >> 8u) & 255u;\n"
    "    uint dst_bpp = dst_kind == 1u ? 4u : 2u, src_bpp = source_bpp();\n"
    "    uvec2 p = uvec2(gl_FragCoord.xy / source_transform.z);\n"
    "    uint off = p.y * uint(source_transform.x) + p.x * dst_bpp - source_bytes.x;\n"
    "    if ((source_bytes.w & 131072u) != 0u) {\n"
    "        uvec2 size = uvec2(textureSize(tex, 0)) / uint(source_transform.z);\n"
    "        if (off / source_bytes.y >= size.y || off % source_bytes.y + dst_bpp > size.x * src_bpp) discard;\n"
    "    }\n"
    "    if (src_kind == dst_kind) return byte_texel(off);\n"
    "    uint word = 0u;\n"
    "    if (src_bpp >= dst_bpp) {\n"
    "        word = byte_word(off) >> ((off % src_bpp) * 8u);\n"
    "    } else {\n"
    "        for (uint k = 0u; k < dst_bpp; k += src_bpp) word |= byte_word(off + k) << (k * 8u);\n"
    "    }\n"
    "    if (dst_kind == 1u) return unpackUnorm4x8(word).bgra;\n"
    "    uvec3 v = dst_kind == 2u ? uvec3((word >> 11u) & 31u, (word >> 5u) & 63u, word & 31u) : uvec3((word >> 10u) & 31u, (word >> 5u) & 31u, word & 31u);\n"
    "    uvec3 rgb = (v << 3u) | (v >> 2u);\n"
    "    if (dst_kind == 2u) rgb.g = (v.g << 2u) | (v.g >> 4u);\n"
    "    return vec4(vec3(rgb) / 255.0, dst_kind == 2u ? 1.0 : float((word >> 15u) & 1u));\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    vec2 tex_coord = gl_FragCoord.xy/display_size;\n"
    "    if (source_transform.z > 0) {\n"
    "        tex_coord = (gl_FragCoord.xy * source_transform.zw + source_transform.xy) / textureSize(tex, 0);\n"
    "    } else {\n"
    "        float rel = display_size.y/textureSize(tex, 0).y/line_offset;\n"
    "        tex_coord.y = 1 + rel*(tex_coord.y - 1);\n"
    "    }\n"
    "    if (byte_scanout) out_Color = byte_color();\n"
    "    else out_Color = texture(tex, tex_coord);\n"
    "    if (pvideo_enable) {\n"
    "        vec2 screen_coord = vec2(gl_FragCoord.x, display_size.y - gl_FragCoord.y) * pvideo_scale.z;\n"
    "        vec4 output_region = vec4(pvideo_pos.xy, pvideo_pos.xy + pvideo_pos.zw);\n"
    "        bvec4 clip = bvec4(lessThan(screen_coord, output_region.xy),\n"
    "                           greaterThan(screen_coord, output_region.zw));\n"
    "        if (!any(clip) && (!pvideo_color_key_enable || out_Color.rgb == pvideo_color_key)) {\n"
    "            vec2 out_xy = screen_coord - pvideo_pos.xy;\n"
    "            vec2 in_st = (pvideo_in_pos + out_xy * pvideo_scale.xy) / textureSize(pvideo_tex, 0);\n"
    "            out_Color.rgba = texture(pvideo_tex, in_st);\n"
    "        }\n"
    "    }\n"
    "}\n";

static void create_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorPoolSize pool_sizes = {
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 2 * PGRAPH_VK_DISPLAY_SETS,
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_sizes,
        .maxSets = PGRAPH_VK_DISPLAY_SETS,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    };
    VK_CHECK(vkCreateDescriptorPool(r->device, &pool_info, NULL,
                                    &r->display.descriptor_pool));
}

static void destroy_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorPool(r->device, r->display.descriptor_pool, NULL);
    r->display.descriptor_pool = VK_NULL_HANDLE;
}

static void create_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorSetLayoutBinding bindings[2];

    for (int i = 0; i < ARRAY_SIZE(bindings); i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
    }
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ARRAY_SIZE(bindings),
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(r->device, &layout_info, NULL,
                                         &r->display.descriptor_set_layout));
}

static void destroy_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorSetLayout(r->device, r->display.descriptor_set_layout,
                                 NULL);
    r->display.descriptor_set_layout = VK_NULL_HANDLE;
}

static void create_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorSetLayout layouts[PGRAPH_VK_DISPLAY_SETS];
    for (int i = 0; i < PGRAPH_VK_DISPLAY_SETS; i++) {
        layouts[i] = r->display.descriptor_set_layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = r->display.descriptor_pool,
        .descriptorSetCount = PGRAPH_VK_DISPLAY_SETS,
        .pSetLayouts = layouts,
    };
    VK_CHECK(vkAllocateDescriptorSets(r->device, &alloc_info,
                                      r->display.descriptor_sets));
    r->display.descriptor_index = 0;
}

static void create_render_pass(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkAttachmentDescription attachment;

    VkAttachmentReference color_reference;
    attachment = (VkAttachmentDescription){
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    color_reference = (VkAttachmentReference){
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
    };

    dependency.srcStageMask |=
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask |=
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_reference,
    };

    VkRenderPassCreateInfo renderpass_create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    VK_CHECK(vkCreateRenderPass(r->device, &renderpass_create_info, NULL,
                                &r->display.render_pass));
}

static void destroy_render_pass(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    vkDestroyRenderPass(r->device, r->display.render_pass, NULL);
    r->display.render_pass = VK_NULL_HANDLE;
}

static void create_display_pipeline(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->display.display_frag =
        pgraph_vk_create_shader_module_from_glsl(
            r, VK_SHADER_STAGE_FRAGMENT_BIT, display_frag_glsl);
    /* The internal GLSL compiler has no specialization constants. Compile
     * startup variants with a literal constant instead; both share the layout. */
    const char *body = strchr(strchr(display_frag_glsl, '\n') + 1, '\n') + 1;
    char *native = g_strdup_printf("#version 450\nconst bool byte_scanout = false;\n%s", body);
    r->display.native_frag = pgraph_vk_create_shader_module_from_glsl(
        r, VK_SHADER_STAGE_FRAGMENT_BIT, native);
    g_free(native);

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = r->quad_vert_module,
            .pName = "main",
        },
        (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = r->display.native_frag->module,
            .pName = "main",
        },
     };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_FALSE,
    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT,
                                        VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    assert(r->display.display_frag->push_constants.total_size <=
           r->device_props.limits.maxPushConstantsSize);
    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = r->display.display_frag->push_constants.total_size,
    };

    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &r->display.descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };
    VK_CHECK(vkCreatePipelineLayout(r->device, &pipeline_layout_info, NULL,
                                    &r->display.pipeline_layout));

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = ARRAY_SIZE(shader_stages),
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = r->zeta_binding ? &depth_stencil : NULL,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = r->display.pipeline_layout,
        .renderPass = r->display.render_pass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
    };
    VK_CHECK(vkCreateGraphicsPipelines(r->device, r->vk_pipeline_cache, 1,
                                       &pipeline_info, NULL,
                                       &r->display.pipeline));
    /* The ordinary pipeline has no byte addressing/conversion/discard work. */
    shader_stages[1].module = r->display.display_frag->module;
    VK_CHECK(vkCreateGraphicsPipelines(r->device, r->vk_pipeline_cache, 1,
        &pipeline_info, NULL, &r->display.byte_pipeline));
}

static void destroy_display_pipeline(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyPipeline(r->device, r->display.pipeline, NULL);
    r->display.pipeline = VK_NULL_HANDLE;
    vkDestroyPipeline(r->device, r->display.byte_pipeline, NULL);
    r->display.byte_pipeline = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(r->device, r->display.pipeline_layout, NULL);
    r->display.pipeline_layout = VK_NULL_HANDLE;

    pgraph_vk_destroy_shader_module(r, r->display.display_frag);
    r->display.display_frag = NULL;
    pgraph_vk_destroy_shader_module(r, r->display.native_frag);
    r->display.native_frag = NULL;
}

static void create_frame_buffer(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkFramebufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = r->display.render_pass,
        .attachmentCount = 1,
        .pAttachments = &r->display.image_view,
        .width = r->display.width,
        .height = r->display.height,
        .layers = 1,
    };
    VK_CHECK(vkCreateFramebuffer(r->device, &create_info, NULL,
                                 &r->display.framebuffer));
}

static void destroy_frame_buffer(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    vkDestroyFramebuffer(r->device, r->display.framebuffer, NULL);
    r->display.framebuffer = NULL;
}

static void destroy_current_display_image(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *d = &r->display;

    if (d->image == VK_NULL_HANDLE) {
        return;
    }

    destroy_frame_buffer(pg);

#if HAVE_EXTERNAL_MEMORY
    if (!on_frontend_device()) {
    glDeleteTextures(1, &d->gl_texture_id);
    d->gl_texture_id = 0;

    glDeleteMemoryObjectsEXT(1, &d->gl_memory_obj);
    d->gl_memory_obj = 0;

#ifdef WIN32
    CloseHandle(d->handle);
    d->handle = 0;
#else
    // Not taken by the frontend: nobody else will close it.
    if (d->frontend_fd_plus1) {
        close(d->frontend_fd_plus1 - 1);
        d->frontend_fd_plus1 = 0;
    }
#endif
    }
#endif

    vkDestroyImageView(r->device, d->image_view, NULL);
    d->image_view = VK_NULL_HANDLE;

    vkDestroyImage(r->device, d->image, NULL);
    d->image = VK_NULL_HANDLE;

    vkFreeMemory(r->device, d->memory, NULL);
    d->memory = VK_NULL_HANDLE;

    d->draw_time = 0;
}

// FIXME: We may need to use two images. One for actually rendering display,
// and another for GL in the correct tiling mode

static void create_display_image(PGRAPHState *pg, int width, int height)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *d = &r->display;

    if (r->display.image != VK_NULL_HANDLE) {
        destroy_current_display_image(pg);
    }

    const GLint gl_internal_format = GL_RGBA8;
    bool use_optimal_tiling = true;

#if HAVE_EXTERNAL_MEMORY
    if (!on_frontend_device()) {
    GLint num_tiling_types;
    glGetInternalformativ(GL_TEXTURE_2D, gl_internal_format,
                          GL_NUM_TILING_TYPES_EXT, 1, &num_tiling_types);
    // XXX: Apparently on AMD GL_OPTIMAL_TILING_EXT is reported to be
    // supported, but doesn't work? On nVidia, GL_LINEAR_TILING_EXT may not
    // be supported so we must use optimal. Default to optimal unless
    // linear is explicitly specified...
    GLint tiling_types[num_tiling_types];
    glGetInternalformativ(GL_TEXTURE_2D, gl_internal_format,
                          GL_TILING_TYPES_EXT, num_tiling_types, tiling_types);
    for (int i = 0; i < num_tiling_types; i++) {
        if (tiling_types[i] == GL_LINEAR_TILING_EXT) {
            use_optimal_tiling = false;
            break;
        }
    }
    }
#endif

    // Create image
    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .tiling = use_optimal_tiling ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkExternalMemoryImageCreateInfo external_memory_image_create_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
#ifdef WIN32
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
#else
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
#endif
    };
    if (!on_frontend_device()) {
        image_create_info.pNext = &external_memory_image_create_info;
    }

    VK_CHECK(vkCreateImage(r->device, &image_create_info, NULL, &d->image));

    // Allocate and bind image memory
    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(r->device, d->image, &memory_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex =
            pgraph_vk_get_memory_type(pg, memory_requirements.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    VkExportMemoryAllocateInfo export_memory_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .handleTypes =
#ifdef WIN32
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR
#else
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
#endif
            ,
    };
    if (!on_frontend_device()) {
        alloc_info.pNext = &export_memory_alloc_info;
    }

    VK_CHECK(vkAllocateMemory(r->device, &alloc_info, NULL, &d->memory));
    VK_CHECK(vkBindImageMemory(r->device, d->image, d->memory, 0));

    // Create Image View
    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = d->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image_create_info.format,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.levelCount = 1,
        .subresourceRange.layerCount = 1,
    };
    VK_CHECK(vkCreateImageView(r->device, &image_view_create_info, NULL,
                               &d->image_view));

#if HAVE_EXTERNAL_MEMORY
    if (!on_frontend_device()) {

#ifdef WIN32

    VkMemoryGetWin32HandleInfoKHR handle_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
        .memory = d->memory,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT_KHR
    };
    VK_CHECK(vkGetMemoryWin32HandleKHR(r->device, &handle_info, &d->handle));

    glCreateMemoryObjectsEXT(1, &d->gl_memory_obj);
    glImportMemoryWin32HandleEXT(d->gl_memory_obj, memory_requirements.size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, d->handle);
    assert(glGetError() == GL_NO_ERROR);

#else

    VkMemoryGetFdInfoKHR fd_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .memory = d->memory,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
    };
    VK_CHECK(vkGetMemoryFdKHR(r->device, &fd_info, &d->fd));

    glCreateMemoryObjectsEXT(1, &d->gl_memory_obj);
    glImportMemoryFdEXT(d->gl_memory_obj, memory_requirements.size,
                        GL_HANDLE_TYPE_OPAQUE_FD_EXT, d->fd);
    assert(glIsMemoryObjectEXT(d->gl_memory_obj));
    assert(glGetError() == GL_NO_ERROR);

    int frontend_fd = -1;
    VK_CHECK(vkGetMemoryFdKHR(r->device, &fd_info, &frontend_fd));
    if (d->frontend_fd_plus1) {
        close(d->frontend_fd_plus1 - 1);
    }
    d->frontend_fd_plus1 = frontend_fd + 1;

#endif // WIN32

    glGenTextures(1, &d->gl_texture_id);
    glBindTexture(GL_TEXTURE_2D, d->gl_texture_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT,
                    use_optimal_tiling ? GL_OPTIMAL_TILING_EXT :
                                         GL_LINEAR_TILING_EXT);
    glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, gl_internal_format,
                         image_create_info.extent.width,
                         image_create_info.extent.height, d->gl_memory_obj, 0);
    assert(glGetError() == GL_NO_ERROR);

    }
#endif // HAVE_EXTERNAL_MEMORY

    d->width = image_create_info.extent.width;
    d->height = image_create_info.extent.height;

    create_frame_buffer(pg);
}

static void update_descriptor_set(PGRAPHState *pg, SurfaceBinding *surface)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorImageInfo image_infos[2];
    VkWriteDescriptorSet descriptor_writes[2];

    // Display surface
    image_infos[0] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = surface->image_view,
        .sampler = r->display.sampler,
    };
    descriptor_writes[0] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = r->display.descriptor_sets[r->display.descriptor_index],
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .pImageInfo = &image_infos[0],
    };

    // FIXME: PVIDEO Overlay
    if (r->display.pvideo.state.enabled) {
        assert(r->display.pvideo.image_view != VK_NULL_HANDLE);
        assert(r->display.pvideo.sampler != VK_NULL_HANDLE);
        image_infos[1] = (VkDescriptorImageInfo){
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = r->display.pvideo.image_view,
            .sampler = r->display.pvideo.sampler,
        };
    } else {
        image_infos[1] = (VkDescriptorImageInfo){
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView = r->dummy_texture.image_view,
            .sampler = r->dummy_texture.sampler,
        };
    }
    descriptor_writes[1] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = r->display.descriptor_sets[r->display.descriptor_index],
        .dstBinding = 1,
        .dstArrayElement = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .pImageInfo = &image_infos[1],
    };

    PGRAPHVkDisplayState *disp = &r->display;
    unsigned index = disp->descriptor_index;
    uint64_t video_seq = disp->pvideo.state.enabled ? disp->pvideo.seq : 0;
    if (disp->descriptor_keys[index].source == image_infos[0].imageView &&
        disp->descriptor_keys[index].source_seq == surface->image_seq &&
        disp->descriptor_keys[index].video == image_infos[1].imageView &&
        disp->descriptor_keys[index].video_seq == video_seq) {
        return;
    }
    vkUpdateDescriptorSets(r->device, ARRAY_SIZE(descriptor_writes),
                           descriptor_writes, 0, NULL);
    disp->descriptor_keys[index].source = image_infos[0].imageView;
    disp->descriptor_keys[index].source_seq = surface->image_seq;
    disp->descriptor_keys[index].video = image_infos[1].imageView;
    disp->descriptor_keys[index].video_seq = video_seq;
}

static PvideoState get_pvideo_state(PGRAPHState *pg)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PvideoState state;

    // FIXME: This check against PVIDEO_SIZE_IN does not match HW behavior.
    // Many games seem to pass this value when initializing or tearing down
    // PVIDEO. On its own, this generally does not result in the overlay being
    // hidden, however there are certain games (e.g., Ultimate Beach Soccer)
    // that use an unknown mechanism to hide the overlay without explicitly
    // stopping it.
    // Since the value seems to be set to 0xFFFFFFFF only in cases where the
    // content is not valid, it is probably good enough to treat it as an
    // implicit stop.
    state.enabled = (d->pvideo.regs[NV_PVIDEO_BUFFER] & NV_PVIDEO_BUFFER_0_USE)
        && d->pvideo.regs[NV_PVIDEO_SIZE_IN] != 0xFFFFFFFF;
    if (!state.enabled) {
        return state;
    }

    state.base = d->pvideo.regs[NV_PVIDEO_BASE];
    state.limit = d->pvideo.regs[NV_PVIDEO_LIMIT];
    state.offset = d->pvideo.regs[NV_PVIDEO_OFFSET];

    state.pitch =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT], NV_PVIDEO_FORMAT_PITCH);
    state.format =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT], NV_PVIDEO_FORMAT_COLOR);

    /* TODO: support other color formats */
    assert(state.format == NV_PVIDEO_FORMAT_COLOR_LE_CR8YB8CB8YA8);

    state.in_width =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_IN], NV_PVIDEO_SIZE_IN_WIDTH);
    state.in_height =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_IN], NV_PVIDEO_SIZE_IN_HEIGHT);

    state.out_width =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT], NV_PVIDEO_SIZE_OUT_WIDTH);
    state.out_height =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_SIZE_OUT], NV_PVIDEO_SIZE_OUT_HEIGHT);

    state.in_s = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_IN],
                        NV_PVIDEO_POINT_IN_S);
    state.in_t = GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_IN],
                        NV_PVIDEO_POINT_IN_T);

    uint32_t ds_dx = d->pvideo.regs[NV_PVIDEO_DS_DX];
    uint32_t dt_dy = d->pvideo.regs[NV_PVIDEO_DT_DY];
    state.scale_x = ds_dx == NV_PVIDEO_DIN_DOUT_UNITY ?
                        1.0f :
                        pvideo_calculate_scale(ds_dx, state.out_width);
    state.scale_y = dt_dy == NV_PVIDEO_DIN_DOUT_UNITY ?
                        1.0f :
                        pvideo_calculate_scale(dt_dy, state.out_height);

    // On HW, setting NV_PVIDEO_SIZE_IN larger than NV_PVIDEO_SIZE_OUT results
    // in them being capped to the output size, content is not scaled. This is
    // particularly important as NV_PVIDEO_SIZE_IN may be set to 0xFFFFFFFF
    // during initialization or teardown.
    if (state.in_width > state.out_width) {
        state.in_width = floorf((float)state.out_width * state.scale_x + 0.5f);
    }
    if (state.in_height > state.out_height) {
        state.in_height = floorf((float)state.out_height * state.scale_y + 0.5f);
    }

    state.out_x =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT], NV_PVIDEO_POINT_OUT_X);
    state.out_y =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_POINT_OUT], NV_PVIDEO_POINT_OUT_Y);

    state.color_key_enabled =
        GET_MASK(d->pvideo.regs[NV_PVIDEO_FORMAT], NV_PVIDEO_FORMAT_DISPLAY);

    // Note: PVIDEO color keying ignores alpha.
    state.color_key = d->pvideo.regs[NV_PVIDEO_COLOR_KEY] & 0xFFFFFF;

    assert(state.offset + state.pitch * state.in_height <= state.limit);
    hwaddr end = state.base + state.offset + state.pitch * state.in_height;
    assert(end <= memory_region_size(d->vram));

    return state;
}

static void update_uniforms(PGRAPHState *pg, SurfaceBinding *surface)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    ShaderUniformLayout *l = &r->display.display_frag->push_constants;

    uniform4f(l, uniform_index(l, "source_transform"), 0, 0, 0, 0);
    int display_size_loc = uniform_index(l, "display_size");  // FIXME: Cache
    uniform2f(l, display_size_loc, r->display.width, r->display.height);

    VGADisplayParams vga_display_params;
    d->vga.get_params(&d->vga, &vga_display_params);
    int line_offset = vga_display_params.line_offset ?
                          surface->pitch / vga_display_params.line_offset :
                          1;
    int line_offset_loc = uniform_index(l, "line_offset");
    uniform1f(l, line_offset_loc, line_offset);

    PvideoState *pvideo = &r->display.pvideo.state;
    uniform1i(l, uniform_index(l, "pvideo_enable"), pvideo->enabled);
    if (pvideo->enabled) {
        uniform1i(l, uniform_index(l, "pvideo_color_key_enable"),
                  pvideo->color_key_enabled);
        uniform3f(
            l, uniform_index(l, "pvideo_color_key"),
            GET_MASK(pvideo->color_key, NV_PVIDEO_COLOR_KEY_RED) / 255.0,
            GET_MASK(pvideo->color_key, NV_PVIDEO_COLOR_KEY_GREEN) / 255.0,
            GET_MASK(pvideo->color_key, NV_PVIDEO_COLOR_KEY_BLUE) / 255.0);
        uniform2f(l, uniform_index(l, "pvideo_in_pos"), pvideo->in_s / 16.f,
                  pvideo->in_t / 8.f);
        uniform4f(l, uniform_index(l, "pvideo_pos"), pvideo->out_x,
                  pvideo->out_y, pvideo->out_width, pvideo->out_height);
        uniform4f(l, uniform_index(l, "pvideo_scale"), pvideo->scale_x,
                  pvideo->scale_y, 1.0f / pg->surface_scale_factor, 1.0);
    }
}

static void destroy_frame_image(PGRAPHVkState *r, struct PGRAPHVkFrameImage *f)
{
    if (f->image == VK_NULL_HANDLE) {
        return;
    }
    vkDestroyFramebuffer(r->device, f->framebuffer, NULL);
    vkDestroyImageView(r->device, f->view, NULL);
    vkDestroyImage(r->device, f->image, NULL);
    vkFreeMemory(r->device, f->memory, NULL);
    memset(f, 0, sizeof(*f));
}

static void create_frame_image(PGRAPHState *pg, struct PGRAPHVkFrameImage *f,
                               int width, int height)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        /* the display's render target, the crosshairs' copy destination,
         * the frontend's texture */
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vkCreateImage(r->device, &image_create_info, NULL, &f->image));

    VkMemoryRequirements memory_requirements;
    vkGetImageMemoryRequirements(r->device, f->image, &memory_requirements);
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memory_requirements.size,
        .memoryTypeIndex =
            pgraph_vk_get_memory_type(pg, memory_requirements.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VK_CHECK(vkAllocateMemory(r->device, &alloc_info, NULL, &f->memory));
    VK_CHECK(vkBindImageMemory(r->device, f->image, f->memory, 0));

    VkImageViewCreateInfo image_view_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = f->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = image_create_info.format,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.levelCount = 1,
        .subresourceRange.layerCount = 1,
    };
    VK_CHECK(vkCreateImageView(r->device, &image_view_create_info, NULL,
                               &f->view));
    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = r->display.render_pass,
        .attachmentCount = 1,
        .pAttachments = &f->view,
        .width = width,
        .height = height,
        .layers = 1,
    };
    VK_CHECK(vkCreateFramebuffer(r->device, &framebuffer_info, NULL,
                                 &f->framebuffer));
    f->width = width;
    f->height = height;
}

/*
 * The frame's command buffer, fence and semaphores, one set for each of
 * the frontend's sync indices (renderer.h PGRAPHVkFrameSync).
 */
static void create_frame_sync(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *disp = &r->display;

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = xemu_libretro_vk.queue_family,
    };
    VK_CHECK(vkCreateCommandPool(r->device, &pool_info, NULL,
                                 &disp->frame_pool));
    VkCommandBuffer cmds[XEMU_LIBRETRO_VK_FRAMES];
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = disp->frame_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = XEMU_LIBRETRO_VK_FRAMES,
    };
    VK_CHECK(vkAllocateCommandBuffers(r->device, &alloc_info, cmds));
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };
    for (int i = 0; i < XEMU_LIBRETRO_VK_FRAMES; i++) {
        struct PGRAPHVkFrameSync *s = &disp->sync[i];
        s->cmd = cmds[i];
        VK_CHECK(vkCreateFence(r->device, &fence_info, NULL, &s->fence));
        s->pending = false;
        if (!xemu_libretro_vk.queue_shared) {
            VK_CHECK(vkCreateSemaphore(r->device, &sem_info, NULL,
                                       &s->drawn));
            VK_CHECK(vkCreateSemaphore(r->device, &sem_info, NULL,
                                       &s->shown));
        }
    }
}

/* The frame's last submission, waited for: its command buffer and the
 * image can be recorded and drawn again. Our own fence, on our own
 * submission; by the time a sync index comes round again the frontend has
 * waited for its frame at that index, which came after, so this is
 * normally already signalled. */
static void wait_frame_sync(PGRAPHVkState *r, struct PGRAPHVkFrameSync *s)
{
    if (s->pending) {
        /* Retirement polling already proved completion for this exact
         * submission. Reset still belongs here, before command-buffer reuse. */
        if (!s->signalled) {
            VK_CHECK(vkWaitForFences(r->device, 1, &s->fence, VK_TRUE,
                                     UINT64_MAX));
        }
        VK_CHECK(vkResetFences(r->device, 1, &s->fence));
        s->pending = false;
        s->signalled = false;
        r->display.settled = MAX(r->display.settled, s->serial);
    }
}

/* Render-thread retirement query, never a CPU wait. The display can use
 * surface inputs after the main draw slots have already retired. */
void pgraph_vk_poll_display_retired(PGRAPHVkState *r)
{
    PGRAPHVkDisplayState *disp = &r->display;
    for (unsigned i = 0; i < ARRAY_SIZE(disp->sync); i++) {
        struct PGRAPHVkFrameSync *sync = &disp->sync[i];
        if (sync->pending && sync->serial > disp->settled &&
            vkGetFenceStatus(r->device, sync->fence) == VK_SUCCESS) {
            sync->signalled = true;
            disp->settled = sync->serial;
        }
    }
}

static void destroy_frame_sync(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *disp = &r->display;

    if (disp->frame_pool == VK_NULL_HANDLE) {
        return;
    }
    for (int i = 0; i < XEMU_LIBRETRO_VK_FRAMES; i++) {
        struct PGRAPHVkFrameSync *s = &disp->sync[i];
        wait_frame_sync(r, s);
        vkDestroyFence(r->device, s->fence, NULL);
        if (s->drawn != VK_NULL_HANDLE) {
            vkDestroySemaphore(r->device, s->drawn, NULL);
            vkDestroySemaphore(r->device, s->shown, NULL);
        }
    }
    vkDestroyCommandPool(r->device, disp->frame_pool, NULL);
    memset(disp->sync, 0, sizeof(disp->sync));
    disp->frame_pool = VK_NULL_HANDLE;
    disp->shown_pending = VK_NULL_HANDLE;
}

static void frame_image_barrier(VkCommandBuffer cmd, VkImage image,
                                VkImageLayout from, VkImageLayout to,
                                VkAccessFlags src_access,
                                VkAccessFlags dst_access,
                                VkPipelineStageFlags src_stage,
                                VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = src_access,
        .dstAccessMask = dst_access,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.levelCount = 1,
        .subresourceRange.layerCount = 1,
    };
    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1,
                         &barrier);
}

/* The crosshairs' pixels, copied into the frame: a black outline, then
 * player 1's red and player 2's blue (R8G8B8A8 as little-endian words) */
#define CROSSHAIR_PIXELS 1024
static const uint32_t crosshair_colors[3] = {
    0xFF000000u, 0xFF2828FFu, 0xFFFF8C28u,
};
static VkBuffer crosshair_buffer;
static VmaAllocation crosshair_allocation;

static bool create_crosshair_buffer(PGRAPHVkState *r)
{
    if (crosshair_buffer != VK_NULL_HANDLE) {
        return true;
    }
    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(crosshair_colors) * CROSSHAIR_PIXELS,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_MAPPED_BIT,
    };
    VmaAllocationInfo info;
    if (vmaCreateBuffer(r->allocator, &buffer_info, &alloc_info,
                        &crosshair_buffer, &crosshair_allocation,
                        &info) != VK_SUCCESS) {
        crosshair_buffer = VK_NULL_HANDLE;
        return false;
    }
    uint32_t *pixels = info.pMappedData;
    for (int c = 0; c < ARRAY_SIZE(crosshair_colors); c++) {
        for (int i = 0; i < CROSSHAIR_PIXELS; i++) {
            pixels[c * CROSSHAIR_PIXELS + i] = crosshair_colors[c];
        }
    }
    vmaFlushAllocation(r->allocator, crosshair_allocation, 0, VK_WHOLE_SIZE);
    return true;
}

static void destroy_crosshair_buffer(PGRAPHVkState *r)
{
    if (crosshair_buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(r->allocator, crosshair_buffer, crosshair_allocation);
        crosshair_buffer = VK_NULL_HANDLE;
    }
}

/* One bar of a crosshair, clipped to the frame, from the color's pixels */
static void crosshair_bar(VkCommandBuffer cmd, VkImage image, int width,
                          int height, int x, int y, int w, int h, int color)
{
    int x0 = MAX(x, 0), y0 = MAX(y, 0);
    int x1 = MIN(x + w, width), y1 = MIN(y + h, height);
    if (x1 <= x0 || y1 <= y0 || (x1 - x0) * (y1 - y0) > CROSSHAIR_PIXELS) {
        return;
    }
    VkBufferImageCopy region = {
        .bufferOffset = (VkDeviceSize)color * CROSSHAIR_PIXELS * sizeof(uint32_t),
        .bufferRowLength = (uint32_t)(x1 - x0),
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.layerCount = 1,
        .imageOffset = { x0, y0, 0 },
        .imageExtent = { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0), 1 },
    };
    vkCmdCopyBufferToImage(cmd, crosshair_buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

/* The gun games' crosshairs (ui/libretro-crosshair.h) over the frame, in
 * TRANSFER_DST_OPTIMAL: a plus sign a little larger than the picture's
 * fortieth, outlined in black so it shows on any background */
static void draw_crosshairs(PGRAPHVkState *r, VkCommandBuffer cmd,
                            VkImage image, int width, int height)
{
    if (!xemu_libretro_crosshairs[0].shown &&
        !xemu_libretro_crosshairs[1].shown) {
        return;
    }
    if (!create_crosshair_buffer(r)) {
        return;
    }
    const int arm = MIN(MAX(height / 40, 6), 40);
    const int thick = MIN(MAX(height / 240, 2), 6);

    for (int pass = 0; pass < 2; pass++) {
        /* the colored bars overwrite the outline's: ordered after it */
        frame_image_barrier(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);
        for (int p = 0; p < 2; p++) {
            const XemuLibretroCrosshair *c = &xemu_libretro_crosshairs[p];
            if (!c->shown) {
                continue;
            }
            const int cx = (int)(c->x * width), cy = (int)(c->y * height);
            const int edge = pass ? 0 : 1;
            const int color = pass ? 1 + p : 0;
            const int len = 2 * arm + 1 + 2 * edge, t = thick + 2 * edge;
            crosshair_bar(cmd, image, width, height, cx - arm - edge,
                          cy - t / 2, len, t, color);
            crosshair_bar(cmd, image, width, height, cx - t / 2,
                          cy - arm - edge, t, len, color);
        }
    }
}

/*
 * The display is drawn, on this thread, when the guest moves the scanout
 * (PCRTC_START) or the frontend asks for another look, into the next of
 * the renderer's own frame images, and published (display.published);
 * retro_run takes the last published one with no wait and no lock
 * (nv2a_vk_libretro_get_frame). The GPU is not waited for: the command
 * buffer starts with a barrier after any fragment reads of the image
 * (the frontend's earlier submissions on the shared queue) and ends with
 * the barrier to SHADER_READ_ONLY, which orders the frontend's later
 * submission after the draw. Where the renderer has a queue of its own
 * (ui/libretro.c), the frame's command buffer runs on the frontend's
 * queue instead, after a semaphore the renderer's queue signals once the
 * frame's draws are in, and the renderer's next submission waits for the
 * frame to have been drawn before it draws to the surface again
 * (pgraph_vk_queue_submit). A slot is drawn again only after every other
 * has been, XEMU_LIBRETRO_VK_FRAMES renders later, by when the frontend
 * has long moved on from it.
 */
static struct PGRAPHVkFrameImage *frame_image_for_slot(PGRAPHState *pg,
                                                       unsigned slot)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *disp = &r->display;
    struct PGRAPHVkFrameImage *f = &disp->frames[slot];

    disp->frame_count++;
    for (int i = 0; i < ARRAY_SIZE(disp->retired); i++) {
        if (disp->retired[i].image != VK_NULL_HANDLE &&
            disp->frame_count >= disp->retired[i].retire_at) {
            destroy_frame_image(r, &disp->retired[i]);
        }
    }

    if (f->image != VK_NULL_HANDLE &&
        (f->width != disp->width || f->height != disp->height)) {
        /* A frame the frontend was handed may still be on screen: kept until
         * every sync index has come round twice */
        bool kept = false;
        for (int i = 0; i < ARRAY_SIZE(disp->retired) && !kept; i++) {
            if (disp->retired[i].image == VK_NULL_HANDLE) {
                disp->retired[i] = *f;
                disp->retired[i].retire_at =
                    disp->frame_count + 2 * XEMU_LIBRETRO_VK_FRAMES;
                kept = true;
            }
        }
        if (!kept) {
            destroy_frame_image(r, f);
        }
        memset(f, 0, sizeof(*f));
    }
    if (f->image == VK_NULL_HANDLE) {
        create_frame_image(pg, f, disp->width, disp->height);
    }
    return f;
}

/* After the display's render pass: the crosshairs, then the layout the
 * frontend samples in, ordered before any fragment shader that follows in
 * submission order */
static void frame_image_finish(PGRAPHVkState *r, VkCommandBuffer cmd,
                               struct PGRAPHVkFrameImage *f)
{
    PGRAPHVkDisplayState *disp = &r->display;

    if (xemu_libretro_crosshairs[0].shown ||
        xemu_libretro_crosshairs[1].shown) {
        frame_image_barrier(cmd, f->image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);
        draw_crosshairs(r, cmd, f->image, disp->width, disp->height);
        frame_image_barrier(cmd, f->image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    } else {
        frame_image_barrier(cmd, f->image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
}

/* The frame's command buffer, submitted: on the shared queue as any other
 * submission; on the frontend's queue, between the two semaphores, where
 * the renderer draws on a queue of its own */
static void submit_frame(PGRAPHVkState *r, struct PGRAPHVkFrameSync *s)
{
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &s->cmd,
    };

    if (xemu_libretro_vk.queue_shared) {
        pgraph_vk_queue_submit(r, 1, &submit, s->fence);
    } else {
        /* the renderer's queue: everything submitted so far, then the
         * signal */
        VkSubmitInfo drawn = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &s->drawn,
        };
        pgraph_vk_queue_submit(r, 1, &drawn, VK_NULL_HANDLE);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &s->drawn;
        submit.pWaitDstStageMask = &wait_stage;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &s->shown;
        xemu_libretro_vk_lock_frontend_queue();
        VK_CHECK(vkQueueSubmit(xemu_libretro_vk.frontend_queue, 1, &submit,
                               s->fence));
        xemu_libretro_vk_unlock_frontend_queue();
        /* the renderer's next submission waits for this one */
        r->display.shown_pending = s->shown;
    }
    nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_5);
    s->serial = ++r->display.submitted;
    s->signalled = false;
    s->pending = true;
}

static void destroy_frame_images(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    for (int i = 0; i < ARRAY_SIZE(r->display.frames); i++) {
        destroy_frame_image(r, &r->display.frames[i]);
    }
    for (int i = 0; i < ARRAY_SIZE(r->display.retired); i++) {
        destroy_frame_image(r, &r->display.retired[i]);
    }
}

static void render_display(PGRAPHState *pg, SurfaceBinding *surface)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *disp = &r->display;

    struct PGRAPHVkScanoutMode *mode = &r->scanout_modes[r->scanout_mode];
    bool direct = r->scanout_active && surface == &mode->out;
    unsigned layer_count = direct ? r->scanout_layer_count : 0;
    bool layers_pending = false;
    for (unsigned i = 0; i < layer_count; i++) {
        SurfaceBinding *s = r->scanout_layers[i].surface;
        /* Packed spans split into consecutive scissors sharing one image. */
        if (i && r->scanout_layers[i - 1].surface == s) continue;
        layers_pending |= s->draw_time >= r->command_buffer_start_time ||
                          pgraph_vk_surface_borrow_pending(r, s);
    }
    if (r->in_command_buffer &&
        (surface->draw_time >= r->command_buffer_start_time ||
         pgraph_vk_surface_borrow_pending(r, surface) || layers_pending)) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_PRESENTING);
    }

    pgraph_vk_upload_surface_data(d, surface, !tcg_enabled());

    disp->pvideo.state = get_pvideo_state(pg);
    if (disp->pvideo.state.enabled) {
        upload_pvideo_image(pg, disp->pvideo.state);
    }

    update_uniforms(pg, surface);

    /* The render target: the display image, or on the frontend's device
     * the frame image of its current sync index, in that index's own
     * command buffer */
    VkImage target = disp->image;
    VkFramebuffer framebuffer = disp->framebuffer;
    VkCommandBuffer cmd;
    struct PGRAPHVkFrameImage *f = NULL;
    struct PGRAPHVkFrameSync *sync = NULL;
    unsigned slot = 0;
    if (on_frontend_device()) {
        if (surface->vram_addr != disp->last_surface_addr ||
            surface->draw_time != disp->draw_time) {
            disp->guest_frame_count++;
        }
        disp->last_surface_addr = surface->vram_addr;
        slot = disp->next_slot;
        disp->next_slot = (slot + 1) % XEMU_LIBRETRO_VK_FRAMES;
        sync = &disp->sync[slot];
        wait_frame_sync(r, sync);
        f = frame_image_for_slot(pg, slot);
        target = f->image;
        framebuffer = f->framebuffer;
        cmd = sync->cmd;
        VkCommandBufferBeginInfo begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        VK_CHECK(vkBeginCommandBuffer(cmd, &begin_info));
    } else
    {
        cmd = pgraph_vk_begin_single_time_commands(pg);
    }
    pgraph_vk_begin_debug_marker(r, cmd, RGBA_YELLOW,
        "Display Surface %08"HWADDR_PRIx, surface->vram_addr);

    /* In-place sampling: a surface sampled in place is read and left in
     * SHADER_READ, the layout its recorded draws expect. */
    bool sampled_in_place = surface->in_shader_read;
    if (!sampled_in_place) {
        pgraph_vk_transition_image_layout(
            pg, cmd, surface->image, surface->host_fmt.vk_format,
            pgraph_vk_surface_base_layout(surface),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    for (unsigned i = 0; i < layer_count; i++) {
        SurfaceBinding *s = r->scanout_layers[i].surface;
        /* Packed spans split into consecutive scissors sharing one image. */
        if (i && r->scanout_layers[i - 1].surface == s) continue;
        if (!s->in_shader_read) {
            pgraph_vk_transition_image_layout(pg, cmd, s->image,
                s->host_fmt.vk_format, pgraph_vk_surface_base_layout(s),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
    if (f) {
        /* after the frontend's fragment reads of the slot's last frame,
         * earlier on the queue */
        frame_image_barrier(cmd, target, VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_ACCESS_SHADER_READ_BIT,
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    } else {
        pgraph_vk_transition_image_layout(
            pg, cmd, target, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }

    VkRenderPassBeginInfo render_pass_begin_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = disp->render_pass,
        .framebuffer = framebuffer,
        .renderArea.extent.width = disp->width,
        .renderArea.extent.height = disp->height,
    };
    vkCmdBeginRenderPass(cmd, &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      disp->pipeline);

    VkViewport viewport = {
        .width = disp->width,
        .height = disp->height,
        .minDepth = 0.0,
        .maxDepth = 1.0,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {
        .extent.width = disp->width,
        .extent.height = disp->height,
    };

    ShaderUniformLayout *uniforms = &disp->display_frag->push_constants;
    int source_loc = uniform_index(uniforms, "source_transform");
    /* Each source has its own descriptor set, reserved at init for eight
     * worst-case frames. Its background rectangles share immutable bindings. */
    const unsigned sets_per_frame = PGRAPH_VK_SCANOUT_DIRECT_LAYERS + 1;
    unsigned set_base = ((disp->descriptor_index / sets_per_frame + 1) % 8) *
                        sets_per_frame;
    VkPipeline bound = disp->pipeline;
    for (unsigned i = 0; i <= layer_count; i++) {
        VkPipeline pipeline = i && r->scanout_layers[i - 1].reinterpret ?
                              disp->byte_pipeline : disp->pipeline;
        if (pipeline != bound) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            bound = pipeline;
        }
        SurfaceBinding *source = surface;
        if (i) {
            struct PGRAPHVkScanoutLayer *layer = &r->scanout_layers[i - 1];
            PGRAPHScanoutRect *p = &layer->rect;
            source = layer->surface;
            unsigned scale = pg->surface_scale_factor;
            scissor.offset = (VkOffset2D){ p->dx * scale, p->dy * scale };
            scissor.extent = (VkExtent2D){ p->width * scale, p->height * scale };
            uniform4f(uniforms, source_loc,
                ((float)p->sx - p->dx) * scale,
                ((float)p->sy - p->dy) * scale, 1, 1);
            if (layer->reinterpret) {
                const PGRAPHScanout *scan = &mode->cache.scan;
                uint32_t mx = 0, my = 0, mz;
                if (source->swizzle) generate_swizzle_masks(source->width,
                    source->height, 1, &mx, &my, &mz);
                unsigned src_kind = source->host_fmt.vk_format == VK_FORMAT_B8G8R8A8_UNORM ? 1 :
                    source->host_fmt.vk_format == VK_FORMAT_R5G6B5_UNORM_PACK16 ? 2 :
                    source->host_fmt.vk_format == VK_FORMAT_A1R5G5B5_UNORM_PACK16 ? 3 :
                    source->host_fmt.vk_format == VK_FORMAT_R8_UNORM ? 4 : 5;
                unsigned dst_kind = scan->bpp == 4 ? 1 :
                    mode->out.host_fmt.vk_format == VK_FORMAT_R5G6B5_UNORM_PACK16 ? 2 : 3;
                uint32_t bytes[4] = { (uint32_t)(source->vram_addr - scan->start),
                    source->swizzle ? mx : source->pitch, my,
                    src_kind | (dst_kind << 8) | (source->swizzle ? 65536 : 0) |
                        (layer->holes ? 131072 : 0) };
                memcpy(uniform_ptr(uniforms, uniform_index(uniforms, "source_bytes")),
                    bytes, sizeof(bytes));
                uniform4f(uniforms, source_loc, scan->pitch, 0, scale, -1);
            }
        } else if (direct) {
            float inv_scale = 1.0f / pg->surface_scale_factor;
            uniform4f(uniforms, source_loc, 0, 0, inv_scale, inv_scale);
        }
        if (i || !direct || !mode->trim_background) {
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }
        disp->descriptor_index = set_base + i;
        update_descriptor_set(pg, source);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            disp->pipeline_layout, 0, 1,
            &disp->descriptor_sets[disp->descriptor_index], 0, NULL);
        vkCmdPushConstants(cmd, disp->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
            0, uniforms->total_size, uniforms->allocation);
        if (!i && direct && mode->trim_background) {
            /* Later opaque tile draws cover the excluded pixels. Absolute
             * shader coordinates keep PVIDEO keying identical at every edge.
             * All rectangles share this source's descriptor/push constants. */
            for (unsigned j = 0; j < mode->uncovered.count; j++) {
                PGRAPHScanoutRect *p = &mode->uncovered.rects[j];
                unsigned scale = pg->surface_scale_factor;
                scissor.offset = (VkOffset2D){ p->dx * scale, p->dy * scale };
                scissor.extent = (VkExtent2D){ p->width * scale, p->height * scale };
                vkCmdSetScissor(cmd, 0, 1, &scissor);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        } else {
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }
    }

    vkCmdEndRenderPass(cmd);

    if (!sampled_in_place) {
        pgraph_vk_transition_image_layout(
            pg, cmd, surface->image, surface->host_fmt.vk_format,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            pgraph_vk_surface_base_layout(surface));
    }

    for (unsigned i = 0; i < layer_count; i++) {
        SurfaceBinding *s = r->scanout_layers[i].surface;
        /* Packed spans split into consecutive scissors sharing one image. */
        if (i && r->scanout_layers[i - 1].surface == s) continue;
        if (!s->in_shader_read) {
            pgraph_vk_transition_image_layout(pg, cmd, s->image,
                s->host_fmt.vk_format, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                pgraph_vk_surface_base_layout(s));
        }
    }
    if (f) {
        frame_image_finish(r, cmd, f);
        pgraph_vk_end_debug_marker(r, cmd);
        VK_CHECK(vkEndCommandBuffer(cmd));
        submit_frame(r, sync);
        f->ready = true;
        /* the frontend's thread takes it from here */
        retro_atomic_store_release_64(&disp->published,
            (int64_t)(((uint64_t)disp->frame_count << 8) | slot));
    } else
    {
        pgraph_vk_transition_image_layout(pg, cmd, disp->image,
                                          VK_FORMAT_R8G8B8_UNORM,
                                          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        pgraph_vk_end_debug_marker(r, cmd);
        pgraph_vk_end_single_time_commands(pg, cmd);
        nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT_5);
    }

    disp->draw_time = surface->draw_time;
}

static void create_surface_sampler(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

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

    VK_CHECK(vkCreateSampler(r->device, &sampler_create_info, NULL,
                             &r->display.sampler));
}

static void destroy_surface_sampler(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroySampler(r->device, r->display.sampler, NULL);
    r->display.sampler = VK_NULL_HANDLE;
}

void pgraph_vk_init_display(PGRAPHState *pg)
{
    create_descriptor_pool(pg);
    create_descriptor_set_layout(pg);
    create_descriptor_sets(pg);
    create_render_pass(pg);
    create_display_pipeline(pg);
    create_surface_sampler(pg);
}

void pgraph_vk_finalize_display(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* Retire display reads before destroying any of their input images. */
    destroy_frame_sync(pg);
    destroy_pvideo_image(pg);

    if (r->display.image != VK_NULL_HANDLE) {
        destroy_current_display_image(pg);
    }
    destroy_frame_images(pg);
    destroy_crosshair_buffer(pg->vk_renderer_state);

    destroy_surface_sampler(pg);
    destroy_display_pipeline(pg);
    destroy_render_pass(pg);
    destroy_descriptor_set_layout(pg);
    destroy_descriptor_pool(pg);
}

/* The scanout moved (PCRTC_START), or the frontend found nothing new and
 * asks for another look: the display is drawn again when the front buffer
 * is another surface, or one drawn to since, or carries the video
 * overlay (read from guest RAM at each draw). On this thread, from
 * process_pending. */
void pgraph_vk_present_scanout(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;
    PGRAPHVkDisplayState *disp = &r->display;

    if (!on_frontend_device()) {
        return;
    }
    VGADisplayParams vga_display_params;
    d->vga.get_params(&d->vga, &vga_display_params);
    SurfaceBinding *surface = pgraph_vk_surface_get_within(
        d, d->pcrtc.start + vga_display_params.line_offset);
    if (surface == NULL || !surface->color) {
        surface = pgraph_vk_compose_scanout(d);
    }
    if (surface == NULL || !surface->color || !surface->width ||
        !surface->height) {
        return;
    }
    /* On screen: not an old surface to expire (expire_old_surfaces), even
     * when the game shows it without drawing to it - a loading screen, a
     * movie the CPU writes. The display path of retro_run did this at each
     * frame before the display was drawn here. */
    surface->frame_time = pg->frame_time;

    /* The same picture as the last one drawn: the same surface, nothing
     * drawn to it nor written by the CPU since, the same display mode, no
     * video overlay (read from guest RAM at each draw) */
    unsigned int width = 0, height = 0;
    d->vga.get_resolution(&d->vga, (int *)&width, (int *)&height);
    const bool interlaced = d->vga.cr[NV_PRMCIO_INTERLACE_MODE] !=
                            NV_PRMCIO_INTERLACE_MODE_DISABLED;
    if (surface->vram_addr == disp->last_surface_addr &&
        surface->draw_time == disp->draw_time &&
        !surface->upload_pending &&
        vga_display_params.line_offset == disp->last_line_offset &&
        width == disp->last_mode_width && height == disp->last_mode_height &&
        interlaced == disp->last_interlaced &&
        pg->surface_scale_factor == disp->last_scale &&
        !get_pvideo_state(pg).enabled) {
        return;
    }
    disp->last_line_offset = vga_display_params.line_offset;
    disp->last_mode_width = width;
    disp->last_mode_height = height;
    disp->last_interlaced = interlaced;
    disp->last_scale = pg->surface_scale_factor;
    pgraph_vk_render_display(pg, surface);
}

void pgraph_vk_render_display(PGRAPHState *pg, SurfaceBinding *surface)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;

    VGADisplayParams vga_display_params;
    d->vga.get_params(&d->vga, &vga_display_params);

    if (!surface) {
        surface = pgraph_vk_surface_get_within(
            d, d->pcrtc.start + vga_display_params.line_offset);
    }
    if (surface == NULL || !surface->color) {
        surface = pgraph_vk_compose_scanout(d);
    }
    if (surface == NULL || !surface->color || !surface->width ||
        !surface->height) {
        return;
    }

    unsigned int width = 0, height = 0;
    d->vga.get_resolution(&d->vga, (int *)&width, (int *)&height);

    /* Adjust viewport height for interlaced mode, used only in 1080i */
    if (d->vga.cr[NV_PRMCIO_INTERLACE_MODE] != NV_PRMCIO_INTERLACE_MODE_DISABLED) {
        height *= 2;
    }

    /* Clamp display to surface dimensions when CRTC area exceeds the
     * framebuffer (e.g. PAL 720x576 CRTC with 640x480 render target).
     * On real hardware the TV encoder fills the overscan with black;
     * xemu has no TV encoder so we just match the surface size. */
    if (width > surface->width)   width  = surface->width;
    if (height > surface->height) height = surface->height;

    pgraph_apply_scaling_factor(pg, &width, &height);

    PGRAPHVkDisplayState *disp = &r->display;
    if (on_frontend_device()) {
        /* no display image: the frame images are the render targets */
        disp->width = width;
        disp->height = height;
        if (disp->frame_pool == VK_NULL_HANDLE) {
            create_frame_sync(pg);
        }
    } else
    if (!disp->image || disp->width != width || disp->height != height) {
        create_display_image(pg, width, height);
    }

    render_display(pg, surface);
}
