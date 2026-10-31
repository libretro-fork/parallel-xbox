/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "renderer.h"

/* An odd final pixel still needs Cr from the complete four-byte YUY2 pair. */
static void pvideo_pack_rows(uint8_t *dst, const uint8_t *src,
                             unsigned width, unsigned height, unsigned pitch)
{
    size_t row_bytes = ((size_t)width + 1) / 2 * 4;
    if (row_bytes == pitch) {
        memcpy(dst, src, row_bytes * height);
    } else {
        for (unsigned y = 0; y < height; y++) {
            memcpy(dst + y * row_bytes, src + (size_t)y * pitch, row_bytes);
        }
    }
}

/* One invocation per logical pixel, preserving the old integer conversion.
 * The packed snapshot belongs to this auxiliary slot until its fence retires. */
static const char *pvideo_convert_glsl =
    "#version 450\n"
    "layout(local_size_x=64) in;\n"
    "layout(binding=0, std430) readonly buffer Input { uint yuy2[]; };\n"
    "layout(binding=1, rgba8) uniform writeonly image2D rgba;\n"
    "layout(push_constant, std430) uniform Args { uint src; uint dst; uint width; uint height; };\n"
    "void main() {\n"
    "    uint i = gl_GlobalInvocationID.x;\n"
    "    if (i >= width * height) return;\n"
    "    uint x = i % width;\n"
    "    uint pair = yuy2[src + (i / width) * ((width + 1u) / 2u) + x / 2u];\n"
    "    int c = int((pair >> ((x & 1u) * 16u)) & 255u) - 16;\n"
    "    int d = int((pair >> 8u) & 255u) - 128;\n"
    "    int e = int(pair >> 24u) - 128;\n"
    "    int r = clamp((298 * c + 409 * e + 128) >> 8, 0, 255);\n"
    "    int g = clamp((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255);\n"
    "    int b = clamp((298 * c + 516 * d + 128) >> 8, 0, 255);\n"
    "    imageStore(rgba, ivec2(x, i / width), vec4(r, g, b, 255) / 255.0);\n"
    "}\n";

static void pvideo_convert_bind_source(PGRAPHVkState *r)
{
    typeof(r->display.pvideo.convert) *c = &r->display.pvideo.convert;
    VkBuffer source = r->storage_buffers[BUFFER_STAGING_SRC].buffer;
    VkImageView output = r->display.pvideo.image_view;
    unsigned slot = r->aux_slot;
    assert(r->in_aux_command_buffer && !r->aux[slot].pending);
    if (c->source[slot] != source || c->output[slot] != output) {
        /* begin_single_time_commands retired this slot before reuse. Other
         * slots keep their old descriptors while a resize is in flight. */
        VkDescriptorBufferInfo buffer = { .buffer = source, .range = VK_WHOLE_SIZE };
        VkDescriptorImageInfo image = { .imageView = output,
                                       .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSet writes[2] = {
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = c->sets[slot], .dstBinding = 0, .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &buffer },
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = c->sets[slot], .dstBinding = 1, .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &image },
        };
        vkUpdateDescriptorSets(r->device, 2, writes, 0, NULL);
        c->source[slot] = source; c->output[slot] = output;
    }
}

void pgraph_vk_pvideo_init(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(r->physical_device,
        VK_FORMAT_R8G8B8A8_UNORM, &props);
    r->display.pvideo.storage =
        (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
    if (!r->display.pvideo.storage) {
        return;
    }
    typeof(r->display.pvideo.convert) *c = &r->display.pvideo.convert;
    if (!c->pipeline) {
        VkDescriptorSetLayoutBinding bindings[2];
        for (unsigned i = 0; i < 2; i++) {
            bindings[i] = (VkDescriptorSetLayoutBinding){
                .binding = i, .descriptorCount = 1,
                .descriptorType = i ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            };
        }
        VkDescriptorSetLayoutCreateInfo sl = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2, .pBindings = bindings,
        };
        VK_CHECK(vkCreateDescriptorSetLayout(r->device, &sl, NULL, &c->set_layout));
        VkDescriptorPoolSize sizes[2] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PGRAPH_VK_AUX_SLOTS},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, PGRAPH_VK_AUX_SLOTS},
        };
        VkDescriptorPoolCreateInfo pool = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = PGRAPH_VK_AUX_SLOTS, .poolSizeCount = 2, .pPoolSizes = sizes,
        };
        VK_CHECK(vkCreateDescriptorPool(r->device, &pool, NULL, &c->pool));
        VkDescriptorSetLayout layouts[PGRAPH_VK_AUX_SLOTS];
        for (unsigned i = 0; i < PGRAPH_VK_AUX_SLOTS; i++) layouts[i] = c->set_layout;
        VkDescriptorSetAllocateInfo allocate = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = c->pool, .descriptorSetCount = PGRAPH_VK_AUX_SLOTS,
            .pSetLayouts = layouts,
        };
        VK_CHECK(vkAllocateDescriptorSets(r->device, &allocate, c->sets));
        VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 16};
        VkPipelineLayoutCreateInfo pl = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &c->set_layout,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &push,
        };
        VK_CHECK(vkCreatePipelineLayout(r->device, &pl, NULL, &c->layout));
        VkShaderModule module = pgraph_vk_create_fixed_shader_module_from_glsl(
            r, VK_SHADER_STAGE_COMPUTE_BIT, pvideo_convert_glsl);
        VkComputePipelineCreateInfo pipeline = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .layout = c->layout,
            .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main" },
        };
        VK_CHECK(vkCreateComputePipelines(r->device, r->vk_pipeline_cache, 1,
                                           &pipeline, NULL, &c->pipeline));
        vkDestroyShaderModule(r->device, module, NULL);
    }
}

bool pgraph_vk_pvideo_convert(PGRAPHState *pg, VkCommandBuffer cmd,
    const uint8_t *source, unsigned width, unsigned height, unsigned pitch,
    bool created)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    size_t pixels = (size_t)width * height;
    size_t packed_size = ((size_t)width + 1) / 2 * height * 4;
    VkDeviceSize limit = r->device_props.limits.maxStorageBufferRange;
    if (!r->display.pvideo.storage || !pixels || pixels > UINT32_MAX / 4 ||
        DIV_ROUND_UP(pixels, 64) > r->device_props.limits.maxComputeWorkGroupCount[0] ||
        r->storage_buffers[BUFFER_STAGING_SRC].buffer_size > limit ||
        packed_size >= r->storage_buffers[BUFFER_STAGING_SRC].buffer_size) {
        return false;
    }
    uint8_t *staged;
    VkDeviceSize src = pgraph_vk_staging_alloc(pg, packed_size, 16, &staged);
    pvideo_pack_rows(staged, source, width, height, pitch);
    pgraph_vk_staging_flush(pg, src, packed_size);
    pvideo_convert_bind_source(r);

    VkBufferMemoryBarrier before = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = r->storage_buffers[BUFFER_STAGING_SRC].buffer,
        .offset = src, .size = packed_size,
    };
    VkImageMemoryBarrier image = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = created ? 0 : VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = created ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = r->display.pvideo.image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1, &before, 1, &image);
    typeof(r->display.pvideo.convert) *c = &r->display.pvideo.convert;
    uint32_t push[4] = {src / 4, 0, width, height};
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, c->layout,
                            0, 1, &c->sets[r->aux_slot], 0, NULL);
    vkCmdPushConstants(cmd, c->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), push);
    vkCmdDispatch(cmd, DIV_ROUND_UP(pixels, 64), 1, 1);
    image.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    image.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    image.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &image);
    return true;
}

void pgraph_vk_pvideo_finalize(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    typeof(r->display.pvideo.convert) *c = &r->display.pvideo.convert;
    /* Also covers the most recent upload when no display consumed it yet. */
    pgraph_vk_wait_for_aux(pg);
    if (c->pipeline) vkDestroyPipeline(r->device, c->pipeline, NULL);
    if (c->layout) vkDestroyPipelineLayout(r->device, c->layout, NULL);
    if (c->pool) vkDestroyDescriptorPool(r->device, c->pool, NULL);
    if (c->set_layout) vkDestroyDescriptorSetLayout(r->device, c->set_layout, NULL);
    memset(c, 0, sizeof(*c));
}
