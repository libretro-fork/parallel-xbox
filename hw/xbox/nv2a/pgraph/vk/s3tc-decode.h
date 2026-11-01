/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/nv2a/pgraph/s3tc-decode-glsl.h"

#define S3TC_VK_PREFIX \
    "#version 450\n" \
    "layout(local_size_x=8,local_size_y=8) in;\n" \
    "layout(binding=0,std430) readonly buffer Blocks { uint words[]; };\n" \
    "layout(push_constant) uniform Args { uvec4 shape; uvec4 output_shape; uint src; };\n"
#define S3TC_VK_MAIN \
    S3TC_DECODE_GLSL \
    "void main() {\n" \
    " uvec3 p=gl_GlobalInvocationID;\n" \
    " if(p.x>=output_shape.x || p.y>=output_shape.y || p.z>=shape.z) return;\n" \
    " uvec3 input_pos=p+uvec3(output_shape.zw,0u);\n" \
    " imageStore(output_image,ivec3(p),vec4(decode_s3tc(input_pos,shape,src))/255.0);\n" \
    "}\n"
static const char s3tc_vk_array_glsl[] = S3TC_VK_PREFIX
    "layout(binding=1,rgba8) uniform writeonly image2DArray output_image;\n"
    S3TC_VK_MAIN;
static const char s3tc_vk_volume_glsl[] = S3TC_VK_PREFIX
    "layout(binding=1,rgba8) uniform writeonly image3D output_image;\n"
    S3TC_VK_MAIN;
#undef S3TC_VK_PREFIX
#undef S3TC_VK_MAIN

typedef struct S3TCUploadLevel {
    VkDeviceSize source, size, packed;
    unsigned width, height, depth, out_width, out_height, crop;
    unsigned layer, mip;
} S3TCUploadLevel;

/* Plan first: a rejected upload must leave the command buffer and rings alone. */
static unsigned s3tc_plan_levels(const TextureShape *s,
    const VkImageCreateInfo *ci, VkDeviceSize source, size_t face_stride,
    unsigned skip_layers, unsigned block_size, S3TCUploadLevel levels[96])
{
    if (!s->levels || s->levels > 16 || s->dimensionality < 2 ||
        s->dimensionality > 3 || (s->cubemap && s->dimensionality != 2) ||
        (s->border && !s->cubemap) || ci->extent.width != s->width ||
        ci->extent.height != s->height || ci->extent.depth != s->depth ||
        ci->mipLevels != s->levels || ci->arrayLayers != (s->cubemap ? 6u : 1u)) {
        return 0;
    }
    unsigned count = 0, layers = s->cubemap ? 6 : 1;
    for (unsigned layer = 0; layer < layers; layer++) {
        if (skip_layers & (1u << layer)) continue;
        unsigned w = s->border ? MAX(16, s->width * 2) : s->width;
        unsigned h = s->border ? MAX(16, s->height * 2) : s->height;
        unsigned d = s->dimensionality == 3 ? s->depth : 1;
        VkDeviceSize offset = source + layer * face_stride;
        for (unsigned mip = 0; mip < s->levels; mip++) {
            unsigned ow = MAX(s->width >> mip, 1u), oh = MAX(s->height >> mip, 1u);
            unsigned crop = s->border && mip == 0 ? 4 : 0;
            if (crop + ow > w || crop + oh > h) return 0;
            size_t bytes = (size_t)((w+3)/4) * ((h+3)/4) * d * block_size;
            levels[count++] = (S3TCUploadLevel){ .source = offset, .size = bytes,
                .width = w, .height = h, .depth = d, .out_width = ow,
                .out_height = oh, .crop = crop, .layer = layer, .mip = mip };
            offset += bytes;
            w = MAX(w/2, 1u); h = MAX(h/2, 1u); d = MAX(d/2, 1u);
        }
    }
    return count;
}

static bool s3tc_storage_image(PGRAPHVkState *r, const VkImageCreateInfo *ci)
{
    if (!r->s3tc_decode.enabled || ci->format != VK_FORMAT_R8G8B8A8_UNORM) return false;
    VkImageFormatProperties props;
    return vkGetPhysicalDeviceImageFormatProperties(r->physical_device, ci->format,
        ci->imageType, ci->tiling, ci->usage | VK_IMAGE_USAGE_STORAGE_BIT,
        ci->flags, &props) == VK_SUCCESS && ci->extent.width <= props.maxExtent.width &&
        ci->extent.height <= props.maxExtent.height && ci->extent.depth <= props.maxExtent.depth &&
        ci->mipLevels <= props.maxMipLevels && ci->arrayLayers <= props.maxArrayLayers;
}

static void s3tc_decode_pipeline(PGRAPHVkState *r, unsigned volume)
{
    if (!r->s3tc_decode.set_layout) {
        VkDescriptorSetLayoutBinding bindings[2] = {
            { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
            { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
              .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        };
        VkDescriptorSetLayoutCreateInfo si = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2, .pBindings = bindings };
        VK_CHECK(vkCreateDescriptorSetLayout(r->device, &si, NULL, &r->s3tc_decode.set_layout));
        VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 9 * sizeof(uint32_t) };
        VkPipelineLayoutCreateInfo li = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1, .pSetLayouts = &r->s3tc_decode.set_layout,
            .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
        VK_CHECK(vkCreatePipelineLayout(r->device, &li, NULL, &r->s3tc_decode.layout));
    }
    if (!r->s3tc_decode.pipelines[volume]) {
        VkShaderModule module = pgraph_vk_create_fixed_shader_module_from_glsl(r,
            VK_SHADER_STAGE_COMPUTE_BIT, volume ? s3tc_vk_volume_glsl : s3tc_vk_array_glsl);
        VkComputePipelineCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .layout = r->s3tc_decode.layout, .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main" } };
        VK_CHECK(vkCreateComputePipelines(r->device, r->vk_pipeline_cache, 1, &pi,
                                          NULL, &r->s3tc_decode.pipelines[volume]));
        vkDestroyShaderModule(r->device, module, NULL);
    }
}

/* begin_single_time_commands already retired this slot's previous submission. */
static struct S3TCDecodeSlot *s3tc_decode_slot(PGRAPHVkState *r)
{
    assert(r->in_aux_command_buffer && !r->aux[r->aux_slot].pending);
    struct S3TCDecodeSlot *slot = &r->s3tc_decode.slots[r->aux_slot];
    for (unsigned i = 0; i < slot->used; i++) vkDestroyImageView(r->device, slot->views[i], NULL);
    slot->used = 0;
    if (!slot->pool) {
        VkDescriptorPoolSize sizes[2] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 96 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 96 } };
        VkDescriptorPoolCreateInfo pi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 96, .poolSizeCount = 2, .pPoolSizes = sizes };
        VK_CHECK(vkCreateDescriptorPool(r->device, &pi, NULL, &slot->pool));
        VkDescriptorSetLayout layouts[96];
        for (unsigned i = 0; i < 96; i++) layouts[i] = r->s3tc_decode.set_layout;
        VkDescriptorSetAllocateInfo ai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = slot->pool, .descriptorSetCount = 96, .pSetLayouts = layouts };
        VK_CHECK(vkAllocateDescriptorSets(r->device, &ai, slot->sets));
    }
    return slot;
}

static bool s3tc_decode_levels(PGRAPHState *pg, VkCommandBuffer cmd,
    TextureBinding *binding, S3TCUploadLevel *levels, unsigned count, unsigned format)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r->s3tc_decode.enabled || binding->s2t_borrowed || !count || count > 96 ||
        binding->image_ci.format != VK_FORMAT_R8G8B8A8_UNORM ||
        !(binding->image_ci.usage & VK_IMAGE_USAGE_STORAGE_BIT)) return false;
    VkDeviceSize ram_size = memory_region_size(d->vram), total = 0;
    VkDeviceSize limit = r->device_props.limits.maxStorageBufferRange;
    bool direct = r->vram_buffer && ram_size <= limit;
    for (unsigned i = 0; i < count; i++) {
        S3TCUploadLevel *l = &levels[i];
        if (!l->size || l->source > ram_size || l->size > ram_size-l->source ||
            DIV_ROUND_UP(l->out_width, 8) > r->device_props.limits.maxComputeWorkGroupCount[0] ||
            DIV_ROUND_UP(l->out_height, 8) > r->device_props.limits.maxComputeWorkGroupCount[1] ||
            l->depth > r->device_props.limits.maxComputeWorkGroupCount[2]) return false;
        direct = direct && l->source % 4 == 0;
        l->packed = total;
        total += ROUND_UP(l->size, 16);
    }
    if (!direct && (total > limit || total >= r->storage_buffers[BUFFER_STAGING_SRC].buffer_size)) return false;

    VkDeviceSize source_offset = 0;
    VkBuffer source = r->vram_buffer;
    if (!direct) {
        uint8_t *snapshot;
        source_offset = pgraph_vk_staging_alloc(pg, total,
            MAX(16, r->device_props.limits.minStorageBufferOffsetAlignment), &snapshot);
        for (unsigned i = 0; i < count; i++) {
            memcpy(snapshot + levels[i].packed, (uint8_t *)d->vram_ptr + levels[i].source, levels[i].size);
        }
        pgraph_vk_staging_flush(pg, source_offset, total);
        source = r->storage_buffers[BUFFER_STAGING_SRC].buffer;
    }
    unsigned volume = binding->image_ci.imageType == VK_IMAGE_TYPE_3D;
    s3tc_decode_pipeline(r, volume);
    struct S3TCDecodeSlot *slot = s3tc_decode_slot(r);
    VkDescriptorBufferInfo input = { .buffer = source, .offset = source_offset,
                                     .range = direct ? ram_size : total };
    /* Imported RAM may also contain writes from an earlier GPU operation. */
    VkBufferMemoryBarrier before = { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = source, .offset = source_offset, .size = input.range };
    VkImageMemoryBarrier image = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = binding->current_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
                         VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .oldLayout = binding->current_layout,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = binding->image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, binding->image_ci.mipLevels,
                             0, binding->image_ci.arrayLayers } };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1, &before, 1, &image);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->s3tc_decode.pipelines[volume]);
    VkDescriptorImageInfo outputs[96];
    VkWriteDescriptorSet writes[192];
    for (unsigned i = 0; i < count; i++) {
        S3TCUploadLevel *l = &levels[i];
        VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = binding->image, .viewType = volume ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, l->mip, 1, l->layer, 1 } };
        VK_CHECK(vkCreateImageView(r->device, &vi, NULL, &slot->views[i]));
        slot->used++;
        outputs[i] = (VkDescriptorImageInfo){ .imageView = slot->views[i], .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
        writes[2 * i] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = slot->sets[i],
              .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
              .pBufferInfo = &input };
        writes[2 * i + 1] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = slot->sets[i],
              .dstBinding = 1, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
              .pImageInfo = &outputs[i] };
    }
    /* Every set belongs to this retired slot; publish all bindings together. */
    vkUpdateDescriptorSets(r->device, 2 * count, writes, 0, NULL);
    for (unsigned i = 0; i < count; i++) {
        S3TCUploadLevel *l = &levels[i];
        uint32_t push[9] = { l->width, l->height, l->depth, format,
            l->out_width, l->out_height, l->crop, l->crop,
            (direct ? l->source : l->packed)/4 };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, r->s3tc_decode.layout,
                                0, 1, &slot->sets[i], 0, NULL);
        vkCmdPushConstants(cmd, r->s3tc_decode.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), push);
        vkCmdDispatch(cmd, DIV_ROUND_UP(l->out_width, 8), DIV_ROUND_UP(l->out_height, 8), l->depth);
    }
    /* Skipped cube faces can retain writes from a preceding surface copy. */
    image.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    image.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    image.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    image.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 1, &image);
    binding->current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return true;
}

static bool upload_s3tc_image_gpu(PGRAPHState *pg, VkCommandBuffer cmd,
    TextureBinding *binding, unsigned skip_layers)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    const TextureShape *s = &binding->key.state;
    if (!r->s3tc_decode.enabled || r->bc_native || binding->logo || binding->s2t_borrowed || !pgraph_is_texture_format_compressed(pg, s->color_format) ||
        (size_t)s->width * s->height * s->depth < 65536) return false;
    S3TCUploadLevel levels[96];
    unsigned format = kelvin_format_to_s3tc_format(s->color_format);
    size_t stride = s->cubemap ? binding->key.texture_length / 6 : 0;
    unsigned count = s3tc_plan_levels(s, &binding->image_ci,
        binding->key.texture_vram_offset, stride, skip_layers,
        format == S3TC_DECOMPRESS_FORMAT_DXT1 ? 8 : 16, levels);
    return s3tc_decode_levels(pg, cmd, binding, levels, count, format);
}

static void s3tc_decode_finalize(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r->s3tc_decode.set_layout) {
        memset(&r->s3tc_decode, 0, sizeof(r->s3tc_decode));
        return;
    }
    pgraph_vk_wait_for_aux(pg);
    for (unsigned j = 0; j < PGRAPH_VK_AUX_SLOTS; j++) {
        struct S3TCDecodeSlot *slot = &r->s3tc_decode.slots[j];
        for (unsigned i = 0; i < slot->used; i++) vkDestroyImageView(r->device, slot->views[i], NULL);
        if (slot->pool) vkDestroyDescriptorPool(r->device, slot->pool, NULL);
    }
    for (unsigned i = 0; i < 2; i++) if (r->s3tc_decode.pipelines[i]) vkDestroyPipeline(r->device, r->s3tc_decode.pipelines[i], NULL);
    if (r->s3tc_decode.layout) vkDestroyPipelineLayout(r->device, r->s3tc_decode.layout, NULL);
    if (r->s3tc_decode.set_layout) vkDestroyDescriptorSetLayout(r->device, r->s3tc_decode.set_layout, NULL);
    memset(&r->s3tc_decode, 0, sizeof(r->s3tc_decode));
}
