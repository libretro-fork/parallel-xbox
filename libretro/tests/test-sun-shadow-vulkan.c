/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <volk.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "hw/xbox/nv2a/pgraph/vk/sun-shadow-pass.h"
/* GPU HELPERS */

static PFN_vkCreateGraphicsPipelines real_create_pipelines;
static PFN_vkDestroyPipeline real_destroy_pipeline;
static PFN_vkDestroyPipelineLayout real_destroy_layout;
static unsigned failed_creates, freed_pipelines, freed_layouts;
static bool partial_pipeline;
static VKAPI_ATTR VkResult VKAPI_CALL fail_pipeline(
    VkDevice d, VkPipelineCache cache, uint32_t n, const VkGraphicsPipelineCreateInfo *ci,
    const VkAllocationCallbacks *a, VkPipeline *p)
{
    assert(n == 1); failed_creates++; *p = VK_NULL_HANDLE;
    if (partial_pipeline) CHECK(real_create_pipelines(d, cache, n, ci, a, p));
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}
static VKAPI_ATTR void VKAPI_CALL free_pipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *a)
{ freed_pipelines++; real_destroy_pipeline(d, p, a); }
static VKAPI_ATTR void VKAPI_CALL free_layout(VkDevice d, VkPipelineLayout p, const VkAllocationCallbacks *a)
{ freed_layouts++; real_destroy_layout(d, p, a); }

static PFN_vkCmdDraw real_draw;
static PFN_vkCmdDrawIndexed real_draw_indexed;
static PFN_vkCmdPushConstants real_push;
static PFN_vkCmdBindVertexBuffers real_bind;
static unsigned draws, pushes, bindings;
static VKAPI_ATTR void VKAPI_CALL count_draw(VkCommandBuffer c, uint32_t n, uint32_t instances, uint32_t first, uint32_t base)
{ draws++; real_draw(c, n, instances, first, base); }
static VKAPI_ATTR void VKAPI_CALL count_indexed(VkCommandBuffer c, uint32_t n, uint32_t instances, uint32_t first, int32_t base, uint32_t instance)
{ draws++; real_draw_indexed(c, n, instances, first, base, instance); }
static VKAPI_ATTR void VKAPI_CALL count_push(VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags f, uint32_t o, uint32_t n, const void *p)
{ assert(n == 80 && o == 0); pushes++; real_push(c, l, f, o, n, p); }
static VKAPI_ATTR void VKAPI_CALL count_bind(VkCommandBuffer c, uint32_t first, uint32_t n, const VkBuffer *b, const VkDeviceSize *o)
{ assert(first == 0 && n == 1 && *o == 16); bindings++; real_bind(c, first, n, b, o); }

static void identity(float *m, float z)
{
    memset(m, 0, 64); m[0] = m[5] = m[10] = m[15] = 1; m[14] = z;
}

#include "sun-shadow-capture-vulkan.h"
#include "sun-shadow-material-vulkan.h"
#include "sun-shadow-receiver-vulkan.h"

int main(void)
{
    gpu_init();
    GlslcResult code = {0}; GlslcOptions options = {0};
    if (!glslc_compile(GLSLC_STAGE_VERTEX, pgraph_vk_sun_vertex_glsl, &options, &code)) {
        fprintf(stderr, "%s\n", code.error); return 1;
    }
    VkShaderModule shader = gpu_module(code.words, code.num_words);
    glslc_result_free_words(&code);
    VkImageCreateInfo image_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_D32_SFLOAT,
        .extent = {WIDTH, HEIGHT, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    VkImage image; CHECK(vkCreateImage(device, &image_info, NULL, &image));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device, image, &req);
    VkMemoryAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size,
        .memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };
    VkDeviceMemory memory; CHECK(vkAllocateMemory(device, &allocate, NULL, &memory));
    CHECK(vkBindImageMemory(device, image, memory, 0));
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
    };
    VkImageView view; CHECK(vkCreateImageView(device, &view_info, NULL, &view));
    VkAttachmentDescription attachment = {
        .format = VK_FORMAT_D32_SFLOAT, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    };
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .pDepthStencilAttachment = &ref,
    };
    VkSubpassDependency dependency = {
        .srcSubpass = 0, .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
    };
    VkRenderPassCreateInfo render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment,
        .subpassCount = 1, .pSubpasses = &subpass,
        .dependencyCount = 1, .pDependencies = &dependency,
    };
    VkRenderPass render; CHECK(vkCreateRenderPass(device, &render_info, NULL, &render));
    VkFramebufferCreateInfo framebuffer_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = render,
        .attachmentCount = 1, .pAttachments = &view, .width = WIDTH, .height = HEIGHT, .layers = 1,
    };
    VkFramebuffer framebuffer; CHECK(vkCreateFramebuffer(device, &framebuffer_info, NULL, &framebuffer));
    real_create_pipelines = vkCreateGraphicsPipelines;
    real_destroy_pipeline = vkDestroyPipeline; real_destroy_layout = vkDestroyPipelineLayout;
    vkCreateGraphicsPipelines = fail_pipeline;
    vkDestroyPipeline = free_pipeline; vkDestroyPipelineLayout = free_layout;
    for (unsigned partial = 0; partial < 2; partial++) {
        partial_pipeline = partial;
        PgraphVkSunPass failed = {0};
        assert(pgraph_vk_sun_pass_create(&failed, device, render, shader,
            VK_FORMAT_R32G32B32A32_SFLOAT, 16, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST) == VK_ERROR_OUT_OF_DEVICE_MEMORY);
        assert(!failed.pipeline && !failed.layout && !failed.device);
    }
    assert(failed_creates == 2 && freed_layouts == 2 && freed_pipelines == 1);
    vkCreateGraphicsPipelines = real_create_pipelines;
    vkDestroyPipeline = real_destroy_pipeline; vkDestroyPipelineLayout = real_destroy_layout;
    real_draw = vkCmdDraw; real_draw_indexed = vkCmdDrawIndexed;
    real_push = vkCmdPushConstants; real_bind = vkCmdBindVertexBuffers;
    vkCmdDraw = count_draw; vkCmdDrawIndexed = count_indexed;
    vkCmdPushConstants = count_push; vkCmdBindVertexBuffers = count_bind;
    Buffer vertices = make_buffer(256, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    Buffer indices = make_buffer(64, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    Buffer output = make_buffer(WIDTH * HEIGHT * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    size_t pixels = 0;
    for (unsigned components = 3; components <= 4; components++)
    for (unsigned pad = 0; pad < 2; pad++)
    for (unsigned strip = 0; strip < 2; strip++) {
        PgraphVkSunPass pass = {0};
        unsigned stride = components * 4 + pad * 4;
        CHECK(pgraph_vk_sun_pass_create(&pass, device, render, shader,
            components == 3 ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT,
            stride, strip ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST));
        unsigned count = strip ? 4 : 3;
        for (unsigned indexing = 0; indexing < 3; indexing++)
        for (unsigned test = 0; test < 4; test++) {
            uint8_t geometry[256] = {0}, elements[64] = {0};
            const float triangle[4][4] = {{-1,-1,0,1},{3,-1,0,1},{-1,3,0,1},{0,0,0,1}};
            const float quad[4][4] = {{-1,-1,0,1},{-1,1,0,1},{1,-1,0,1},{1,1,0,1}};
            for (unsigned i = 0; i < count; i++) {
                memcpy(geometry + 16 + (i + 1) * stride, strip ? quad[i] : triangle[i], components * 4);
                uint16_t i16 = i; uint32_t i32 = i;
                memcpy(elements + 8 + (i + 1) * (indexing == 1 ? 2 : 4),
                       indexing == 1 ? (void *)&i16 : (void *)&i32, indexing == 1 ? 2 : 4);
            }
            begin();
            vkCmdUpdateBuffer(command, vertices.buffer, 0, sizeof(geometry), geometry);
            vkCmdUpdateBuffer(command, indices.buffer, 0, sizeof(elements), elements);
            memset(geometry, 0x99, sizeof(geometry)); memset(elements, 0x99, sizeof(elements));
            VkBufferMemoryBarrier barriers[2] = {
                {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                 .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                 .buffer = vertices.buffer, .size = VK_WHOLE_SIZE},
                {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                 .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_INDEX_READ_BIT,
                 .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                 .buffer = indices.buffer, .size = VK_WHOLE_SIZE},
            };
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                 0, 0, NULL, 2, barriers, 0, NULL);
            assert(pgraph_vk_sun_pass_begin(&pass, command, render, framebuffer, WIDTH, HEIGHT));
            PgraphVkSunDraw draw = {
                .vertex = vertices.buffer, .vertex_offset = 16, .vertex_size = sizeof(geometry) - 16,
                .index = indexing ? indices.buffer : VK_NULL_HANDLE,
                .index_offset = 8, .index_size = sizeof(elements) - 8,
                .index_type = indexing == 1 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32,
                .first = 1, .count = count, .vertex_base = indexing ? 1 : 0,
            };
            float depth = (test + 1) * 0.125f;
            identity(draw.world_to_light, depth);
            assert(pgraph_vk_sun_pass_draw(&pass, command, &draw));
            /* Push constants and scalar packet fields must already belong
             * to the recording; mutate them before submitting. The actual
             * GPU input versions remain live and unchanged until completion. */
            identity(draw.world_to_light, 0.875f);
            assert(pgraph_vk_sun_pass_draw(&pass, command, &draw));
            unsigned before_draws = draws, before_pushes = pushes, before_bindings = bindings;
            for (unsigned bad = 0; bad < 7; bad++) {
                PgraphVkSunDraw invalid = draw;
                switch (bad) {
                case 0: invalid.count = 0; break;
                case 1: invalid.vertex = VK_NULL_HANDLE; break;
                case 2: invalid.vertex_offset = UINT64_MAX - 3; break;
                case 3: invalid.world_to_light[3] = NAN; break;
                case 4: invalid.vertex_size = 1; break;
                case 5: invalid.count = UINT32_MAX; break;
                case 6:
                    if (indexing) invalid.index_size = 1;
                    else invalid.vertex_base = 1;
                    break;
                }
                assert(!pgraph_vk_sun_pass_draw(&pass, command, &invalid));
            }
            assert(draws == before_draws && pushes == before_pushes && bindings == before_bindings);
            memset(&draw, 0, sizeof(draw));
            assert(!pgraph_vk_sun_pass_draw(&pass, command, &draw));
            vkCmdEndRenderPass(command);
            VkBufferImageCopy copy = {
                .imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1},
                .imageExtent = {WIDTH, HEIGHT, 1},
            };
            vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output.buffer, 1, &copy);
            host_barrier(output, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
            submit();
            for (unsigned i = 0; i < WIDTH * HEIGHT; i++) {
                assert(fabsf(((float *)output.mapped)[i] - depth) < 1e-6f); pixels++;
            }
            gpu_cases++;
        }
        pgraph_vk_sun_pass_destroy(&pass);
        assert(!pass.pipeline && !pass.layout && !pass.device);
        pgraph_vk_sun_pass_destroy(&pass);
    }
    assert(draws == gpu_cases * 2 && pushes == draws && bindings == draws);
    vkCmdDraw = real_draw; vkCmdDrawIndexed = real_draw_indexed;
    vkCmdPushConstants = real_push; vkCmdBindVertexBuffers = real_bind;
    material_tests(render, framebuffer, image, output);
    capture_gpu_tests(render, framebuffer, image, output, shader);
    receiver_tests(render, framebuffer, image, view);
    drop_buffer(output); drop_buffer(indices); drop_buffer(vertices);
    vkDestroyFramebuffer(device, framebuffer, NULL); vkDestroyRenderPass(device, render, NULL);
    vkDestroyImageView(device, view, NULL); vkDestroyImage(device, image, NULL);
    vkFreeMemory(device, memory, NULL); vkDestroyShaderModule(device, shader, NULL);
    vkDestroyFence(device, fence, NULL); vkDestroyCommandPool(device, pool, NULL); vkDestroyDevice(device, NULL);
    if (messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy = (void *)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        destroy(instance, messenger, NULL);
    }
    vkDestroyInstance(instance, NULL);
    assert(!errors);
    printf("Sun caster pass: %u GPU cases, %zu depth pixels; validation=%u errors=%u\n",
           gpu_cases, pixels, validation_enabled, errors);
    return 0;
}
