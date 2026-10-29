/*
 * Geforce NV2A PGRAPH Vulkan Renderer: the unswizzle on the GPU
 *
 * A swizzled (Morton order) texture level or surface in the Xbox's RAM,
 * read straight from the RAM's buffer (buffer.c create_vram_buffer) by a
 * compute shader that writes it out linear, x fastest, into the output
 * ring (BUFFER_UNSWIZZLE_OUT), from where the single-time command buffer
 * copies it into the image. An 8-bit palette texture is looked up on the
 * way. Nothing of it passes through the CPU.
 *
 * The other way, a surface read back is swizzled into the RAM by the GPU
 * as well (pgraph_vk_swizzle_to_ram): from the ring, where the read-back
 * copied it linear, into its place in the RAM's buffer.
 *
 * Two compute pipelines and one descriptor set (the two buffers, whole),
 * made at init: a level or a surface is a dispatch with push constants.
 *
 * Copyright (c) 2026 Réda Chérif-Touil
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

#include "hw/xbox/nv2a/nv2a_int.h"
#include "hw/xbox/nv2a/pgraph/swizzle.h"
#include "renderer.h"

#define UNSWIZZLE_WORKGROUP 256

/* One invocation writes one 32-bit word of the output: the texels it
 * holds (4, 2 or 1 of 1, 2 or 4 bytes; one through the palette), each
 * read at its swizzled offset: x, y and z spread over the offset's bits
 * by the masks (swizzle.c generate_swizzle_masks). */
static const char *unswizzle_glsl =
    "#version 450\n"
    "layout(local_size_x = 256) in;\n"
    "layout(push_constant) uniform PC {\n"
    "    uint src;      /* the level's byte offset in the RAM */\n"
    "    uint dst;      /* the output's byte offset in the ring, 4-aligned */\n"
    "    uint width;\n"
    "    uint height;\n"
    "    uint depth;\n"
    "    uint bpp;      /* bytes a texel, in the RAM: 1, 2 or 4 */\n"
    "    uint mask_x;\n"
    "    uint mask_y;\n"
    "    uint mask_z;\n"
    "    uint n_words;  /* words of output */\n"
    "    uint palette;  /* byte offset of 256 words, or 0: none */\n"
    "} pc;\n"
    "layout(std430, set = 0, binding = 0) readonly buffer Vram { uint w[]; } vram;\n"
    "layout(std430, set = 0, binding = 1) writeonly buffer Out { uint w[]; } outb;\n"
    "uint spread(uint v, uint mask) {\n"
    "    uint r = 0u, bit = 1u;\n"
    "    while (mask != 0u) {\n"
    "        uint low = mask & (0u - mask);\n"
    "        if ((v & bit) != 0u) r |= low;\n"
    "        mask &= ~low;\n"
    "        bit <<= 1u;\n"
    "    }\n"
    "    return r;\n"
    "}\n"
    "uint texel(uint t) {\n"
    "    uint x = t % pc.width;\n"
    "    uint y = (t / pc.width) % pc.height;\n"
    "    uint z = t / (pc.width * pc.height);\n"
    "    uint sw = spread(x, pc.mask_x) | spread(y, pc.mask_y) | spread(z, pc.mask_z);\n"
    "    uint off = pc.src + sw * pc.bpp;\n"
    "    uint word = vram.w[off >> 2u];\n"
    "    if (pc.bpp == 4u) return word;\n"
    "    if (pc.bpp == 2u) return (word >> ((off & 2u) * 8u)) & 0xffffu;\n"
    "    uint v = (word >> ((off & 3u) * 8u)) & 0xffu;\n"
    "    if (pc.palette != 0u) return vram.w[(pc.palette >> 2u) + v];\n"
    "    return v;\n"
    "}\n"
    "void main() {\n"
    "    uint i = gl_GlobalInvocationID.x;\n"
    "    if (i >= pc.n_words) return;\n"
    "    uint out_bpp = pc.palette != 0u ? 4u : pc.bpp;\n"
    "    uint per_word = 4u / out_bpp;\n"
    "    uint first = i * per_word;\n"
    "    uint r = 0u;\n"
    "    for (uint k = 0u; k < per_word; k++) {\n"
    "        uint t = first + k;\n"
    "        if (t < pc.width * pc.height * pc.depth) {\n"
    "            r |= texel(t) << (k * out_bpp * 8u);\n"
    "        }\n"
    "    }\n"
    "    outb.w[(pc.dst >> 2u) + i] = r;\n"
    "}\n";

/* One invocation writes one 32-bit word of the surface in the RAM: the
 * texels at the swizzled offsets it covers, each read from the linear copy
 * in the ring at its x and y, taken out of the offset by the masks. A
 * swizzled surface's sides are powers of two, so the words cover it
 * exactly. */
static const char *swizzle_glsl =
    "#version 450\n"
    "layout(local_size_x = 256) in;\n"
    "layout(push_constant) uniform PC {\n"
    "    uint src;      /* the linear copy's byte offset in the ring, 4-aligned */\n"
    "    uint dst;      /* the surface's byte offset in the RAM, 4-aligned */\n"
    "    uint width;\n"
    "    uint height;\n"
    "    uint depth;\n"
    "    uint bpp;      /* bytes a texel: 2 or 4 */\n"
    "    uint mask_x;\n"
    "    uint mask_y;\n"
    "    uint mask_z;\n"
    "    uint n_words;  /* words of output */\n"
    "    uint palette;\n"
    "} pc;\n"
    "layout(std430, set = 0, binding = 0) writeonly buffer Vram { uint w[]; } vram;\n"
    "layout(std430, set = 0, binding = 1) readonly buffer Lin { uint w[]; } lin;\n"
    "uint compact(uint s, uint mask) {\n"
    "    uint r = 0u, bit = 1u;\n"
    "    while (mask != 0u) {\n"
    "        uint low = mask & (0u - mask);\n"
    "        if ((s & low) != 0u) r |= bit;\n"
    "        mask &= ~low;\n"
    "        bit <<= 1u;\n"
    "    }\n"
    "    return r;\n"
    "}\n"
    "void main() {\n"
    "    uint i = gl_GlobalInvocationID.x;\n"
    "    if (i >= pc.n_words) return;\n"
    "    uint per_word = 4u / pc.bpp;\n"
    "    uint r = 0u;\n"
    "    for (uint k = 0u; k < per_word; k++) {\n"
    "        uint s = i * per_word + k;\n"
    "        uint x = compact(s, pc.mask_x);\n"
    "        uint y = compact(s, pc.mask_y);\n"
    "        uint off = pc.src + (y * pc.width + x) * pc.bpp;\n"
    "        uint word = lin.w[off >> 2u];\n"
    "        uint v = pc.bpp == 4u ? word : (word >> ((off & 2u) * 8u)) & 0xffffu;\n"
    "        r |= v << (k * pc.bpp * 8u);\n"
    "    }\n"
    "    vram.w[(pc.dst >> 2u) + i] = r;\n"
    "}\n";

struct UnswizzlePush {
    uint32_t src, dst, width, height, depth, bpp;
    uint32_t mask_x, mask_y, mask_z, n_words, palette;
};

void pgraph_vk_init_unswizzle(PGRAPHState *pg)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;

    memset(&r->unswizzle, 0, sizeof(r->unswizzle));
    if (r->vram_buffer == VK_NULL_HANDLE ||
        memory_region_size(d->vram) >
            r->device_props.limits.maxStorageBufferRange ||
        r->storage_buffers[BUFFER_UNSWIZZLE_OUT].buffer_size >
            r->device_props.limits.maxStorageBufferRange) {
        return;
    }

    VkDescriptorSetLayoutBinding bindings[2];
    for (int i = 0; i < 2; i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(r->device, &layout_info, NULL,
                                         &r->unswizzle.set_layout));

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 2,
    };
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
        .maxSets = 1,
    };
    VK_CHECK(vkCreateDescriptorPool(r->device, &pool_info, NULL,
                                    &r->unswizzle.pool));
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = r->unswizzle.pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &r->unswizzle.set_layout,
    };
    VK_CHECK(vkAllocateDescriptorSets(r->device, &alloc_info,
                                      &r->unswizzle.set));

    VkDescriptorBufferInfo buffers[2] = {
        { .buffer = r->vram_buffer, .range = VK_WHOLE_SIZE },
        { .buffer = r->storage_buffers[BUFFER_UNSWIZZLE_OUT].buffer,
          .range = VK_WHOLE_SIZE },
    };
    VkWriteDescriptorSet writes[2];
    for (int i = 0; i < 2; i++) {
        writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = r->unswizzle.set,
            .dstBinding = i,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo = &buffers[i],
        };
    }
    vkUpdateDescriptorSets(r->device, 2, writes, 0, NULL);

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = sizeof(struct UnswizzlePush),
    };
    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &r->unswizzle.set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    VK_CHECK(vkCreatePipelineLayout(r->device, &pipeline_layout_info, NULL,
                                    &r->unswizzle.layout));

    ShaderModuleInfo *module = pgraph_vk_create_shader_module_from_glsl(
        r, VK_SHADER_STAGE_COMPUTE_BIT, unswizzle_glsl);
    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = r->unswizzle.layout,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .pName = "main",
            .module = module->module,
        },
    };
    VK_CHECK(vkCreateComputePipelines(r->device, r->vk_pipeline_cache, 1,
                                      &pipeline_info, NULL,
                                      &r->unswizzle.pipeline));
    pgraph_vk_destroy_shader_module(r, module);

    module = pgraph_vk_create_shader_module_from_glsl(
        r, VK_SHADER_STAGE_COMPUTE_BIT, swizzle_glsl);
    pipeline_info.stage.module = module->module;
    VK_CHECK(vkCreateComputePipelines(r->device, r->vk_pipeline_cache, 1,
                                      &pipeline_info, NULL,
                                      &r->unswizzle.swizzle_pipeline));
    pgraph_vk_destroy_shader_module(r, module);
    r->unswizzle.ready = true;
}

void pgraph_vk_finalize_unswizzle(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (r->unswizzle.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(r->device, r->unswizzle.pipeline, NULL);
    }
    if (r->unswizzle.swizzle_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(r->device, r->unswizzle.swizzle_pipeline, NULL);
    }
    if (r->unswizzle.layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(r->device, r->unswizzle.layout, NULL);
    }
    if (r->unswizzle.pool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(r->device, r->unswizzle.pool, NULL);
    }
    if (r->unswizzle.set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(r->device, r->unswizzle.set_layout,
                                     NULL);
    }
    memset(&r->unswizzle, 0, sizeof(r->unswizzle));
}

bool pgraph_vk_unswizzle(PGRAPHState *pg, VkCommandBuffer cmd,
                         hwaddr src_offset, unsigned int width,
                         unsigned int height, unsigned int depth,
                         unsigned int bpp, hwaddr palette_offset,
                         VkDeviceSize *out_offset)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!r->unswizzle.ready || (bpp != 1 && bpp != 2 && bpp != 4) ||
        (palette_offset && bpp != 1) || (src_offset % bpp) ||
        (palette_offset % 4)) {
        return false;
    }
    size_t texels = (size_t)width * height * depth;
    size_t out_bpp = palette_offset ? 4 : bpp;
    size_t out_size = ROUND_UP(texels * out_bpp, 4);
    if (!texels || src_offset + texels * bpp > memory_region_size(d->vram) ||
        (palette_offset &&
         palette_offset + 256 * 4 > memory_region_size(d->vram))) {
        return false;
    }
    uint32_t n_words = out_size / 4;
    uint32_t groups = DIV_ROUND_UP(n_words, UNSWIZZLE_WORKGROUP);
    if (groups > r->device_props.limits.maxComputeWorkGroupCount[0]) {
        return false;
    }
    VkDeviceSize dst;
    if (!pgraph_vk_ring_alloc(pg, PGRAPH_VK_RING_UNSWIZZLE, out_size, 16,
                              &dst)) {
        return false;
    }

    struct UnswizzlePush push = {
        .src = src_offset,
        .dst = dst,
        .width = width,
        .height = height,
        .depth = depth,
        .bpp = bpp,
        .n_words = n_words,
        .palette = palette_offset,
    };
    generate_swizzle_masks(width, height, depth, &push.mask_x, &push.mask_y,
                           &push.mask_z);

    /* The guest's writes to its RAM before the shader reads it; the copies
     * of earlier submissions that read the ring before this writes it */
    VkBufferMemoryBarrier before[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = r->vram_buffer,
            .size = VK_WHOLE_SIZE,
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = r->storage_buffers[BUFFER_UNSWIZZLE_OUT].buffer,
            .offset = dst,
            .size = out_size,
        },
    };
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_HOST_BIT |
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 2,
                         before, 0, NULL);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                      r->unswizzle.pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            r->unswizzle.layout, 0, 1, &r->unswizzle.set, 0,
                            NULL);
    vkCmdPushConstants(cmd, r->unswizzle.layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(push), &push);
    vkCmdDispatch(cmd, groups, 1, 1);
    nv2a_profile_inc_counter(NV2A_PROF_SURF_SWIZZLE);

    *out_offset = dst;
    return true;
}

void pgraph_vk_unswizzle_done(PGRAPHState *pg, VkCommandBuffer cmd)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkBufferMemoryBarrier after = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = r->storage_buffers[BUFFER_UNSWIZZLE_OUT].buffer,
        .size = VK_WHOLE_SIZE,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &after,
                         0, NULL);
}

bool pgraph_vk_swizzle_possible(PGRAPHState *pg, hwaddr dst_offset,
                                unsigned int width, unsigned int height,
                                unsigned int bpp)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    size_t size = (size_t)width * height * bpp;

    return r->unswizzle.ready && (bpp == 2 || bpp == 4) && size &&
           size % 4 == 0 && dst_offset % 4 == 0 &&
           dst_offset + size <= memory_region_size(d->vram) &&
           DIV_ROUND_UP(size / 4, UNSWIZZLE_WORKGROUP) <=
               r->device_props.limits.maxComputeWorkGroupCount[0] &&
           size <= r->storage_buffers[BUFFER_UNSWIZZLE_OUT].buffer_size;
}

void pgraph_vk_swizzle_to_ram(PGRAPHState *pg, VkCommandBuffer cmd,
                              VkDeviceSize src_offset, hwaddr dst_offset,
                              unsigned int width, unsigned int height,
                              unsigned int bpp)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    size_t size = (size_t)width * height * bpp;

    struct UnswizzlePush push = {
        .src = src_offset,
        .dst = dst_offset,
        .width = width,
        .height = height,
        .depth = 1,
        .bpp = bpp,
        .n_words = size / 4,
    };
    generate_swizzle_masks(width, height, 1, &push.mask_x, &push.mask_y,
                           &push.mask_z);

    /* The linear copy's writes into the ring before the shader reads it */
    VkBufferMemoryBarrier before = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = r->storage_buffers[BUFFER_UNSWIZZLE_OUT].buffer,
        .offset = src_offset,
        .size = size,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1,
                         &before, 0, NULL);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                      r->unswizzle.swizzle_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            r->unswizzle.layout, 0, 1, &r->unswizzle.set, 0,
                            NULL);
    vkCmdPushConstants(cmd, r->unswizzle.layout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(push), &push);
    vkCmdDispatch(cmd, DIV_ROUND_UP(push.n_words, UNSWIZZLE_WORKGROUP), 1, 1);
    nv2a_profile_inc_counter(NV2A_PROF_SURF_SWIZZLE);
}
