/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_VK_SUN_SHADOW_PASS_H
#define NV2A_VK_SUN_SHADOW_PASS_H

#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <volk.h>
#include "sun-shadow-material.h"

/* Opt-in caster renderer. No normal shader key, descriptor set or renderer
 * dispatch is changed. Input streams and descriptors borrow GPU versions. */
static const char pgraph_vk_sun_vertex_glsl[] =
    "#version 450\n"
    "layout(location=0) in vec4 position;\n"
    "layout(push_constant) uniform Shadow {\n"
    " vec4 r0; vec4 r1; vec4 r2; vec4 r3;\n"
    "} light;\n"
    "void main() { gl_Position = position.x*light.r0 + position.y*light.r1 + "
    "position.z*light.r2 + position.w*light.r3; }\n";

typedef struct PgraphVkSunPass {
    VkDevice device;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    uint32_t stride, position_bytes;
    PgraphVkSunVariant variant;
    uint32_t weight_bytes;
} PgraphVkSunPass;

typedef struct PgraphVkSunDraw {
    /* Borrow immutable GPU versions, never guest pointers. The recording
     * owner must protect these buffers against overwrite/recycling from
     * before this call until the submission's final consumer completes.
     * vertex_size/index_size are available bytes AFTER their binding offsets.
     * Indexed vertex bounds must already have been validated by the owner;
     * this layer does not read or copy index payloads to revalidate them. */
    VkBuffer vertex, index;
    VkDeviceSize vertex_offset, vertex_size, index_offset, index_size;
    VkIndexType index_type;
    uint32_t first, count;
    int32_t vertex_base;
    float world_to_light[16];
    VkBuffer weights, texcoords;
    VkDeviceSize weight_offset, weight_size, uv_offset, uv_size;
    /* Immutable descriptor set: binding 0 palette UBO (256 bytes), binding
     * 1 alpha texture/sampler when used. The owner protects the set, buffer,
     * sampler, view AND sampled content version through final completion.
     * Keeping an image alive does not protect it from later surface writes. */
    VkDescriptorSet material;
    uint32_t alpha_reference, alpha_compare;
} PgraphVkSunDraw;

/* GPU-owner only, after every recording/submission using the pass has
 * completed or been discarded. No queue/device idle or implicit wait. */
static inline void pgraph_vk_sun_pass_destroy(PgraphVkSunPass *pass)
{
    if (pass->pipeline) vkDestroyPipeline(pass->device, pass->pipeline, NULL);
    if (pass->layout) vkDestroyPipelineLayout(pass->device, pass->layout, NULL);
    *pass = (PgraphVkSunPass){0};
}

/* A new/zero pass only. Shader and compatible depth-only render pass are
 * borrowed during creation; no input arrays/pointers are retained. The
 * caller creates/owns map storage and uses its existing retirement path. */
static inline VkResult pgraph_vk_sun_pass_create_variant(
    PgraphVkSunPass *pass, VkDevice device, VkRenderPass render_pass,
    VkShaderModule vertex_shader, VkFormat position_format, uint32_t stride,
    VkPrimitiveTopology topology, const PgraphVkSunVariant *variant,
    VkShaderModule cutout_shader, VkDescriptorSetLayout material_layout)
{
    if (!pass || pass->pipeline || pass->layout || !device || !render_pass ||
        !vertex_shader || (position_format != VK_FORMAT_R32G32B32_SFLOAT &&
                           position_format != VK_FORMAT_R32G32B32A32_SFLOAT) ||
        (topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST &&
        topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP) ||
        !variant || variant->matrices < 1 || variant->matrices > 4 ||
        (variant->matrices == 1 && variant->remainder) ||
        ((variant->matrices > 1 || variant->cutout) && !material_layout) ||
        (variant->cutout && !cutout_shader)) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    uint32_t bytes = position_format == VK_FORMAT_R32G32B32_SFLOAT ? 12 : 16;
    /* Vulkan guarantees at least 2048 bytes of vertex binding stride. */
    if (stride < bytes || stride > 2048 || (stride & 3)) return VK_ERROR_INITIALIZATION_FAILED;
    unsigned weight_bytes = pgraph_vk_sun_weights(variant) * 4;
    if ((weight_bytes && (variant->weight_stride < weight_bytes || variant->weight_stride > 2048 || (variant->weight_stride & 3))) ||
        (variant->cutout && (variant->uv_stride < 8 || variant->uv_stride > 2048 || (variant->uv_stride & 3)))) return VK_ERROR_INITIALIZATION_FAILED;
    PgraphVkSunPass created = {.device = device, .stride = stride, .position_bytes = bytes,
        .variant = *variant, .weight_bytes = weight_bytes};
    /* Both generated stages declare the complete 80-byte block. */
    VkPushConstantRange push = {
        VK_SHADER_STAGE_VERTEX_BIT | (variant->cutout ? VK_SHADER_STAGE_FRAGMENT_BIT : 0), 0, 80
    };
    VkPipelineLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
        .setLayoutCount = (variant->matrices > 1 || variant->cutout) ? 1 : 0,
        .pSetLayouts = &material_layout,
    };
    VkResult result = vkCreatePipelineLayout(device, &layout, NULL, &created.layout);
    if (result != VK_SUCCESS) return result;
    VkPipelineShaderStageCreateInfo stages[2] = {{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex_shader, .pName = "main",
    }, {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = cutout_shader, .pName = "main",
    }};
    VkVertexInputBindingDescription bindings[3] = {{0, stride, VK_VERTEX_INPUT_RATE_VERTEX}};
    VkVertexInputAttributeDescription attributes[3] = {{0, 0, position_format, 0}};
    unsigned count = 1;
    if (weight_bytes) {
        static const VkFormat formats[] = {VK_FORMAT_UNDEFINED, VK_FORMAT_R32_SFLOAT,
            VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
        bindings[count] = (VkVertexInputBindingDescription){1, variant->weight_stride, VK_VERTEX_INPUT_RATE_VERTEX};
        attributes[count++] = (VkVertexInputAttributeDescription){1, 1, formats[weight_bytes / 4], 0};
    }
    if (variant->cutout) {
        bindings[count] = (VkVertexInputBindingDescription){2, variant->uv_stride, VK_VERTEX_INPUT_RATE_VERTEX};
        attributes[count++] = (VkVertexInputAttributeDescription){2, 2, VK_FORMAT_R32G32_SFLOAT, 0};
    }
    VkPipelineVertexInputStateCreateInfo vertex = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = count, .pVertexBindingDescriptions = bindings,
        .vertexAttributeDescriptionCount = count, .pVertexAttributeDescriptions = attributes,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology,
    };
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo raster = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1,
    };
    VkPipelineMultisampleStateCreateInfo samples = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo depth = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    };
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    };
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = states,
    };
    VkGraphicsPipelineCreateInfo pipeline = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = variant->cutout ? 2 : 1, .pStages = stages,
        .pVertexInputState = &vertex, .pInputAssemblyState = &assembly,
        .pViewportState = &viewport, .pRasterizationState = &raster,
        .pMultisampleState = &samples, .pDepthStencilState = &depth,
        .pColorBlendState = &blend, .pDynamicState = &dynamic,
        .layout = created.layout, .renderPass = render_pass,
    };
    result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, NULL, &created.pipeline);
    if (result != VK_SUCCESS) {
        pgraph_vk_sun_pass_destroy(&created);
        return result;
    }
    *pass = created;
    return VK_SUCCESS;
}

static inline VkResult pgraph_vk_sun_pass_create(
    PgraphVkSunPass *pass, VkDevice device, VkRenderPass render_pass,
    VkShaderModule vertex_shader, VkFormat position_format, uint32_t stride,
    VkPrimitiveTopology topology)
{
    const PgraphVkSunVariant rigid = {.matrices = 1};
    return pgraph_vk_sun_pass_create_variant(pass, device, render_pass, vertex_shader,
        position_format, stride, topology, &rigid, VK_NULL_HANDLE, VK_NULL_HANDLE);
}

/* Requires an active command recording outside a render pass. Attachment
 * transitions/dependencies belong to the supplied render pass and owner.
 * Map/view/framebuffer and this pipeline must survive GPU completion. The
 * owner ends the pass and invalidates its normal pipeline/vertex/dynamic
 * state caches before resuming ordinary draws on this command buffer. */
static inline bool pgraph_vk_sun_pass_begin(
    const PgraphVkSunPass *pass, VkCommandBuffer command, VkRenderPass render_pass,
    VkFramebuffer framebuffer, uint32_t width, uint32_t height)
{
    if (!pass || !pass->pipeline || !command || !render_pass || !framebuffer ||
        !width || !height || width > 16384 || height > 16384) return false;
    VkClearValue clear = {.depthStencil = {1, 0}};
    VkRenderPassBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = render_pass, .framebuffer = framebuffer,
        .renderArea = {{0, 0}, {width, height}},
        .clearValueCount = 1, .pClearValues = &clear,
    };
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->pipeline);
    VkViewport viewport = {0, 0, width, height, 0, 1};
    VkRect2D scissor = {{0, 0}, {width, height}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    return true;
}

/* Validation performs no Vulkan calls and can precede lease acquisition. */
static inline bool pgraph_vk_sun_draw_valid(
    const PgraphVkSunPass *pass, const PgraphVkSunDraw *draw)
{
    if (!pass || !pass->pipeline || !pass->layout || !pass->stride ||
        (pass->weight_bytes && !pass->variant.weight_stride) ||
        (pass->variant.cutout && !pass->variant.uv_stride) || !draw || !draw->vertex ||
        !draw->count || (draw->vertex_offset & 3) ||
        draw->vertex_size < pass->position_bytes ||
        draw->vertex_offset > UINT64_MAX - draw->vertex_size) return false;
    for (unsigned i = 0; i < 16; i++) if (!isfinite(draw->world_to_light[i])) return false;
    if ((pass->weight_bytes || pass->variant.cutout) && !draw->material) return false;
    if (pass->weight_bytes && (!draw->weights || (draw->weight_offset & 3) ||
        draw->weight_size < pass->weight_bytes || draw->weight_offset > UINT64_MAX - draw->weight_size)) return false;
    if (pass->variant.cutout && (!draw->texcoords || (draw->uv_offset & 3) ||
        draw->uv_size < 8 || draw->uv_offset > UINT64_MAX - draw->uv_size ||
        draw->alpha_reference > 255 || draw->alpha_compare > 7)) return false;
    if (draw->index) {
        uint32_t bytes = draw->index_type == VK_INDEX_TYPE_UINT16 ? 2 : 4;
        if ((draw->index_type != VK_INDEX_TYPE_UINT16 && draw->index_type != VK_INDEX_TYPE_UINT32) ||
            (draw->index_offset & (bytes - 1)) ||
            draw->index_offset > UINT64_MAX - draw->index_size ||
            ((uint64_t)draw->first + draw->count) * bytes > draw->index_size) return false;
    } else {
        uint64_t last = (uint64_t)draw->first + draw->count - 1;
        if (draw->vertex_base || last > (draw->vertex_size - pass->position_bytes) / pass->stride) return false;
        if (pass->weight_bytes && last > (draw->weight_size - pass->weight_bytes) / pass->variant.weight_stride) return false;
        if (pass->variant.cutout && last > (draw->uv_size - 8) / pass->variant.uv_stride) return false;
    }
    return true;
}

/* Records immediately into the caster pass. Transform/alpha constants enter
 * the command stream; existing GPU streams and descriptors are referenced
 * directly. No packet, allocation or host pointer survives this call. */
static inline bool pgraph_vk_sun_pass_draw(
    const PgraphVkSunPass *pass, VkCommandBuffer command, const PgraphVkSunDraw *draw)
{
    if (!command || !pgraph_vk_sun_draw_valid(pass, draw)) return false;
    float constants[20] = {0};
    memcpy(constants, draw->world_to_light, 64);
    constants[16] = draw->alpha_reference / 255.0f; constants[17] = draw->alpha_compare;
    vkCmdPushConstants(command, pass->layout, VK_SHADER_STAGE_VERTEX_BIT |
        (pass->variant.cutout ? VK_SHADER_STAGE_FRAGMENT_BIT : 0), 0, sizeof(constants), constants);
    vkCmdBindVertexBuffers(command, 0, 1, &draw->vertex, &draw->vertex_offset);
    if (pass->weight_bytes) vkCmdBindVertexBuffers(command, 1, 1, &draw->weights, &draw->weight_offset);
    if (pass->variant.cutout) {
        vkCmdBindVertexBuffers(command, 2, 1, &draw->texcoords, &draw->uv_offset);
    }
    if (pass->weight_bytes || pass->variant.cutout) {
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->layout, 0, 1, &draw->material, 0, NULL);
    }
    if (draw->index) {
        vkCmdBindIndexBuffer(command, draw->index, draw->index_offset, draw->index_type);
        vkCmdDrawIndexed(command, draw->count, 1, draw->first, draw->vertex_base, 0);
    } else {
        vkCmdDraw(command, draw->count, 1, draw->first, 0);
    }
    return true;
}

#endif
