/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/nv2a/pgraph/vk/sun-shadow-owner.h"

/* Test-only resource ownership. Every rewritten GPU input follows completion
 * of its previous consumer; none of these buffers belongs to the caster. */
static bool reject_material_commands;
static PFN_vkCmdBindVertexBuffers material_bind;
static PFN_vkCmdPushConstants material_push;
static PFN_vkCmdBindDescriptorSets material_descriptors;
static PFN_vkCmdDraw material_draw;
static VKAPI_ATTR void VKAPI_CALL guard_bind(VkCommandBuffer c,uint32_t first,uint32_t n,const VkBuffer *b,const VkDeviceSize *o)
{ assert(!reject_material_commands);material_bind(c,first,n,b,o); }
static VKAPI_ATTR void VKAPI_CALL guard_push(VkCommandBuffer c,VkPipelineLayout l,VkShaderStageFlags s,uint32_t o,uint32_t n,const void *p)
{ assert(!reject_material_commands);material_push(c,l,s,o,n,p); }
static VKAPI_ATTR void VKAPI_CALL guard_descriptors(VkCommandBuffer c,VkPipelineBindPoint b,VkPipelineLayout l,uint32_t first,uint32_t n,const VkDescriptorSet *s,uint32_t nd,const uint32_t *d)
{ assert(!reject_material_commands);material_descriptors(c,b,l,first,n,s,nd,d); }
static VKAPI_ATTR void VKAPI_CALL guard_draw(VkCommandBuffer c,uint32_t n,uint32_t instances,uint32_t first,uint32_t base)
{ assert(!reject_material_commands);material_draw(c,n,instances,first,base); }

static bool alpha_pass(unsigned value, unsigned reference, unsigned compare)
{
    switch (compare) {
    case 0: return false;
    case 1: return value < reference;
    case 2: return value == reference;
    case 3: return value <= reference;
    case 4: return value > reference;
    case 5: return value != reference;
    case 6: return value >= reference;
    default: return true;
    }
}

static VkShaderModule compile_sun(GlslcStage stage, const char *source)
{
    GlslcResult code = {0}; GlslcOptions options = {0};
    if (!glslc_compile(stage, source, &options, &code)) {
        fprintf(stderr, "%s\n%s\n", code.error, source); abort();
    }
    VkShaderModule module = gpu_module(code.words, code.num_words);
    glslc_result_free_words(&code);
    return module;
}

static void material_tests(VkRenderPass render, VkFramebuffer framebuffer,
                            VkImage depth_image, Buffer output)
{
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
    };
    VkDescriptorSetLayoutCreateInfo li = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = bindings,
    };
    VkDescriptorSetLayout layout; CHECK(vkCreateDescriptorSetLayout(device, &li, NULL, &layout));
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1}};
    VkDescriptorPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = sizes};
    VkDescriptorPool descriptors; CHECK(vkCreateDescriptorPool(device, &pi, NULL, &descriptors));
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptors, .descriptorSetCount = 1, .pSetLayouts = &layout};
    VkDescriptorSet material; CHECK(vkAllocateDescriptorSets(device, &ai, &material));
    Buffer vertices = make_buffer(64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Buffer weights = make_buffer(64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Buffer coords = make_buffer(48, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    Buffer palette = make_buffer(256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    Buffer upload = make_buffer(4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    const float triangle[3][4] = {{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1}};
    memcpy((uint8_t *)vertices.mapped + 16, triangle, sizeof(triangle));
    const float weight_values[4] = {.2f,.3f,.1f,.4f};
    for (unsigned i = 0; i < 3; i++) {
        memcpy((uint8_t *)weights.mapped + 16 + i * 16, weight_values, 16);
        float uv[2] = {.5f,.5f}; memcpy((uint8_t *)coords.mapped + 16 + i * 8, uv, 8);
    }
    for (unsigned bone = 0; bone < 4; bone++) identity((float *)palette.mapped + bone * 16, (bone + 1) * .125f);
    VkImageCreateInfo image_info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {1,1,1},
        .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    VkImage alpha_image; CHECK(vkCreateImage(device, &image_info, NULL, &alpha_image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device, alpha_image, &req);
    VkMemoryAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VkDeviceMemory memory; CHECK(vkAllocateMemory(device, &allocate, NULL, &memory));
    CHECK(vkBindImageMemory(device, alpha_image, memory, 0));
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = alpha_image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    VkImageView view; CHECK(vkCreateImageView(device, &vi, NULL, &view));
    VkSamplerCreateInfo sampler_info = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0};
    VkSampler sampler; CHECK(vkCreateSampler(device, &sampler_info, NULL, &sampler));
    VkDescriptorBufferInfo uniform = {palette.buffer,0,256};
    VkDescriptorImageInfo texture = {sampler,view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet writes[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = material, .dstBinding = 0,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .pBufferInfo = &uniform},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = material, .dstBinding = 1,
         .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &texture},
    };
    vkUpdateDescriptorSets(device, 2, writes, 0, NULL);
    VkShaderModule fragment = compile_sun(GLSLC_STAGE_FRAGMENT, pgraph_vk_sun_cutout_glsl);
    bool texture_ready = false;
    unsigned cases = 0;
    size_t pixels = 0;
    material_bind=vkCmdBindVertexBuffers;material_push=vkCmdPushConstants;
    material_descriptors=vkCmdBindDescriptorSets;material_draw=vkCmdDraw;
    vkCmdBindVertexBuffers=guard_bind;vkCmdPushConstants=guard_push;
    vkCmdBindDescriptorSets=guard_descriptors;vkCmdDraw=guard_draw;
    for (unsigned matrices = 1; matrices <= 4; matrices++)
    for (unsigned remainder = 0; remainder < (matrices == 1 ? 1u : 2u); remainder++)
    for (unsigned cutout = 0; cutout < 2; cutout++) {
        PgraphVkSunVariant variant = {.matrices = matrices, .remainder = remainder,
            .cutout = cutout, .weight_stride = 16, .uv_stride = 8};
        char vertex_source[4096];
        char too_small[1];
        assert(!pgraph_vk_sun_vertex_source(&variant,too_small,sizeof(too_small)) && !too_small[0]);
        assert(pgraph_vk_sun_vertex_source(&variant, vertex_source, sizeof(vertex_source)));
        VkShaderModule vertex = compile_sun(GLSLC_STAGE_VERTEX, vertex_source);
        PgraphVkSunPass pass = {0};
        CHECK(pgraph_vk_sun_pass_create_variant(&pass, device, render, vertex,
            VK_FORMAT_R32G32B32A32_SFLOAT, 16, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            &variant, fragment, layout));
        /* Shader modules are borrowed only during pipeline construction. */
        vkDestroyShaderModule(device, vertex, NULL);
        float expected = .375f;
        if (matrices > 1) {
            float sum = 0, weight_sum = 0, rest = 1;
            for (unsigned bone = 0; bone < matrices; bone++) {
                float w = remainder && bone == matrices - 1 ? rest : weight_values[bone];
                sum += w * (bone + 1) * .125f; weight_sum += w;
                rest -= w;
            }
            expected = sum / weight_sum;
        }
        unsigned attempts = cutout ? 72 : 1;
        for (unsigned test = 0; test < attempts; test++) {
            const unsigned alpha_values[3] = {0,128,255};
            unsigned value = alpha_values[(test / 24) % 3];
            unsigned reference = alpha_values[(test / 8) % 3], compare = test % 8;
            memset(upload.mapped, 255, 4); ((uint8_t *)upload.mapped)[3] = value;
            begin();
            VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = texture_ready ? VK_ACCESS_SHADER_READ_BIT : 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = texture_ready ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = alpha_image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
            vkCmdPipelineBarrier(command, texture_ready ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
            VkBufferImageCopy upload_copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}, .imageExtent = {1,1,1}};
            vkCmdCopyBufferToImage(command, upload.buffer, alpha_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload_copy);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, NULL, 0, NULL, 1, &barrier);
            texture_ready = true;
            assert(pgraph_vk_sun_pass_begin(&pass, command, render, framebuffer, WIDTH, HEIGHT));
            PgraphVkSunDraw draw = {.vertex = vertices.buffer, .vertex_offset = 16, .vertex_size = 48,
                .weights = weights.buffer, .weight_offset = 16, .weight_size = 48,
                .texcoords = coords.buffer, .uv_offset = 16, .uv_size = 24,
                .material = material, .count = 3, .alpha_reference = reference, .alpha_compare = compare};
            identity(draw.world_to_light, .375f);
            reject_material_commands=true;
            if(matrices>1 || cutout) {
                PgraphVkSunDraw invalid=draw;invalid.material=VK_NULL_HANDLE;
                assert(!pgraph_vk_sun_pass_draw(&pass,command,&invalid));
            }
            if(matrices>1) {
                PgraphVkSunDraw invalid=draw;invalid.weights=VK_NULL_HANDLE;
                assert(!pgraph_vk_sun_pass_draw(&pass,command,&invalid));
                invalid=draw;invalid.weight_size=32;
                assert(!pgraph_vk_sun_pass_draw(&pass,command,&invalid));
            }
            if(cutout) {
                PgraphVkSunDraw invalid=draw;invalid.texcoords=VK_NULL_HANDLE;
                assert(!pgraph_vk_sun_pass_draw(&pass,command,&invalid));
                invalid=draw;invalid.uv_size=16;
                assert(!pgraph_vk_sun_pass_draw(&pass,command,&invalid));
                invalid=draw;invalid.alpha_reference=256;
                assert(!pgraph_vk_sun_pass_draw(&pass,command,&invalid));
                invalid=draw;invalid.alpha_compare=8;
                assert(!pgraph_vk_sun_pass_draw(&pass,command,&invalid));
            }
            reject_material_commands=false;
            PgraphSunCapture capture={0};PgraphSunCapturePacket packet;
            PgraphSunVersion versions[10]={0};PgraphSunTicket tickets[8]={0};PgraphSunOwner leases={0};
            assert(pgraph_sun_owner_start(&leases,versions,10,tickets,8,3,7));
            PgraphSunDrawResources refs={0};
            refs.vertex=pgraph_sun_owner_publish(&leases,0,(PgraphSunVersion){
                .type=PGRAPH_SUN_BUFFER_VERSION,.handle=(uint64_t)(uintptr_t)vertices.buffer,
                .offset=16,.size=48,.ready=true},false);
            refs.weights=pgraph_sun_owner_publish(&leases,2,(PgraphSunVersion){
                .type=PGRAPH_SUN_BUFFER_VERSION,.handle=(uint64_t)(uintptr_t)weights.buffer,
                .offset=16,.size=48,.ready=true},false);
            refs.uv=pgraph_sun_owner_publish(&leases,3,(PgraphSunVersion){
                .type=PGRAPH_SUN_BUFFER_VERSION,.handle=(uint64_t)(uintptr_t)coords.buffer,
                .offset=16,.size=24,.ready=true},false);
            PgraphSunVersion compound={.type=PGRAPH_SUN_MATERIAL_VERSION,
                .handle=(uint64_t)(uintptr_t)material,.ready=true,.dependency_count=4};
            compound.dependencies[0]=pgraph_sun_owner_publish(&leases,6,(PgraphSunVersion){
                .type=PGRAPH_SUN_BUFFER_VERSION,.handle=(uint64_t)(uintptr_t)palette.buffer,
                .size=256,.ready=true},false);
            compound.dependencies[1]=pgraph_sun_owner_publish(&leases,7,(PgraphSunVersion){
                .type=PGRAPH_SUN_IMAGE_VERSION,.handle=(uint64_t)(uintptr_t)alpha_image,
                .companion=(uint64_t)(uintptr_t)view,.ready=true},false);
            compound.dependencies[2]=pgraph_sun_owner_publish(&leases,8,(PgraphSunVersion){
                .type=PGRAPH_SUN_SAMPLER_VERSION,.handle=(uint64_t)(uintptr_t)sampler,.ready=true},false);
            compound.dependencies[3]=pgraph_sun_owner_publish(&leases,9,(PgraphSunVersion){
                .type=PGRAPH_SUN_DESCRIPTOR_POOL_VERSION,.handle=(uint64_t)(uintptr_t)descriptors,
                .companion=(uint64_t)(uintptr_t)layout,.ready=true},false);
            refs.material=pgraph_sun_owner_publish(&leases,4,compound,false);
            refs.pipeline=pgraph_sun_owner_publish(&leases,5,(PgraphSunVersion){
                .type=PGRAPH_SUN_PIPELINE_VERSION,.handle=(uint64_t)(uintptr_t)pass.pipeline,
                .companion=(uint64_t)(uintptr_t)pass.layout,.ready=true},false);
            PgraphSunScene captured_scene=scene();
            assert(pgraph_sun_capture_begin(true,&capture,&packet,1,pgraph_sun_owner_callbacks(&leases),
                PGRAPH_SUN_DOAXVB_US_11106,3,11,7,&captured_scene));
            assert(pgraph_sun_capture_append(&capture,10,&pass,&draw,&refs));
            memset(&draw, 0, sizeof(draw));
            PgraphSunUse use={.lifetime=3,.scene_epoch=11,.recording=7,.main_submit=1};
            assert(pgraph_sun_capture_submit(&capture,use,emit_capture,&command));
            assert(!pgraph_sun_owner_writable(&leases,refs.vertex) && !pgraph_sun_owner_writable(&leases,refs.pipeline));
            if(matrices>1 || cutout) {
                assert(!pgraph_sun_owner_writable(&leases,refs.material));
                for(unsigned dep=0;dep<4;dep++) {
                    assert(!pgraph_sun_owner_writable(&leases,compound.dependencies[dep]));
                    PgraphSunVersion replacement=versions[6+dep];replacement.cpu_refs=replacement.gpu_refs=0;
                    assert(!pgraph_sun_owner_publish(&leases,6+dep,replacement,false).id);
                }
            }
            memset(&packet,0x99,sizeof(packet));
            vkCmdEndRenderPass(command);
            VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1}, .imageExtent = {WIDTH,HEIGHT,1}};
            vkCmdCopyImageToBuffer(command, depth_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output.buffer, 1, &copy);
            host_barrier(output, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            submit();
            pgraph_sun_owner_retire(&leases,1,0,0);
            assert(pgraph_sun_owner_writable(&leases,refs.vertex));
            assert(pgraph_sun_owner_stop(&leases));
            float expected_depth = !cutout || alpha_pass(value, reference, compare) ? expected : 1;
            for (unsigned i = 0; i < WIDTH * HEIGHT; i++) {
                float actual = ((float *)output.mapped)[i];
                if (fabsf(actual - expected_depth) >= 1e-6f) {
                    fprintf(stderr, "material mismatch matrices=%u remainder=%u cutout=%u alpha=%u ref=%u compare=%u depth=%g expected=%g\n",
                        matrices,remainder,cutout,value,reference,compare,actual,expected_depth); abort();
                }
                pixels++;
            }
            cases++;
        }
        pgraph_vk_sun_pass_destroy(&pass);
    }
    vkCmdBindVertexBuffers=material_bind;vkCmdPushConstants=material_push;
    vkCmdBindDescriptorSets=material_descriptors;vkCmdDraw=material_draw;
    vkDestroyShaderModule(device, fragment, NULL);
    vkDestroyDescriptorPool(device, descriptors, NULL); vkDestroyDescriptorSetLayout(device, layout, NULL);
    vkDestroySampler(device, sampler, NULL); vkDestroyImageView(device, view, NULL);
    vkDestroyImage(device, alpha_image, NULL); vkFreeMemory(device, memory, NULL);
    drop_buffer(upload); drop_buffer(palette); drop_buffer(coords); drop_buffer(weights); drop_buffer(vertices);
    printf("Sun materials: %u captured GPU cases, %zu depth pixels; native owner/palette/material leases, explicit/remainder weights and all alpha comparisons\n", cases, pixels);
}
