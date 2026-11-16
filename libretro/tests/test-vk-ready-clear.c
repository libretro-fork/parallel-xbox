/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define assert(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
/* STATE */
typedef struct PipelineBinding {
    struct { bool clear; RenderPassState render_pass_state; } key;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkRenderPass render_pass;
} PipelineBinding;
typedef struct PGRAPHVkState {
    bool dyn_blend, pipeline_binding_changed;
    VkDevice device;
    VkPhysicalDevice physical_device;
    VkShaderModule quad_vert_module, solid_frag_module;
    VkPipelineCache vk_pipeline_cache;
    VkPipelineLayout clear_pipeline_layout;
    struct { VkFormat vk_format; } kelvin_surface_zeta_vk_map[3];
    /* OWNERS */
    PipelineBinding *pipeline_binding;
} PGRAPHVkState;
typedef struct PGRAPHState { PGRAPHVkState *vk_renderer_state; } PGRAPHState;
struct XemuVkTrashEntry { VkPipeline pipeline; };
typedef struct SurfaceFormatInfo {
    unsigned bytes;
    VkFormat vk_format;
    VkImageUsageFlags usage;
    VkImageAspectFlags aspect;
} SurfaceFormatInfo;
/* FORMAT MAP */
static unsigned created, destroyed, retired, failure, failure_kind;
static bool live[128], deferred[128], unsupported;
static VkColorComponentFlags expected_mask;
static bool expected_dynamic;
static VkRenderPass get_render_pass(PGRAPHVkState *r, const RenderPassState *s)
{
    (void)r;
    return (VkRenderPass)(uintptr_t)(1 + s->color_format * 256 + s->zeta_format);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice d, VkFormat f, VkFormatProperties *p)
{
    (void)d;
    memset(p, 0, sizeof(*p));
    if (!(unsupported && f == VK_FORMAT_R8_UNORM))
        p->optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice d, VkPipelineCache cache, uint32_t count,
    const VkGraphicsPipelineCreateInfo *p, const VkAllocationCallbacks *a, VkPipeline *out)
{
    (void)d; (void)cache; (void)a;
    assert(count == 1 && p->stageCount == 2);
    assert(p->pStages[0].stage == VK_SHADER_STAGE_VERTEX_BIT && p->pStages[1].stage == VK_SHADER_STAGE_FRAGMENT_BIT);
    assert(p->pColorBlendState->attachmentCount == 1);
    const VkPipelineColorBlendAttachmentState *blend = p->pColorBlendState->pAttachments;
    assert(blend->blendEnable && blend->dstColorBlendFactor == VK_BLEND_FACTOR_ZERO);
    assert(blend->srcColorBlendFactor == VK_BLEND_FACTOR_CONSTANT_COLOR);
    assert(blend->srcAlphaBlendFactor == VK_BLEND_FACTOR_CONSTANT_ALPHA);
    assert(blend->colorWriteMask == (expected_dynamic ? 0 : expected_mask));
    assert(p->pDynamicState->dynamicStateCount == (expected_dynamic ? 4u : 3u));
    if (expected_dynamic) assert(p->pDynamicState->pDynamicStates[3] == VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT);
    if (p->pDepthStencilState) {
        assert(!p->pDepthStencilState->depthTestEnable && !p->pDepthStencilState->depthWriteEnable);
        assert(!p->pDepthStencilState->stencilTestEnable);
    }
    unsigned id = ++created;
    assert(id < ARRAY_SIZE(live));
    *out = VK_NULL_HANDLE;
    if (id == failure && failure_kind != 1) return failure_kind == 2 ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
    *out = (VkPipeline)(uintptr_t)id;
    live[id] = true;
    return id == failure ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *a)
{
    (void)d; (void)a;
    unsigned id = (uintptr_t)p;
    assert(id && id < ARRAY_SIZE(live) && live[id]);
    live[id] = false;
    destroyed++;
}
static void pgraph_vk_trash_push(PGRAPHVkState *r, const struct XemuVkTrashEntry *e)
{
    (void)r;
    unsigned id = (uintptr_t)e->pipeline;
    assert(live[id] && !deferred[id]);
    deferred[id] = true;
    retired++;
}
/* PRODUCTION */
static void reset(PGRAPHVkState *r)
{
    for (unsigned i = 0; i < ARRAY_SIZE(live); i++) assert(!live[i]);
    memset(r, 0, sizeof(*r));
    memset(deferred, 0, sizeof(deferred));
    created = destroyed = retired = failure = failure_kind = 0;
    unsupported = false;
    expected_dynamic = r->dyn_blend = true;
    r->clear_pipeline_layout = (VkPipelineLayout)(uintptr_t)987;
    r->kelvin_surface_zeta_vk_map[1].vk_format = VK_FORMAT_D16_UNORM;
    r->kelvin_surface_zeta_vk_map[2].vk_format = VK_FORMAT_D24_UNORM_S8_UINT;
}
static void drain(PGRAPHVkState *r)
{
    for (unsigned i = 0; i < ARRAY_SIZE(live); i++) if (deferred[i]) {
        vkDestroyPipeline(r->device, (VkPipeline)(uintptr_t)i, NULL);
        deferred[i] = false;
    }
}
int main(void)
{
    PGRAPHVkState r;
    memset(live, 0, sizeof(live));
    reset(&r);
    PGRAPHState pg = {&r};
    init_ready_clear(&r);
    assert(created == 15 && r.ready_clear_count == 15);
    for (unsigned repeat = 0; repeat < 100; repeat++) {
        for (unsigned i = 0; i < r.ready_clear_count; i++) {
            assert(bind_ready_clear(&pg, &r.ready_clear[i].state));
            assert(r.pipeline_binding_changed);
            assert(r.pipeline_binding->pipeline == r.ready_clear[i].pipeline);
            assert(r.pipeline_binding->render_pass == r.ready_clear[i].render_pass);
            assert(r.pipeline_binding->layout == r.clear_pipeline_layout);
            assert(bind_ready_clear(&pg, &r.ready_clear[i].state));
            assert(!r.pipeline_binding_changed);
        }
    }
    RenderPassState missing = {VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_UNDEFINED};
    PipelineBinding *last = r.pipeline_binding;
    assert(!bind_ready_clear(&pg, &missing) && r.pipeline_binding == last);
    assert(created == 15 && destroyed == 0 && retired == 0);
    finalize_ready_clear(&r);
    assert(!r.pipeline_binding && r.pipeline_binding_changed && !r.ready_clear_count);
    assert(retired == 15 && destroyed == 0);
    for (unsigned i = 1; i <= created; i++) assert(live[i]);
    finalize_ready_clear(&r);
    assert(retired == 15);
    drain(&r);
    assert(destroyed == created);
    for (unsigned kind = 0; kind < 3; kind++) for (unsigned at = 1; at <= 15; at++) {
        reset(&r); failure = at; failure_kind = kind;
        init_ready_clear(&r);
        assert(!r.ready_clear_count && !retired && created == at);
        assert(destroyed == at - (kind != 1));
        assert(!bind_ready_clear(&pg, &missing));
        finalize_ready_clear(&r);
    }
    reset(&r); r.dyn_blend = false;
    init_ready_clear(&r);
    assert(!created && !r.ready_clear_count);
    reset(&r); unsupported = true;
    init_ready_clear(&r);
    assert(created == 12 && r.ready_clear_count == 12);
    finalize_ready_clear(&r); drain(&r);
    reset(&r); r.kelvin_surface_zeta_vk_map[2].vk_format = VK_FORMAT_D16_UNORM;
    init_ready_clear(&r);
    assert(created == 10 && r.ready_clear_count == 10);
    finalize_ready_clear(&r); drain(&r);
    for (unsigned dynamic = 0; dynamic < 2; dynamic++) for (unsigned mask = 1; mask < 15; mask++) {
        reset(&r); expected_dynamic = r.dyn_blend = dynamic;
        expected_mask = mask;
        for (unsigned z = 0; z < 3; z++) {
            RenderPassState state = {VK_FORMAT_B8G8R8A8_UNORM, r.kelvin_surface_zeta_vk_map[z].vk_format};
            VkPipeline pipeline;
            assert(create_clear_native(&r, &state, mask << 4, &pipeline) == VK_SUCCESS);
            vkDestroyPipeline(r.device, pipeline, NULL);
        }
    }
    reset(&r);
    puts("Vulkan masked clear: 15 bounded owners, 3000 hot binds, 45 injected startup failures, 84 static/dynamic mask builders and deferred retirement PASS");
    return 0;
}
