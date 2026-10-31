/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024 Matt Borgerson
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

#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "renderer.h"
#include "compute-dispatch.h"
#include "hw/xbox/nv2a/pgraph/spv/depth.h"
#include <vulkan/vulkan_core.h>

// TODO: Swizzle/Unswizzle
// TODO: Float depth format (low priority, but would be better for accuracy)

// FIXME: Below pipeline creation assumes identical 3 buffer setup. For
//        swizzle shader we will need more flexibility.

static void create_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorPoolSize pool_sizes[] = {
        {
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 3 * 2,
        },
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = ARRAY_SIZE(pool_sizes),
        .pPoolSizes = pool_sizes,
        .maxSets = 2,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
    };
    VK_CHECK(vkCreateDescriptorPool(r->device, &pool_info, NULL,
                                    &r->compute.descriptor_pool));
}

static void destroy_descriptor_pool(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorPool(r->device, r->compute.descriptor_pool, NULL);
    r->compute.descriptor_pool = VK_NULL_HANDLE;
}

static void create_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    const int num_buffers = 3;

    VkDescriptorSetLayoutBinding bindings[num_buffers];
    for (int i = 0; i < num_buffers; i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }
    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ARRAY_SIZE(bindings),
        .pBindings = bindings,
    };
    VK_CHECK(vkCreateDescriptorSetLayout(r->device, &layout_info, NULL,
                                         &r->compute.descriptor_set_layout));
}

static void destroy_descriptor_set_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyDescriptorSetLayout(r->device, r->compute.descriptor_set_layout,
                                 NULL);
    r->compute.descriptor_set_layout = VK_NULL_HANDLE;
}

/* The pack and the unpack always use the same two buffers, whole: the
 * pack reads the depth and stencil planes from BUFFER_COMPUTE_DST and
 * writes the packed surface into BUFFER_COMPUTE_SRC, the unpack the other
 * way. So one descriptor set each, written at init and again when the two
 * buffers are recreated (pgraph_vk_compute_buffers_changed, with nothing
 * in flight); the stencil plane's place is a push constant. No set is
 * used up per dispatch, so none ever runs out and nothing has to wait for
 * the GPU to free them. */
static void write_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkBuffer planes_pack = r->storage_buffers[BUFFER_COMPUTE_DST].buffer;
    VkBuffer packed_pack = r->storage_buffers[BUFFER_COMPUTE_SRC].buffer;
    VkDescriptorBufferInfo pack[3] = {
        { .buffer = planes_pack, .range = VK_WHOLE_SIZE },
        { .buffer = planes_pack, .range = VK_WHOLE_SIZE },
        { .buffer = packed_pack, .range = VK_WHOLE_SIZE },
    };
    VkDescriptorBufferInfo unpack[3] = {
        { .buffer = packed_pack, .range = VK_WHOLE_SIZE },
        { .buffer = packed_pack, .range = VK_WHOLE_SIZE },
        { .buffer = planes_pack, .range = VK_WHOLE_SIZE },
    };
    VkWriteDescriptorSet writes[6];
    for (int i = 0; i < 6; i++) {
        writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = i < 3 ? r->compute.set_pack : r->compute.set_unpack,
            .dstBinding = i % 3,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo = i < 3 ? &pack[i] : &unpack[i - 3],
        };
    }
    vkUpdateDescriptorSets(r->device, 6, writes, 0, NULL);
}

void pgraph_vk_compute_buffers_changed(PGRAPHState *pg)
{
    if (pg->vk_renderer_state->compute.set_pack != VK_NULL_HANDLE) {
        write_descriptor_sets(pg);
    }
}

static void create_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkDescriptorSetLayout layouts[2] = {
        r->compute.descriptor_set_layout,
        r->compute.descriptor_set_layout,
    };
    VkDescriptorSet sets[2];
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = r->compute.descriptor_pool,
        .descriptorSetCount = 2,
        .pSetLayouts = layouts,
    };
    VK_CHECK(vkAllocateDescriptorSets(r->device, &alloc_info, sets));
    r->compute.set_pack = sets[0];
    r->compute.set_unpack = sets[1];
    write_descriptor_sets(pg);
}

static void destroy_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkDescriptorSet sets[2] = { r->compute.set_pack, r->compute.set_unpack };

    vkFreeDescriptorSets(r->device, r->compute.descriptor_pool, 2, sets);
    r->compute.set_pack = VK_NULL_HANDLE;
    r->compute.set_unpack = VK_NULL_HANDLE;
}

static void create_compute_pipeline_layout(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VkPushConstantRange push_constant_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = 4 * sizeof(uint32_t),
    };
    VkPipelineLayoutCreateInfo pipeline_layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &r->compute.descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constant_range,
    };
    VK_CHECK(vkCreatePipelineLayout(r->device, &pipeline_layout_info, NULL,
                                    &r->compute.pipeline_layout));
}

static void destroy_compute_pipeline_layout(PGRAPHVkState *r)
{
    vkDestroyPipelineLayout(r->device, r->compute.pipeline_layout, NULL);
    r->compute.pipeline_layout = VK_NULL_HANDLE;
}

/* Four format/direction pipelines cover every surface size. Dimensions are
 * push constants; bounds-checked workgroups and a balanced 2D grid avoid
 * per-size compilation, cache lookups and the X dispatch limit. */
static VkPipeline create_compute_pipeline(PGRAPHVkState *r, bool float_depth, bool pack)
{
    SpvStaticModule words = pgraph_spv_depth_compute(float_depth, pack, r->compute.workgroup_size);
    assert(words.words);
    VkShaderModule module = pgraph_vk_create_shader_module_from_words(r, words.words, words.num_words);
    VkComputePipelineCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = r->compute.pipeline_layout,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                   .stage = VK_SHADER_STAGE_COMPUTE_BIT, .pName = "main", .module = module },
    };
    VkPipeline pipeline;
    VK_CHECK(vkCreateComputePipelines(r->device, r->vk_pipeline_cache, 1, &info, NULL, &pipeline));
    vkDestroyShaderModule(r->device, module, NULL);
    return pipeline;
}

static VkPipeline get_compute_pipeline(PGRAPHVkState *r, VkFormat format, bool pack)
{
    assert(format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT);
    return r->compute.pipelines[format == VK_FORMAT_D32_SFLOAT_S8_UINT][pack];
}

static ComputeGrid get_dispatch(PGRAPHVkState *r, size_t pixels)
{
    assert(pixels <= UINT32_MAX);
    ComputeGrid grid;
    bool valid = pgraph_compute_grid(pixels, r->compute.workgroup_size,
        r->device_props.limits.maxComputeWorkGroupCount[0],
        r->device_props.limits.maxComputeWorkGroupCount[1], &grid);
    assert(valid);
    return grid;
}

//
// Pack depth+stencil into NV097_SET_SURFACE_FORMAT_ZETA_Z24S8
// formatted buffer with depth in bits 31-8 and stencil in bits 7-0.
//
void pgraph_vk_pack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                  VkCommandBuffer cmd, VkBuffer src,
                                  VkBuffer dst, bool downscale)
{
    unsigned int input_width = surface->width, input_height = surface->height;
    pgraph_apply_scaling_factor(pg, &input_width, &input_height);

    unsigned int output_width = surface->width, output_height = surface->height;
    if (!downscale) {
        pgraph_apply_scaling_factor(pg, &output_width, &output_height);
    }
    pgraph_vk_pack_depth_stencil_dims(pg, surface, cmd, src, dst, input_width,
                                      input_height, output_width,
                                      output_height);
}

/* Explicit-dimension variant: the input buffer holds in_w x in_h texels
 * (already native when the caller downsampled on the GPU). */
void pgraph_vk_pack_depth_stencil_dims(PGRAPHState *pg,
                                       SurfaceBinding *surface,
                                       VkCommandBuffer cmd, VkBuffer src,
                                       VkBuffer dst, unsigned int in_w,
                                       unsigned int in_h, unsigned int out_w,
                                       unsigned int out_h)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    unsigned int input_width = in_w, input_height = in_h;
    unsigned int output_width = out_w, output_height = out_h;

    size_t depth_bytes_per_pixel = 4;
    size_t depth_size = input_width * input_height * depth_bytes_per_pixel;

    size_t stencil_bytes_per_pixel = 1;
    size_t stencil_size = input_width * input_height * stencil_bytes_per_pixel;

    size_t output_bytes_per_pixel = 4;
    size_t output_size = output_width * output_height * output_bytes_per_pixel;

    /* the fixed set's buffers (write_descriptor_sets) */
    assert(src == r->storage_buffers[BUFFER_COMPUTE_DST].buffer &&
           dst == r->storage_buffers[BUFFER_COMPUTE_SRC].buffer);
    assert(ROUND_UP(depth_size,
                    r->device_props.limits.minStorageBufferOffsetAlignment) +
               stencil_size <=
           r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size);
    assert(output_size <= r->storage_buffers[BUFFER_COMPUTE_SRC].buffer_size);
    /* the stencil plane follows the depth one, aligned as the copy put it */
    uint32_t stencil_base =
        ROUND_UP(depth_size,
                 r->device_props.limits.minStorageBufferOffsetAlignment) / 4;

    size_t output_size_in_units = output_width * output_height;
    VkPipeline pipeline = get_compute_pipeline(r, surface->host_fmt.vk_format, true);

    ComputeGrid grid = get_dispatch(r, output_size_in_units);

    pgraph_vk_begin_debug_marker(r, cmd, RGBA_PINK, __func__);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->compute.pipeline_layout, 0, 1,
        &r->compute.set_pack, 0, NULL);

    uint32_t push_constants[4] = { input_width, output_width, stencil_base, output_size_in_units };
    vkCmdPushConstants(cmd, r->compute.pipeline_layout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);

    vkCmdDispatch(cmd, grid.x, grid.y, 1);
    pgraph_vk_end_debug_marker(r, cmd);
}

void pgraph_vk_unpack_depth_stencil(PGRAPHState *pg, SurfaceBinding *surface,
                                    VkCommandBuffer cmd, VkBuffer src,
                                    VkBuffer dst)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    unsigned int input_width = surface->width, input_height = surface->height;

    unsigned int output_width = surface->width, output_height = surface->height;
    pgraph_apply_scaling_factor(pg, &output_width, &output_height);

    size_t depth_bytes_per_pixel = 4;
    size_t depth_size = output_width * output_height * depth_bytes_per_pixel;

    size_t stencil_bytes_per_pixel = 1;
    size_t stencil_size = output_width * output_height * stencil_bytes_per_pixel;

    size_t input_bytes_per_pixel = 4;
    size_t input_size = input_width * input_height * input_bytes_per_pixel;

    /* the fixed set's buffers (write_descriptor_sets) */
    assert(src == r->storage_buffers[BUFFER_COMPUTE_DST].buffer &&
           dst == r->storage_buffers[BUFFER_COMPUTE_SRC].buffer);
    assert(ROUND_UP(depth_size,
                    r->device_props.limits.minStorageBufferOffsetAlignment) +
               stencil_size <=
           r->storage_buffers[BUFFER_COMPUTE_SRC].buffer_size);
    assert(input_size <= r->storage_buffers[BUFFER_COMPUTE_DST].buffer_size);
    /* the stencil plane follows the depth one, aligned as the copy reads it */
    uint32_t stencil_base =
        ROUND_UP(depth_size,
                 r->device_props.limits.minStorageBufferOffsetAlignment) / 4;

    size_t output_size_in_units = output_width * output_height;
    VkPipeline pipeline = get_compute_pipeline(r, surface->host_fmt.vk_format, false);

    ComputeGrid grid = get_dispatch(r, output_size_in_units);

    pgraph_vk_begin_debug_marker(r, cmd, RGBA_PINK, __func__);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->compute.pipeline_layout, 0, 1,
        &r->compute.set_unpack, 0, NULL);

    assert(output_width >= input_width);
    uint32_t push_constants[4] = { input_width, output_width, stencil_base, output_size_in_units };
    vkCmdPushConstants(cmd, r->compute.pipeline_layout,
                       VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push_constants),
                       push_constants);
    vkCmdDispatch(cmd, grid.x, grid.y, 1);
    pgraph_vk_end_debug_marker(r, cmd);
}

static void create_compute_pipelines(PGRAPHVkState *r)
{
    uint32_t limit = MIN(r->device_props.limits.maxComputeWorkGroupSize[0],
                         r->device_props.limits.maxComputeWorkGroupInvocations);
    assert(limit >= 128); /* Vulkan's minimum workgroup capability. */
    r->compute.workgroup_size = limit >= 256 ? 256 : 128;
    for (unsigned depth = 0; depth < 2; depth++) {
        for (unsigned pack = 0; pack < 2; pack++) {
            r->compute.pipelines[depth][pack] = create_compute_pipeline(r, depth, pack);
        }
    }
}

static void destroy_compute_pipelines(PGRAPHVkState *r)
{
    for (unsigned depth = 0; depth < 2; depth++) {
        for (unsigned pack = 0; pack < 2; pack++) {
            vkDestroyPipeline(r->device, r->compute.pipelines[depth][pack], NULL);
            r->compute.pipelines[depth][pack] = VK_NULL_HANDLE;
        }
    }
}

void pgraph_vk_init_compute(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    create_descriptor_pool(pg);
    create_descriptor_set_layout(pg);
    create_descriptor_sets(pg);
    create_compute_pipeline_layout(pg);
    create_compute_pipelines(r);
}

void pgraph_vk_finalize_compute(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(!r->in_command_buffer);

    destroy_compute_pipelines(r);
    destroy_compute_pipeline_layout(r);
    destroy_descriptor_sets(pg);
    destroy_descriptor_set_layout(pg);
    destroy_descriptor_pool(pg);
}
