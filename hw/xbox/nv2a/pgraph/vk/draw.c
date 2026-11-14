/*
 * Geforce NV2A PGRAPH Vulkan Renderer
 *
 * Copyright (c) 2024-2025 Matt Borgerson
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

#include "qemu/osdep.h"
#include <encodings/xxh3.h>
#include "qemu/file-map.h"
#include "ui/xemu-settings.h"
#include "renderer.h"
#include "pcache-worker.h"
#include "libretro/dsp-ownership.h"
#include "hw/xbox/nv2a/pgraph/spv/clear.h"
#include "hw/xbox/nv2a/pgraph/blend.h"
#include <math.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>

static void pcache_save_now(void *arg);
static void pump_quiesce(PGRAPHState *pg);
static void gpl_init(PGRAPHVkState *r);
static void init_pipeline_layouts(PGRAPHVkState *r);
static void finalize_pipeline_layouts(PGRAPHVkState *r);
static void gpl_finalize(PGRAPHVkState *r);
static void destroy_framebuffer_cache(PGRAPHVkState *r);
static bool vk_cost_probe_active(PGRAPHState *pg);

void pgraph_vk_draw_begin(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    NV2A_VK_DPRINTF("NV097_SET_BEGIN_END: 0x%x", d->pgraph.primitive_mode);

    if (vk_cost_probe_active(pg)) {
        xemu_cost_draw_begin(d, &pgraph_vk_cost_ops);
    }

    uint32_t control_0 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0);
    bool mask_alpha = control_0 & NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE;
    bool mask_red = control_0 & NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE;
    bool mask_green = control_0 & NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE;
    bool mask_blue = control_0 & NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE;
    bool color_write = mask_alpha || mask_red || mask_green || mask_blue;
    bool depth_test = control_0 & NV_PGRAPH_CONTROL_0_ZENABLE;
    bool stencil_test =
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1) & NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;
    bool is_nop_draw = !(color_write || depth_test || stencil_test);

    pgraph_vk_surface_update(d, true, true, depth_test || stencil_test);

    if (is_nop_draw) {
        NV2A_VK_DPRINTF("nop!");
        return;
    }
}

static VkPrimitiveTopology topology_for_state(const ShaderState *st)
{
    int polygon_mode = st->geom.polygon_front_mode;
    int primitive_mode = st->geom.primitive_mode;

    // FIXME: Replace with LUT
    switch (primitive_mode) {
    case PRIM_TYPE_POINTS:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PRIM_TYPE_LINES:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PRIM_TYPE_LINE_LOOP:
        // FIXME: line strips, except that the first and last vertices are also used as a line
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PRIM_TYPE_LINE_STRIP:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PRIM_TYPE_TRIANGLES:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PRIM_TYPE_TRIANGLE_STRIP:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PRIM_TYPE_TRIANGLE_FAN:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case PRIM_TYPE_QUADS:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
    case PRIM_TYPE_QUAD_STRIP:
        return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
    case PRIM_TYPE_POLYGON:
        if (polygon_mode == POLY_MODE_LINE) {
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; // FIXME
        } else if (polygon_mode == POLY_MODE_FILL) {
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        }
        assert(!"PRIM_TYPE_POLYGON with invalid polygon_mode");
        return 0;
    default:
        assert(!"Invalid primitive_mode");
        return 0;
    }
}

static VkPrimitiveTopology get_primitive_topology(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    return topology_for_state(&r->shader_binding->state);
}

/* The class a dynamic topology must stay in without
 * dynamicPrimitiveTopologyUnrestricted: point, line or triangle. */
static VkPrimitiveTopology topology_class(VkPrimitiveTopology t)
{
    switch (t) {
    case VK_PRIMITIVE_TOPOLOGY_POINT_LIST:
        return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST:
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
    case VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY:
    case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
        return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    default:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

static void pipeline_cache_entry_init(Lru *lru, LruNode *node,
                                      const void *state)
{
    PipelineBinding *snode = container_of(node, PipelineBinding, node);
    snode->layout = VK_NULL_HANDLE;
    snode->pipeline = VK_NULL_HANDLE;
    snode->draw_time = 0;
    snode->has_dynamic_line_width = false;
}

static void pipeline_cache_entry_post_evict(Lru *lru, LruNode *node)
{
    PGRAPHVkState *r = container_of(lru, PGRAPHVkState, pipeline_cache);
    PipelineBinding *snode = container_of(node, PipelineBinding, node);

    assert((snode->pipeline == VK_NULL_HANDLE || !r->in_command_buffer ||
            snode->draw_time < r->command_buffer_start_time) &&
           "Pipeline evicted while in use!");

    /* An in-flight slot may still execute draws bound to this pipeline:
     * destroy it once every slot has settled. */
    /* Layouts are renderer-owned lends, shared by cache entries and jobs. */
    struct XemuVkTrashEntry e = { .pipeline = snode->pipeline };
    if (snode->pipeline != VK_NULL_HANDLE) {
        pgraph_vk_trash_push(r, &e);
    }
    snode->pipeline = VK_NULL_HANDLE;
    snode->layout = VK_NULL_HANDLE;
    /* The node goes to another key (a pipeline the worker made landing,
     * pipeline_land): the next draw looks its pipeline up again rather
     * than binding what the node holds then */
    if (r->pipeline_binding == snode) {
        r->pipeline_binding = NULL;
    }
}

static bool pipeline_cache_entry_compare(Lru *lru, LruNode *node,
                                         const void *key)
{
    PipelineBinding *snode = container_of(node, PipelineBinding, node);
    return memcmp(&snode->key, key, sizeof(PipelineKey));
}

static void init_pipeline_cache(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* Seed the pipeline cache from disk, the file mapped and read by the
     * driver as it lies; it validates the blob header itself and falls
     * back to an empty cache on mismatch. */
    XemuFileMap pc;
    char *pc_path = g_strdup_printf("%s/vk_pipeline_cache.bin",
                                    xemu_settings_get_base_path());
    xemu_file_map(&pc, pc_path);
    r->pcache_path = pc_path;

    VkPipelineCacheCreateInfo cache_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .flags = 0,
        .initialDataSize = pc.len,
        .pInitialData = pc.data,
        .pNext = NULL,
    };
    if (vkCreatePipelineCache(r->device, &cache_info, NULL,
                              &r->vk_pipeline_cache) != VK_SUCCESS) {
        cache_info.initialDataSize = 0;
        cache_info.pInitialData = NULL;
        VK_CHECK(vkCreatePipelineCache(r->device, &cache_info, NULL,
                                       &r->vk_pipeline_cache));
    }
    xemu_file_unmap(&pc);

    /* Only growth beyond the loaded blob triggers a save. */
    r->pcache_saved_len = 0;
    vkGetPipelineCacheData(r->device, r->vk_pipeline_cache,
                           &r->pcache_saved_len, NULL);
    r->pcache_blob_capacity = MAX(r->pcache_saved_len, 64 * 1024);
    r->pcache_blob = g_malloc(r->pcache_blob_capacity);
    /* Startup only: a flip never creates or joins a host thread. */
    r->pcache_worker = vk_pcache_worker_create(pcache_save_now, r);

    /* MEASURED: Crazy Taxi keeps more than 2048 pipelines live. */
    const size_t pipeline_cache_size = 8192;
    lru_init(&r->pipeline_cache);
    r->pipeline_cache_entries =
        g_malloc_n(pipeline_cache_size, sizeof(PipelineBinding));
    assert(r->pipeline_cache_entries != NULL);
    for (int i = 0; i < pipeline_cache_size; i++) {
        lru_add_free(&r->pipeline_cache, &r->pipeline_cache_entries[i].node);
    }

    r->pipeline_cache.init_node = pipeline_cache_entry_init;
    r->pipeline_cache.compare_nodes = pipeline_cache_entry_compare;
    r->pipeline_cache.post_node_evict = pipeline_cache_entry_post_evict;
}

/* Optional flip marks wake a persistent startup-created worker. The worker
 * alone owns export scratch and saved length until lifecycle teardown joins
 * it. Driver cache synchronization can still contend with compilation. */
static void pcache_save_now(void *arg)
{
    PGRAPHVkState *r = arg;
    size_t pc_len = 0;
    if (vkGetPipelineCacheData(r->device, r->vk_pipeline_cache, &pc_len,
                               NULL) != VK_SUCCESS ||
        !pc_len || pc_len == r->pcache_saved_len) {
        return;
    }
    /* The persistent worker owns scratch serially; teardown joins it before
     * the final synchronous save. Old contents need not survive growth. */
    if (pc_len > r->pcache_blob_capacity) {
        size_t grown = r->pcache_blob_capacity <= SIZE_MAX / 2 ?
                       r->pcache_blob_capacity * 2 : pc_len;
        r->pcache_blob_capacity = MAX(pc_len, grown);
        g_free(r->pcache_blob);
        r->pcache_blob = g_malloc(r->pcache_blob_capacity);
    }
    pc_len = r->pcache_blob_capacity;
    if (vkGetPipelineCacheData(r->device, r->vk_pipeline_cache, &pc_len,
                               r->pcache_blob) == VK_SUCCESS) {
        if (g_file_set_contents(r->pcache_path, r->pcache_blob, pc_len, NULL)) {
            r->pcache_saved_len = pc_len;
        }
    }
}

void pgraph_vk_save_pipeline_cache(PGRAPHState *pg, bool sync)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (sync) {
        vk_pcache_worker_destroy(r->pcache_worker);
        r->pcache_worker = NULL;
        pcache_save_now(r);
    } else {
        vk_pcache_worker_request(r->pcache_worker);
    }
}

static void finalize_pipeline_cache(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    lru_flush(&r->pipeline_cache);
    g_free(r->pipeline_cache_entries);
    r->pipeline_cache_entries = NULL;

    pgraph_vk_save_pipeline_cache(pg, true);
    g_free(r->pcache_blob);
    r->pcache_blob = NULL;
    r->pcache_blob_capacity = 0;
    g_free(r->pcache_path);
    r->pcache_path = NULL;

    vkDestroyPipelineCache(r->device, r->vk_pipeline_cache, NULL);
}

static void init_clear_shaders(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    SpvStaticModule vertex = pgraph_spv_fullscreen_vertex();
    SpvStaticModule fragment = pgraph_spv_solid_fragment();
    r->quad_vert_module = pgraph_vk_create_shader_module_from_words(
        r, vertex.words, vertex.num_words);
    r->solid_frag_module = pgraph_vk_create_shader_module_from_words(
        r, fragment.words, fragment.num_words);
}

static void finalize_clear_shaders(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    vkDestroyShaderModule(r->device, r->quad_vert_module, NULL);
    vkDestroyShaderModule(r->device, r->solid_frag_module, NULL);
    r->quad_vert_module = r->solid_frag_module = VK_NULL_HANDLE;
}

static void init_render_passes(PGRAPHVkState *r)
{
    r->render_passes = g_array_new(false, false, sizeof(RenderPass));
}

static void finalize_render_passes(PGRAPHVkState *r)
{
    for (int i = 0; i < r->render_passes->len; i++) {
        RenderPass *p = &g_array_index(r->render_passes, RenderPass, i);
        vkDestroyRenderPass(r->device, p->render_pass, NULL);
    }
    g_array_free(r->render_passes, true);
    r->render_passes = NULL;
}

void pgraph_vk_init_pipelines(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    init_pipeline_layouts(r);
    init_pipeline_cache(pg);
    gpl_init(r);
    init_clear_shaders(pg);
    init_render_passes(r);
}

void pgraph_vk_finalize_pipelines(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    pump_quiesce(pg);
    for (unsigned i = 0; i < r->pump.n; i++) {
        g_array_free(r->pump.retry[i], true);
    }
    g_free(r->pump.state_hash);
    /* Shader finalization releases the seed bank next. Retired progress
     * must not retain its borrowed base or the freed retry/hash arrays. */
    memset(&r->pump, 0, sizeof(r->pump));
    destroy_framebuffer_cache(r);
    finalize_clear_shaders(pg);
    finalize_pipeline_cache(pg);
    /* The linked pipelines the cache flush left in the trash go before the
     * libraries they were linked from. */
    pgraph_vk_wait_all_slots(r);
    pgraph_vk_trash_drain(r);
    gpl_finalize(r);
    finalize_pipeline_layouts(r);
    finalize_render_passes(r);
}

static void init_render_pass_state(PGRAPHState *pg, RenderPassState *state)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    state->color_format = r->color_binding ?
                              r->color_binding->host_fmt.vk_format :
                              VK_FORMAT_UNDEFINED;
    state->zeta_format = r->zeta_binding ? r->zeta_binding->host_fmt.vk_format :
                                           VK_FORMAT_UNDEFINED;
}

static VkRenderPass create_render_pass(PGRAPHVkState *r, RenderPassState *state)
{
    NV2A_VK_DPRINTF("Creating render pass");

    VkAttachmentDescription attachments[2];
    int num_attachments = 0;

    bool color = state->color_format != VK_FORMAT_UNDEFINED;
    bool zeta = state->zeta_format != VK_FORMAT_UNDEFINED;

    VkAttachmentReference color_reference;
    if (color) {
        attachments[num_attachments] = (VkAttachmentDescription){
            .format = state->color_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        color_reference = (VkAttachmentReference){
            num_attachments, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        num_attachments++;
    }

    VkAttachmentReference depth_reference;
    if (zeta) {
        attachments[num_attachments] = (VkAttachmentDescription){
            .format = state->zeta_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        depth_reference = (VkAttachmentReference){
            num_attachments, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        num_attachments++;
    }

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
    };

    if (color) {
        dependency.srcStageMask |=
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstStageMask |=
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }

    if (zeta) {
        dependency.srcStageMask |=
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask |=
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dstStageMask |=
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask |=
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = color ? 1 : 0,
        .pColorAttachments = color ? &color_reference : NULL,
        .pDepthStencilAttachment = zeta ? &depth_reference : NULL,
    };

    VkRenderPassCreateInfo renderpass_create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = num_attachments,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    VkRenderPass render_pass;
    VK_CHECK(vkCreateRenderPass(r->device, &renderpass_create_info, NULL,
                                &render_pass));
    return render_pass;
}

static VkRenderPass add_new_render_pass(PGRAPHVkState *r, RenderPassState *state)
{
    RenderPass new_pass;
    memcpy(&new_pass.state, state, sizeof(*state));
    new_pass.render_pass = create_render_pass(r, state);
    g_array_append_vals(r->render_passes, &new_pass, 1);
    return new_pass.render_pass;
}

static VkRenderPass get_render_pass(PGRAPHVkState *r, RenderPassState *state)
{
    for (int i = 0; i < r->render_passes->len; i++) {
        RenderPass *p = &g_array_index(r->render_passes, RenderPass, i);
        if (!memcmp(&p->state, state, sizeof(*state))) {
            return p->render_pass;
        }
    }
    return add_new_render_pass(r, state);
}

/* Retire a cache entry: the trash destroys the framebuffer once neither a
 * submission nor the open recording can use it. */
static void retire_framebuffer_entry(PGRAPHVkState *r, int i)
{
    struct XemuVkTrashEntry e = {
        .framebuffer = r->framebuffer_cache[i].framebuffer,
    };
    pgraph_vk_trash_push(r, &e);
    if (r->framebuffer == r->framebuffer_cache[i].framebuffer) {
        r->framebuffer = VK_NULL_HANDLE;
        r->framebuffer_dirty = true;
    }
    r->framebuffer_cache[i] = r->framebuffer_cache[--r->framebuffer_cache_n];
}

/* A surface image view is going away: every framebuffer built on it
 * goes with it (a later view may reuse the handle value). */
void pgraph_vk_framebuffer_drop_view(PGRAPHVkState *r, VkImageView view)
{
    if (view == VK_NULL_HANDLE) {
        return;
    }
    for (int i = 0; i < r->framebuffer_cache_n;) {
        if (r->framebuffer_cache[i].color == view ||
            r->framebuffer_cache[i].zeta == view) {
            retire_framebuffer_entry(r, i);
        } else {
            i++;
        }
    }
}

static void destroy_framebuffer_cache(PGRAPHVkState *r)
{
    while (r->framebuffer_cache_n) {
        retire_framebuffer_entry(r, 0);
    }
}

static void create_frame_buffer(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(r->color_binding || r->zeta_binding);

    VkImageView color = r->color_binding ? r->color_binding->image_view :
                                           VK_NULL_HANDLE;
    VkImageView zeta = r->zeta_binding ? r->zeta_binding->image_view :
                                         VK_NULL_HANDLE;
    SurfaceBinding *binding = r->color_binding ? : r->zeta_binding;
    unsigned int width = binding->width, height = binding->height;
    pgraph_apply_scaling_factor(pg, &width, &height);

    r->framebuffer_use_counter++;
    for (int i = 0; i < r->framebuffer_cache_n; i++) {
        struct XemuVkFramebufferEntry *e = &r->framebuffer_cache[i];
        if (e->color == color && e->zeta == zeta &&
            e->render_pass == r->render_pass && e->width == width &&
            e->height == height) {
            e->last_use = r->framebuffer_use_counter;
            r->framebuffer = e->framebuffer;
            return;
        }
    }

    NV2A_VK_DPRINTF("Creating framebuffer");

    if (r->framebuffer_cache_n == ARRAY_SIZE(r->framebuffer_cache)) {
        int oldest = 0;
        for (int i = 1; i < r->framebuffer_cache_n; i++) {
            if (r->framebuffer_cache[i].last_use <
                r->framebuffer_cache[oldest].last_use) {
                oldest = i;
            }
        }
        retire_framebuffer_entry(r, oldest);
    }

    VkImageView attachments[2];
    int attachment_count = 0;
    if (color) {
        attachments[attachment_count++] = color;
    }
    if (zeta) {
        attachments[attachment_count++] = zeta;
    }
    VkFramebufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = r->render_pass,
        .attachmentCount = attachment_count,
        .pAttachments = attachments,
        .width = width,
        .height = height,
        .layers = 1,
    };
    struct XemuVkFramebufferEntry *e =
        &r->framebuffer_cache[r->framebuffer_cache_n++];
    *e = (struct XemuVkFramebufferEntry){
        .color = color,
        .zeta = zeta,
        .render_pass = r->render_pass,
        .width = width,
        .height = height,
        .last_use = r->framebuffer_use_counter,
    };
    VK_CHECK(vkCreateFramebuffer(r->device, &create_info, NULL,
                                 &e->framebuffer));
    r->framebuffer = e->framebuffer;
}
static void create_clear_pipeline(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    NV2A_VK_DGROUP_BEGIN("Creating clear pipeline");

    PipelineKey key;
    memset(&key, 0, sizeof(key));
    key.clear = true;
    init_render_pass_state(pg, &key.render_pass_state);

    key.regs[0] = r->clear_parameter;

    uint64_t hash = xxh3_64((void *)&key, sizeof(key), 0);
    LruNode *node = lru_lookup(&r->pipeline_cache, hash, &key);
    PipelineBinding *snode = container_of(node, PipelineBinding, node);

    if (snode->pipeline != VK_NULL_HANDLE) {
        NV2A_VK_DPRINTF("Cache hit");
        r->pipeline_binding_changed = r->pipeline_binding != snode;
        r->pipeline_binding = snode;
        NV2A_VK_DGROUP_END();
        return;
    }

    NV2A_VK_DPRINTF("Cache miss");
    nv2a_profile_inc_counter(NV2A_PROF_PIPELINE_GEN);
    memcpy(&snode->key, &key, sizeof(key));

    bool clear_any_color_channels =
        r->clear_parameter & NV097_CLEAR_SURFACE_COLOR;
    bool clear_all_color_channels =
        (r->clear_parameter & NV097_CLEAR_SURFACE_COLOR) ==
        (NV097_CLEAR_SURFACE_R | NV097_CLEAR_SURFACE_G | NV097_CLEAR_SURFACE_B |
         NV097_CLEAR_SURFACE_A);
    bool partial_color_clear =
        clear_any_color_channels && !clear_all_color_channels;

    int num_active_shader_stages = 0;
    VkPipelineShaderStageCreateInfo shader_stages[2];
    shader_stages[num_active_shader_stages++] =
        (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = r->quad_vert_module,
            .pName = "main",
        };
    if (partial_color_clear) {
        shader_stages[num_active_shader_stages++] =
            (VkPipelineShaderStageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = r->solid_frag_module,
                .pName = "main",
            };
     }

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
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable =
            (r->clear_parameter & NV097_CLEAR_SURFACE_Z) ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_ALWAYS,
        .depthBoundsTestEnable = VK_FALSE,
    };

    if (r->clear_parameter & NV097_CLEAR_SURFACE_STENCIL) {
        depth_stencil.stencilTestEnable = VK_TRUE;
        depth_stencil.front.failOp = VK_STENCIL_OP_REPLACE;
        depth_stencil.front.passOp = VK_STENCIL_OP_REPLACE;
        depth_stencil.front.depthFailOp = VK_STENCIL_OP_REPLACE;
        depth_stencil.front.compareOp = VK_COMPARE_OP_ALWAYS;
        depth_stencil.front.compareMask = 0xff;
        depth_stencil.front.writeMask = 0xff;
        depth_stencil.front.reference = 0xff;
        depth_stencil.back = depth_stencil.front;
    }

    VkColorComponentFlags write_mask = 0;
    if (r->clear_parameter & NV097_CLEAR_SURFACE_R)
        write_mask |= VK_COLOR_COMPONENT_R_BIT;
    if (r->clear_parameter & NV097_CLEAR_SURFACE_G)
        write_mask |= VK_COLOR_COMPONENT_G_BIT;
    if (r->clear_parameter & NV097_CLEAR_SURFACE_B)
        write_mask |= VK_COLOR_COMPONENT_B_BIT;
    if (r->clear_parameter & NV097_CLEAR_SURFACE_A)
        write_mask |= VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = write_mask,
        .blendEnable = VK_TRUE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .srcColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_COLOR,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA,
    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = r->color_binding ? 1 : 0,
        .pAttachments = r->color_binding ? &color_blend_attachment : NULL,
    };

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT,
                                        VK_DYNAMIC_STATE_SCISSOR,
                                        VK_DYNAMIC_STATE_BLEND_CONSTANTS };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = partial_color_clear ? 3 : 2,
        .pDynamicStates = dynamic_states,
    };

    VkPipelineLayout layout = r->clear_pipeline_layout;

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = num_active_shader_stages,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = r->zeta_binding ? &depth_stencil : NULL,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = layout,
        .renderPass = get_render_pass(r, &key.render_pass_state),
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
    };

    VkPipeline pipeline;
    VK_CHECK(vkCreateGraphicsPipelines(r->device, r->vk_pipeline_cache, 1,
                                       &pipeline_info, NULL, &pipeline));

    snode->pipeline = pipeline;
    snode->layout = layout;
    snode->render_pass = pipeline_info.renderPass;
    snode->draw_time = pg->draw_time;

    r->pipeline_binding = snode;
    r->pipeline_binding_changed = true;

    NV2A_VK_DGROUP_END();
}

static bool check_render_pass_dirty(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(r->pipeline_binding);

    RenderPassState state;
    init_render_pass_state(pg, &state);

    return memcmp(&state, &r->pipeline_binding->key.render_pass_state,
                  sizeof(state)) != 0;
}

static const unsigned pipeline_registers[] = {
    NV_PGRAPH_BLEND,       NV_PGRAPH_BLENDCOLOR,  NV_PGRAPH_CONTROL_0,
    NV_PGRAPH_CONTROL_1,   NV_PGRAPH_CONTROL_2,   NV_PGRAPH_CONTROL_3,
    NV_PGRAPH_SETUPRASTER, NV_PGRAPH_ZOFFSETBIAS, NV_PGRAPH_ZOFFSETFACTOR,
};

static inline void pipeline_reg_values(PGRAPHState *pg, uint32_t *out)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    /* Only the bits create_pipeline() consumes enter the key: any other bit
     * (e.g. animated ZOFFSET* floats) would make duplicate pipelines. */
    static const uint32_t reg_masks[] = {
        /* BLEND: EQN | EN | SFACTOR | DFACTOR */
        PGRAPH_BLEND_STATE_MASK,
        /* BLENDCOLOR: constants, consumed whole */
        0xFFFFFFFF,
        /* CONTROL_0: ZENABLE | ZFUNC | Z/STENCIL/RGBA WRITE_ENABLE */
        0x3F0F4000,
        /* CONTROL_1: STENCIL_TEST | FUNC | REF | MASK_READ | MASK_WRITE */
        0xFFFFFFF1,
        /* CONTROL_2: STENCIL_OP_FAIL | ZFAIL | ZPASS */
        0x00000FFF,
        /* CONTROL_3: not consumed */
        0x00000000,
        /* SETUPRASTER: FRONTFACE | CULLENABLE | CULLCTRL */
        0x10E00000,
        /* ZOFFSETBIAS/FACTOR: not consumed (depthBiasEnable hardcoded off) */
        0x00000000,
        0x00000000,
    };
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(pipeline_registers) != ARRAY_SIZE(((PipelineKey *)0)->regs));
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(reg_masks) != ARRAY_SIZE(pipeline_registers));
    for (int i = 0; i < ARRAY_SIZE(pipeline_registers); i++) {
        out[i] = pgraph_reg_r(pg, pipeline_registers[i]) & reg_masks[i];
    }
    /* State set dynamically does not identify a pipeline. */
    out[1] = 0; /* BLENDCOLOR: VK_DYNAMIC_STATE_BLEND_CONSTANTS */
    if (r->dyn_blend) {
        out[0] = 0;
        out[2] &= ~(NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE |
                    NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE |
                    NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE |
                    NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE);
    } else {
        out[0] = pgraph_surface_blend(out[0],
                                      pg->surface_shape.color_format);
    }
    if (r->dyn_cull_front) {
        out[6] = 0;
        out[2] &= ~(NV_PGRAPH_CONTROL_0_ZENABLE |
                    NV_PGRAPH_CONTROL_0_ZFUNC |
                    NV_PGRAPH_CONTROL_0_ZWRITEENABLE |
                    NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE);
        out[3] = 0;
        out[4] = 0;
    }
}

// Quickly check for any state changes that would require more analysis
static bool check_pipeline_dirty(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* The pipeline bound last is the clear's (create_clear_pipeline): not
     * one a draw can use, whatever else is unchanged */
    if (!r->pipeline_binding || r->pipeline_binding->key.clear ||
        r->shader_bindings_changed || r->texture_bindings_changed ||
        check_render_pass_dirty(pg)) {
        return true;
    }

    /* XRGB and ARGB can share a host attachment format. A guest format
     * switch can change static blend factors without a BLEND write. */
    if (!r->dyn_blend && r->pipeline_binding->key.regs[0] !=
        (pgraph_surface_blend(pgraph_reg_r(pg, NV_PGRAPH_BLEND),
                               pg->surface_shape.color_format) &
         PGRAPH_BLEND_STATE_MASK)) {
        return true;
    }

    for (int i = 0; i < ARRAY_SIZE(pipeline_registers); i++) {
        if (pgraph_is_reg_dirty(pg, pipeline_registers[i])) {
            /* A register write may change only dynamic or unused bits. Use
             * the same projection as the key before rebuilding/hashing it. */
            uint32_t values[ARRAY_SIZE(pipeline_registers)];
            pipeline_reg_values(pg, values);
            if (memcmp(values, r->pipeline_binding->key.regs, sizeof(values))) {
                return true;
            }
            break;
        }
    }

    /* With dynamic vertex input the key carries no descriptions (they are
     * set on the command buffer, emit_dynamic_vertex_input compares them
     * itself): compared against the key's zeros, every draw with a vertex
     * array counted as dirty and rebuilt, hashed and looked up its key. */
    /* init_pipeline_key zeroes the unused tail. A valid format immediately
     * beyond the new list means the old static pipeline has more attributes;
     * comparing only the new prefix would accept it (including an empty list). */
    // FIXME: Use dirty bits instead
    if (!r->dyn_vertex_input &&
        ((r->num_active_vertex_attribute_descriptions <
              ARRAY_SIZE(r->pipeline_binding->key.attribute_descriptions) &&
          r->pipeline_binding->key.attribute_descriptions[
              r->num_active_vertex_attribute_descriptions].format != VK_FORMAT_UNDEFINED) ||
         memcmp(r->vertex_attribute_descriptions,
               r->pipeline_binding->key.attribute_descriptions,
               r->num_active_vertex_attribute_descriptions *
                   sizeof(r->vertex_attribute_descriptions[0])) ||
        memcmp(r->vertex_binding_descriptions,
               r->pipeline_binding->key.binding_descriptions,
               r->num_active_vertex_binding_descriptions *
                   sizeof(r->vertex_binding_descriptions[0])))) {
        return true;
    }

    nv2a_profile_inc_counter(NV2A_PROF_PIPELINE_NOTDIRTY);

    return false;
}

static void init_pipeline_key(PGRAPHState *pg, PipelineKey *key)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* The shader state is copied in full below, including its padding.
     * Clear only the surrounding bytes; every hashed key byte stays defined. */
    memset(key, 0, offsetof(PipelineKey, shader_state));
    const size_t after_shader = offsetof(PipelineKey, shader_state) +
                                sizeof(key->shader_state);
    memset((char *)key + after_shader, 0, sizeof(*key) - after_shader);
    init_render_pass_state(pg, &key->render_pass_state);
    memcpy(&key->shader_state, &r->shader_binding->state,
           sizeof(ShaderState));
    if (!r->dyn_vertex_input) {
        memcpy(key->binding_descriptions, r->vertex_binding_descriptions,
               sizeof(key->binding_descriptions[0]) *
                   r->num_active_vertex_binding_descriptions);
        memcpy(key->attribute_descriptions, r->vertex_attribute_descriptions,
               sizeof(key->attribute_descriptions[0]) *
                   r->num_active_vertex_attribute_descriptions);
    }

    pipeline_reg_values(pg, key->regs);
}

/* Line widths are set per draw where the device has wide lines. */
static bool dynamic_line_width(PGRAPHVkState *r, const ShaderState *st)
{
    return r->enabled_physical_device_features.wideLines == VK_TRUE &&
           (st->geom.polygon_front_mode == POLY_MODE_LINE ||
            st->geom.primitive_mode == PRIM_TYPE_LINES ||
            st->geom.primitive_mode == PRIM_TYPE_LINE_LOOP ||
            st->geom.primitive_mode == PRIM_TYPE_LINE_STRIP);
}

static int fill_dynamic_states(PGRAPHVkState *r, VkDynamicState *out,
                               bool blend_constants)
{
    int n = 0;
    out[n++] = VK_DYNAMIC_STATE_VIEWPORT;
    out[n++] = VK_DYNAMIC_STATE_SCISSOR;
    if (blend_constants) {
        out[n++] = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
    }
    if (r->dyn_cull_front) {
        static const VkDynamicState eds1[] = {
            VK_DYNAMIC_STATE_CULL_MODE_EXT,
            VK_DYNAMIC_STATE_FRONT_FACE_EXT,
            VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT,
            VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE_EXT,
            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT,
            VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT,
            VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE_EXT,
            VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT,
            VK_DYNAMIC_STATE_STENCIL_OP_EXT,
            VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
            VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
            VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        };
        for (int i = 0; i < ARRAY_SIZE(eds1); i++) {
            out[n++] = eds1[i];
        }
    }
    if (r->dyn_blend) {
        out[n++] = VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT;
        out[n++] = VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT;
        out[n++] = VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT;
    }
    if (r->dyn_vertex_input) {
        out[n++] = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
    }
    return n;
}

/*
 * Pipeline libraries: the pre-rasterization half (vertex input, vertex and
 * geometry shaders, rasterizer) and the fragment half (fragment shader,
 * depth/stencil, blend, attachments) are each compiled once; linking them
 * costs far less than compiling a pipeline.
 */

/* Canonical library metadata; no shader or geometry payload. */
typedef struct GplPreKey {
        uint64_t vsh, geom;
        RenderPassState rp;
        VkPipelineRasterizationStateCreateInfo raster;
        VkPipelineInputAssemblyStateCreateInfo ia;
        VkVertexInputBindingDescription bind[NV2A_VERTEXSHADER_ATTRIBUTES];
        VkVertexInputAttributeDescription attr[NV2A_VERTEXSHADER_ATTRIBUTES];
        uint32_t nb, na;
} GplPreKey;

typedef struct GplFragKey {
        uint64_t psh;
        VkPipelineDepthStencilStateCreateInfo ds;
        VkPipelineColorBlendAttachmentState blend;
        RenderPassState rp;
        uint32_t has_zeta, attachments, blend_valid, push_size;
} GplFragKey;

#include "s3tc-pipeline-job.h"

/* Queued pipeline compiles own every structure their create info references.
 * Synchronous creation may borrow its caller's key until the call returns. */
typedef struct {
    bool frag;
    bool compute; /* uses the existing padding before hash */
    bool full;     /* a whole pipeline, for the pipeline cache */
    uint64_t hash; /* a library's; a whole pipeline's key hash */
    uint64_t state_hash; /* full: the shader state it was met as */
    VkDevice device;
    VkPipelineCache cache;
    VkPipelineLayout layout; /* renderer lend through worker completion */
    ShaderModuleInfo *mods[3];
    int nmods;
    VkGraphicsPipelineCreateInfo ci;
    VkGraphicsPipelineLibraryCreateInfoEXT lib_info;
    VkPipelineShaderStageCreateInfo stages[3];
    VkPipelineVertexInputStateCreateInfo vi;
    /* Borrowed by synchronous creation; queued jobs point at their own key. */
    const PipelineKey *vertex_key;
    VkPipelineInputAssemblyStateCreateInfo ia;
    VkPipelineViewportStateCreateInfo vp;
    VkPipelineRasterizationStateCreateInfo raster;
    VkPipelineMultisampleStateCreateInfo ms;
    VkPipelineDepthStencilStateCreateInfo ds;
    VkPipelineColorBlendStateCreateInfo cb;
    VkPipelineColorBlendAttachmentState cba;
    VkPipelineDynamicStateCreateInfo dyn;
    VkDynamicState dyn_states[24];
    union {
        PipelineKey key; /* complete PSO identity */
        GplPreKey pre_key;
        GplFragKey frag_key;
        S3tcPipelineJob s3tc; /* fixed compute metadata, not a key snapshot */
    }; /* final slot owns the active identity through worker completion */
    bool has_dynamic_line_width;
    VkPipeline result;
} LibJob;

typedef struct LibJobHeaderBeforeCompute { bool frag,full; uint64_t hash; } LibJobHeaderBeforeCompute;
_Static_assert(offsetof(LibJob,hash)==offsetof(LibJobHeaderBeforeCompute,hash),
               "compute discriminator uses existing job-header padding");
_Static_assert(sizeof(S3tcPipelineJob)<=sizeof(PipelineKey),"compute metadata fits native job union");

_Static_assert(sizeof(GplPreKey) <= sizeof(PipelineKey), "library key fits native job storage");
_Static_assert(sizeof(GplFragKey) <= sizeof(PipelineKey), "fragment key fits native job storage");

/* Queued jobs overwrite their active identity before publication.
 * Synchronous full jobs borrow their caller's key. */
static void pipeline_job_reset(LibJob *job)
{
    memset(job, 0, offsetof(LibJob, key));
    const size_t after_key = offsetof(LibJob, key) + sizeof(job->key);
    memset((char *)job + after_key, 0, sizeof(*job) - after_key);
}

/* Rebind create-info pointers after filling or moving a job. Queued vertex
 * descriptions are job-owned; synchronous ones may borrow their caller. */
static void lib_job_link(LibJob *job)
{
    bool pre = job->full || !job->frag;
    bool post = job->full || job->frag;
    job->ci.pNext = job->full ? NULL : &job->lib_info;
    job->ci.pStages = job->stages;
    job->vi.pVertexBindingDescriptions =
        job->vi.vertexBindingDescriptionCount ?
            job->vertex_key->binding_descriptions : NULL;
    job->vi.pVertexAttributeDescriptions =
        job->vi.vertexAttributeDescriptionCount ?
            job->vertex_key->attribute_descriptions : NULL;
    job->ci.pVertexInputState = pre ? &job->vi : NULL;
    job->ci.pInputAssemblyState = pre ? &job->ia : NULL;
    job->ci.pViewportState = pre ? &job->vp : NULL;
    job->ci.pRasterizationState = pre ? &job->raster : NULL;
    job->ci.pMultisampleState = post ? &job->ms : NULL;
    job->ci.pDepthStencilState = (post && job->ds.sType) ? &job->ds : NULL;
    job->cb.pAttachments = job->cb.attachmentCount ? &job->cba : NULL;
    job->ci.pColorBlendState = post ? &job->cb : NULL;
    job->dyn.pDynamicStates = job->dyn_states;
    job->ci.pDynamicState = &job->dyn;
}

/*
 * The pipeline worker ("vk-pipe"): one thread, one ring the render thread
 * alone writes and it alone reads, no lock, and the render thread never
 * waits for a job. It prepares upload-time libraries, seeded whole pipelines
 * and specializations whose draws use a ready dynamic sibling until the
 * specialized pipeline lands. The ring is a counter
 * pair: the writer publishes its count with a release store after writing
 * the slot, the reader takes it with an acquire load; the worker sleeps on
 * an eventcount when the ring is empty, and notifies another after each
 * job for the one wait there is, the renderer's shutdown, which cancels
 * what is queued and waits for the job under way alone.
 */
#define PUMP_RING 64
static LibJob pump_jobs[PUMP_RING];
static retro_atomic_int_t pump_w;  /* queued: render thread writes */
static retro_atomic_int_t pump_r;  /* compiled: worker writes */
static retro_atomic_int_t pump_cancel; /* skip what is not started */
static unsigned pump_w_local;      /* the render thread's copy of pump_w */
static unsigned pump_d;            /* drained, render thread only */
static retro_eventcount_t pipe_ec_work, pipe_ec_done;
static QemuThread pipe_thread;
static bool pipe_started;

static void *pipe_worker_fn(void *opaque)
{
    unsigned pr = 0;
    for (;;) {
        if ((unsigned)retro_atomic_load_acquire_int(&pump_w) != pr) {
            LibJob *job = &pump_jobs[pr % PUMP_RING];
            if (retro_atomic_load_acquire_int(&pump_cancel)) {
                job->result = VK_NULL_HANDLE;
            } else if (job->compute) {
                s3tc_pipeline_job_create(job->device,job->cache,&job->layout,
                                         &job->s3tc,&job->result);
            } else {
                VK_CHECK(vkCreateGraphicsPipelines(job->device, job->cache, 1,
                                                   &job->ci, NULL,
                                                   &job->result));
            }
            pr++;
            retro_atomic_store_release_int(&pump_r, (int)pr);
            retro_eventcount_notify(&pipe_ec_done);
            continue;
        }
        int key = retro_eventcount_prepare_wait(&pipe_ec_work);
        if ((unsigned)retro_atomic_load_acquire_int(&pump_w) != pr) {
            retro_eventcount_cancel_wait(&pipe_ec_work);
            continue;
        }
        retro_eventcount_commit_wait(&pipe_ec_work, key);
    }
    return NULL;
}

static void pipe_start(void)
{
    if (pipe_started) {
        return;
    }
    pipe_started = true;
    retro_eventcount_init(&pipe_ec_work);
    retro_eventcount_init(&pipe_ec_done);
    qemu_thread_create(&pipe_thread, "vk-pipe", pipe_worker_fn, NULL,
                       QEMU_THREAD_DETACHED);
}

/*
 * The libraries are kept for the renderer's life: relinking one is cheap,
 * compiling it is not, and a library a full table could not hold was never
 * destroyed nor found again.
 */
typedef struct GplLibrary {
    uint64_t hash;
    VkPipeline lib;
    uint8_t key[]; /* canonical metadata owned for this library's lifetime */
} GplLibrary;

static size_t gpl_key_size(PGRAPHVkState *r, bool frag)
{
    /* Dynamic vertex input leaves all arrays and trailing counts zero.
     * Retain only the distinguishing prefix on that immutable device path. */
    return frag ? sizeof(GplFragKey) : r->dyn_vertex_input ?
           offsetof(GplPreKey, bind) : sizeof(GplPreKey);
}

static GplLibrary *gpl_entry(GArray *libs, unsigned index)
{
    return (GplLibrary *)(libs->data +
                         (size_t)index * g_array_get_element_size(libs));
}

static void gpl_init(PGRAPHVkState *r)
{
    memset(r->gpl_lookup_cache, 0, sizeof(r->gpl_lookup_cache));
    size_t align = _Alignof(GplLibrary);
    size_t pre_size = (sizeof(GplLibrary) + gpl_key_size(r, false) + align - 1) & ~(align - 1);
    size_t frag_size = (sizeof(GplLibrary) + gpl_key_size(r, true) + align - 1) & ~(align - 1);
    r->gpl_pre = g_array_new(false, false, pre_size);
    r->gpl_frag = g_array_new(false, false, frag_size);
}

/* Pipeline jobs quiesce before this renderer-owned metadata is released. */
static void gpl_finalize(PGRAPHVkState *r)
{
    GArray *tables[] = { r->gpl_pre, r->gpl_frag };
    for (int t = 0; t < ARRAY_SIZE(tables); t++) {
        for (guint i = 0; i < tables[t]->len; i++) {
            vkDestroyPipeline(r->device, gpl_entry(tables[t], i)->lib, NULL);
        }
        g_array_free(tables[t], true);
    }
    r->gpl_pre = r->gpl_frag = NULL;
}

static VkPipeline gpl_lookup(PGRAPHVkState *r, bool frag, uint64_t hash,
                             const void *key, bool *found)
{
    GArray *libs = frag ? r->gpl_frag : r->gpl_pre;
    size_t key_size = gpl_key_size(r, frag);
    unsigned slot = (hash ^ (hash >> 32)) % ARRAY_SIZE(r->gpl_lookup_cache[0]);
    PipelineLibraryLookup *cached = &r->gpl_lookup_cache[frag][slot];
    /* Indices survive array relocation. A digest is only a filter, including
     * on this direct-index path: every hit verifies its complete identity. */
    if (cached->hash == hash && cached->index_plus_one &&
        cached->index_plus_one <= libs->len) {
        GplLibrary *e = gpl_entry(libs, cached->index_plus_one - 1);
        if (e->hash == hash && !memcmp(e->key, key, key_size)) {
            *found = true;
            return e->lib;
        }
    }
    for (guint i = 0; i < libs->len; i++) {
        GplLibrary *e = gpl_entry(libs, i);
        if (e->hash == hash && !memcmp(e->key, key, key_size)) {
            cached->hash = hash;
            cached->index_plus_one = i + 1;
            *found = true;
            return e->lib;
        }
    }
    *found = false;
    return VK_NULL_HANDLE;
}

static void gpl_store(PGRAPHVkState *r, bool frag, uint64_t hash,
                      const void *key, VkPipeline lib)
{
    GArray *libs = frag ? r->gpl_frag : r->gpl_pre;
    unsigned index = libs->len;
    /* Callers lend a stack key or final worker-slot key, never an entry
     * pointer that this growth could recycle. Snapshot metadata once into
     * its final cache owner; no shader/IR/resource payload is copied. */
    g_array_set_size(libs, index + 1);
    GplLibrary *entry = gpl_entry(libs, index);
    entry->hash = hash;
    entry->lib = lib;
    memcpy(entry->key, key, gpl_key_size(r, frag));
}

/* The worker's release publication ends its reads/writes of that slot.
 * Borrow its immutable key here and move the handle into the native cache,
 * and leave module references/counters to the ordinary ordered drain. */
static VkPipeline pump_take_library(PGRAPHVkState *r, bool frag, uint64_t hash,
                                    const void *key)
{
    if (!pipe_started) {
        return VK_NULL_HANDLE;
    }
    unsigned done = (unsigned)retro_atomic_load_acquire_int(&pump_r);
    for (unsigned i = pump_d; i != done; i++) {
        LibJob *job = &pump_jobs[i % PUMP_RING];
        const void *owned = frag ? (const void *)&job->frag_key : &job->pre_key;
        if (!job->compute && !job->full && job->frag == frag && job->result &&
            job->hash == hash && !memcmp(owned, key, gpl_key_size(r, frag))) {
            VkPipeline result = job->result;
            gpl_store(r, frag, hash, owned, result);
            job->result = VK_NULL_HANDLE;
            return result;
        }
    }
    return VK_NULL_HANDLE;
}

/* Identity of a pre-rasterization library: vertex/geometry modules, render
 * pass and rasterizer/input state not set dynamically. The pass matters: a
 * linked pipeline takes its attachments from its first library, so a colour
 * pass with a depth-only fragment half lacks blend state (RADV: NULL deref). */
static uint64_t gpl_pre_hash(PGRAPHVkState *r, uint64_t vsh,
                             uint64_t geom,
                             const RenderPassState *rp,
                             const VkPipelineRasterizationStateCreateInfo *raster,
                             const VkPipelineInputAssemblyStateCreateInfo *ia,
                             const VkPipelineVertexInputStateCreateInfo *vi, GplPreKey *k)
{
    memset(k, 0, sizeof(*k));
    k->vsh = vsh;
    k->geom = geom;
    k->rp = *rp;
    k->raster = *raster;
    k->raster.pNext = NULL;
    if (r->dyn_cull_front) {
        /* Set per draw, so they must not identify a library; the topology
         * class does, a dynamic topology cannot leave it. */
        k->raster.cullMode = 0;
        k->raster.frontFace = 0;
        k->ia.topology = topology_class(ia->topology);
    } else {
        k->ia = *ia;
        k->ia.pNext = NULL;
    }
    if (!r->dyn_vertex_input) {
        k->nb = vi->vertexBindingDescriptionCount;
        k->na = vi->vertexAttributeDescriptionCount;
        if (k->nb) {
            memcpy(k->bind, vi->pVertexBindingDescriptions,
                   k->nb * sizeof(k->bind[0]));
        }
        if (k->na) {
            memcpy(k->attr, vi->pVertexAttributeDescriptions,
                   k->na * sizeof(k->attr[0]));
        }
    }
    return xxh3_64((const uint8_t *)k, sizeof(*k), 0);
}

static VkColorComponentFlags color_write_mask(uint32_t control_0)
{
    VkColorComponentFlags write_mask = 0;
    if (control_0 & NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE)
        write_mask |= VK_COLOR_COMPONENT_R_BIT;
    if (control_0 & NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE)
        write_mask |= VK_COLOR_COMPONENT_G_BIT;
    if (control_0 & NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE)
        write_mask |= VK_COLOR_COMPONENT_B_BIT;
    if (control_0 & NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE)
        write_mask |= VK_COLOR_COMPONENT_A_BIT;
    return write_mask;
}

/* Push-constant range of a state's pipeline layout (vertex shader uniform
 * attrs). Libraries linked together need the same range, so it keys them. */
static uint32_t push_constant_size(PGRAPHVkState *r, const ShaderState *st)
{
    (void)st;
    /* Shader initialization enables this only when all 16 vec4 values fit.
     * One range keeps fragment libraries compatible across vertex masks;
     * command recording still uploads only the values the shader uses. */
    return r->use_push_constants_for_uniform_attrs ?
           NV2A_VERTEXSHADER_ATTRIBUTES * 4 * sizeof(float) : 0;
}

/* Identity of a fragment library: the fragment module, the render pass, the
 * push-constant size and the depth/blend state not set dynamically. */
static uint64_t gpl_frag_hash(PGRAPHVkState *r, uint64_t psh,
                              const VkPipelineDepthStencilStateCreateInfo *ds,
                              const VkPipelineColorBlendStateCreateInfo *cb,
                              const RenderPassState *rp, uint32_t push_size, GplFragKey *k)
{
    memset(k, 0, sizeof(*k));
    k->psh = psh;
    k->push_size = push_size;
    if (ds) {
        k->has_zeta = 1;
        if (!r->dyn_cull_front) {
            k->ds = *ds;
            k->ds.pNext = NULL;
        }
    }
    k->rp = *rp;
    k->attachments = cb->attachmentCount;
    if (k->attachments && cb->pAttachments && !r->dyn_blend) {
        k->blend = cb->pAttachments[0];
        k->blend_valid = 1;
    }
    return xxh3_64((const uint8_t *)k, sizeof(*k), 0);
}

/* The pre-rasterization library: the first num_pre_stages stages of full
 * (vertex, and geometry if any). Native module identities remain distinct
 * after a module dies and the driver reuses its handle. These library keys
 * are renderer-local; persistent shader/PSO key formats are unchanged. */
static VkPipeline gpl_get_pre(PGRAPHState *pg,
                              const VkGraphicsPipelineCreateInfo *full,
                              const RenderPassState *rp, VkPipelineLayout layout,
                              int num_pre_stages, const ShaderModuleInfo *vsh,
                              const ShaderModuleInfo *geom)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(full->pStages[0].module == vsh->module);
    assert((num_pre_stages > 1) == (geom != NULL));
    assert(!geom || full->pStages[1].module == geom->module);
    GplPreKey library_key;
    uint64_t hash = gpl_pre_hash(
        r, vsh->identity, geom ? geom->identity : 0,
        rp, full->pRasterizationState,
        full->pInputAssemblyState, full->pVertexInputState, &library_key);
    bool found;
    VkPipeline lib = gpl_lookup(r, false, hash, &library_key, &found);
    if (found) {
        return lib;
    }

    lib = pump_take_library(r, false, hash, &library_key);
    if (lib) {
        return lib;
    }

    VkGraphicsPipelineLibraryCreateInfoEXT lib_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
        .flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
                 VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT,
    };
    VkGraphicsPipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &lib_info,
        .flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR,
        .stageCount = num_pre_stages,
        .pStages = full->pStages,
        .pVertexInputState = full->pVertexInputState,
        .pInputAssemblyState = full->pInputAssemblyState,
        .pViewportState = full->pViewportState,
        .pRasterizationState = full->pRasterizationState,
        .pDynamicState = full->pDynamicState,
        .layout = layout,
        .renderPass = full->renderPass,
    };
    VK_CHECK(vkCreateGraphicsPipelines(r->device, r->vk_pipeline_cache, 1, &ci,
                                       NULL, &lib));
    gpl_store(r, false, hash, &library_key, lib);
    return lib;
}

/* The fragment half, compiled here when no draw has needed it yet: like
 * the pre-rasterization half, on the render thread, never waiting on
 * another thread. */
static VkPipeline gpl_get_frag(PGRAPHState *pg,
                               const VkGraphicsPipelineCreateInfo *full,
                               const PipelineKey *key, VkPipelineLayout layout,
                               int num_stages, const ShaderModuleInfo *psh)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(full->pStages[num_stages - 1].module == psh->module);
    GplFragKey library_key;
    uint64_t hash = gpl_frag_hash(r, psh->identity,
                                  full->pDepthStencilState,
                                  full->pColorBlendState,
                                  &key->render_pass_state,
                                  push_constant_size(r, &key->shader_state), &library_key);
    bool found;
    VkPipeline lib = gpl_lookup(r, true, hash, &library_key, &found);
    if (found) {
        return lib;
    }

    lib = pump_take_library(r, true, hash, &library_key);
    if (lib) {
        return lib;
    }

    VkGraphicsPipelineLibraryCreateInfoEXT lib_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
        .flags = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT |
                 VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT,
    };
    VkGraphicsPipelineCreateInfo ci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &lib_info,
        .flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR,
        .stageCount = 1,
        .pStages = &full->pStages[num_stages - 1],
        .pMultisampleState = full->pMultisampleState,
        .pDepthStencilState = full->pDepthStencilState,
        .pColorBlendState = full->pColorBlendState,
        .pDynamicState = full->pDynamicState,
        .layout = layout,
        .renderPass = full->renderPass,
    };
    VK_CHECK(vkCreateGraphicsPipelines(r->device, r->vk_pipeline_cache, 1, &ci,
                                       NULL, &lib));
    gpl_store(r, true, hash, &library_key, lib);
    return lib;
}

/* The ahead ring publishes complete pipelines into the render-thread cache.
 * Seed recipes and draw-discovered specializations share the same owners. */
static bool pump_full_in_flight(uint64_t key_hash, const PipelineKey *key)
{
    for (unsigned i = pump_d; i < pump_w_local; i++) {
        LibJob *j = &pump_jobs[i % PUMP_RING];
        if (!j->compute && j->full && j->hash == key_hash &&
            !memcmp(&j->key, key, sizeof(*key))) {
            return true;
        }
    }
    return false;
}

/* A whole pipeline the worker made goes into the pipeline cache; one the
 * draw path made meanwhile wins (the worker's was never bound: destroyed
 * at once). The state it was met as is then ready to be drawn with. */
static void pipeline_land(PGRAPHState *pg, LibJob *job)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    LruNode *node = lru_lookup(&r->pipeline_cache, job->hash, &job->key);
    PipelineBinding *snode = container_of(node, PipelineBinding, node);
    if (snode->pipeline == VK_NULL_HANDLE) {
        memcpy(&snode->key, &job->key, sizeof(job->key));
        snode->pipeline = job->result;
        snode->layout = job->layout;
        snode->render_pass = job->ci.renderPass;
        snode->has_dynamic_line_width = job->has_dynamic_line_width;
        snode->draw_time = pg->draw_time;
    } else {
        vkDestroyPipeline(r->device, job->result, NULL);
    }
    pgraph_vk_spec_pipeline_landed(job->state_hash);
}

/* A draw may have needed the library before its optional worker job landed.
 * Keep the already-published owner and retire the unused worker result. */
static void library_land(PGRAPHVkState *r, LibJob *job)
{
    bool found;
    const void *key = job->frag ? (const void *)&job->frag_key : &job->pre_key;
    gpl_lookup(r, job->frag, job->hash, key, &found);
    if (found) {
        vkDestroyPipeline(r->device, job->result, NULL);
    } else {
        gpl_store(r, job->frag, job->hash, key, job->result);
    }
}

/* Completion publication ends the worker's slot lend. Transfer only owned
 * native handles; a later ordered drain must not touch this result again. */
static void s3tc_pipeline_job_land(PGRAPHVkState *r, LibJob *job)
{
    if (job->s3tc.retired) { return; }
    assert(job->device==r->device && job->cache==r->vk_pipeline_cache);
    unsigned volume=job->s3tc.volume;
    assert(volume<2);
    r->s3tc_decode.pending_mask &= ~(1u<<volume);
    if (job->result) {
        assert(!r->s3tc_decode.pipelines[volume]);
        if (job->s3tc.owns_layout) {
            assert(!r->s3tc_decode.layout && !r->s3tc_decode.set_layout);
            r->s3tc_decode.layout=job->layout;
            r->s3tc_decode.set_layout=job->s3tc.created_set_layout;
            job->s3tc.created_set_layout=VK_NULL_HANDLE;
        } else {
            assert(job->layout==r->s3tc_decode.layout);
        }
        r->s3tc_decode.pipelines[volume]=job->result;
        job->result=VK_NULL_HANDLE;
    }
    job->s3tc.retired=true;
}

/* Land what the worker finished: libraries in the lookup tables, whole
 * pipelines in the pipeline cache. */
static void pump_drain(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!pipe_started) {
        return;
    }
    unsigned done = (unsigned)retro_atomic_load_acquire_int(&pump_r);
    while (pump_d != done) {
        LibJob *job = &pump_jobs[pump_d % PUMP_RING];
        if (job->compute) {
            s3tc_pipeline_job_land(r,job);
        } else if (job->result == VK_NULL_HANDLE) {
            /* Cancelled or already-transferred handles leave no result to
             * publish. Release their referenced modules below; layouts
             * remain renderer-owned. */
        } else if (job->full) {
            pipeline_land(pg, job);
        } else {
            library_land(r, job);
        }
        for (int i = 0; i < job->nmods; i++) {
            pgraph_vk_unref_shader_module(r, job->mods[i]);
        }
        pump_d++;
    }
}

/* The caller already owns the exact cache key. Transfer only a completed
 * handle; do not drain unrelated entries while current bindings are borrowed. */
static bool pump_take_pipeline(PGRAPHState *pg, PipelineBinding *binding,
                                uint64_t hash, const PipelineKey *key)
{
    if (!pipe_started) {
        return false;
    }
    unsigned done = (unsigned)retro_atomic_load_acquire_int(&pump_r);
    for (unsigned i = pump_d; i != done; i++) {
        LibJob *job = &pump_jobs[i % PUMP_RING];
        if (!job->compute && job->full && job->result && job->hash == hash &&
            !memcmp(&job->key, key, sizeof(*key))) {
            binding->pipeline = job->result;
            binding->layout = job->layout;
            binding->render_pass = job->ci.renderPass;
            binding->has_dynamic_line_width = job->has_dynamic_line_width;
            binding->draw_time = pg->draw_time;
            job->result = VK_NULL_HANDLE;
            pgraph_vk_spec_pipeline_landed(job->state_hash);
            return true;
        }
    }
    return false;
}

/* At the renderer's end: cancel queued jobs, wait for the one underway,
 * then release every remaining native owner through the ordinary drain. */
static void pump_quiesce(PGRAPHState *pg)
{
    if (!pipe_started) {
        return;
    }
    retro_atomic_store_release_int(&pump_cancel, 1);
    for (;;) {
        int key = retro_eventcount_prepare_wait(&pipe_ec_done);
        if ((unsigned)retro_atomic_load_acquire_int(&pump_r) ==
            pump_w_local) {
            retro_eventcount_cancel_wait(&pipe_ec_done);
            break;
        }
        retro_eventcount_commit_wait(&pipe_ec_done, key);
    }
    retro_atomic_store_release_int(&pump_cancel, 0);
    pump_drain(pg);
}

/* The next free slot of the ahead ring, NULL when it is full. */
static LibJob *pump_slot(void)
{
    if (pump_w_local - pump_d >= PUMP_RING) {
        return NULL;
    }
    LibJob *job = &pump_jobs[pump_w_local % PUMP_RING];
    pipeline_job_reset(job);
    return job;
}

static void pump_submit(LibJob *job)
{
    assert(job == &pump_jobs[pump_w_local % PUMP_RING]);
    /* A queued create never borrows the render thread's temporary key. */
    if (job->full) {
        job->vertex_key = &job->key;
    }
    if (!job->compute) { lib_job_link(job); } /* ring slots never move */
    pipe_start();
    pump_w_local++;
    retro_atomic_store_release_int(&pump_w, (int)pump_w_local);
    retro_eventcount_notify(&pipe_ec_work);
}

/* Only inspect published compute completions while texture/key lends may
 * be live. Do not drain unrelated graphics caches or recycle their slots. */
bool pgraph_vk_s3tc_pipeline_ready(PGRAPHState *pg, unsigned volume)
{
    PGRAPHVkState *r=pg->vk_renderer_state;
    assert(volume<2);
    if (!r->s3tc_decode.enabled) { return false; }
    if (r->s3tc_decode.pipelines[volume]) { return true; }
    /* Do not create an optional worker/thread stack on an upload. Normal
     * pipeline preparation starts it; CPU decode stays valid until then. */
    if (!pipe_started) { return false; }
    {
        unsigned done=(unsigned)retro_atomic_load_acquire_int(&pump_r);
        for (unsigned i=pump_d;i!=done;i++) {
            LibJob *job=&pump_jobs[i%PUMP_RING];
            if (job->compute) { s3tc_pipeline_job_land(r,job); }
        }
    }
    if (r->s3tc_decode.pipelines[volume]) { return true; }
    if ((r->s3tc_decode.pending_mask&(1u<<volume)) ||
        (!r->s3tc_decode.layout && r->s3tc_decode.pending_mask)) { return false; }
    LibJob *job=pump_slot();
    if (!job) { return false; } /* optional work; CPU decode remains ready */
    job->compute=true;
    job->s3tc=(S3tcPipelineJob){.volume=volume,.spirv_1_6=pgraph_vk_spirv_1_6,
                              .owns_layout=r->s3tc_decode.layout==VK_NULL_HANDLE};
    job->device=r->device;
    job->cache=r->vk_pipeline_cache;
    job->layout=r->s3tc_decode.layout;
    r->s3tc_decode.pending_mask|=1u<<volume;
    pump_submit(job);
    return false;
}

/* Explicit uncovered configuration: no existing graphics preparation worker.
 * Keep the previous correct GPU path rather than creating an optional thread
 * or making every upload decode on the CPU forever. This synchronous miss is
 * still a tracked draw-path violation; it is not nonblocking coverage. */
bool pgraph_vk_s3tc_pipeline_uncovered(PGRAPHState *pg, unsigned volume)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(volume < 2);
    if (pipe_started || !r->s3tc_decode.enabled) return false;
    if (r->s3tc_decode.pipelines[volume]) return true;
    S3tcPipelineJob job = {
        .volume = volume, .spirv_1_6 = pgraph_vk_spirv_1_6,
        .owns_layout = r->s3tc_decode.layout == VK_NULL_HANDLE
    };
    s3tc_pipeline_job_create(r->device, r->vk_pipeline_cache,
        &r->s3tc_decode.layout, &job, &r->s3tc_decode.pipelines[volume]);
    if (job.owns_layout) r->s3tc_decode.set_layout = job.created_set_layout;
    return true;
}

/* Teardown only: fixed layout/cache/device lends end after ordinary worker
 * cancellation/drain. Never call this from a draw or an upload. */
void pgraph_vk_quiesce_pipeline_worker(PGRAPHState *pg)
{
    pump_quiesce(pg);
}

/* Scoped render-thread lend of a published immutable key. The worker only
 * reads this identity; native draining/reuse ends the lend, so callers must
 * consume it before invoking any cache initializer or completion service. */
const ShaderState *pgraph_vk_pending_shader_state(PGRAPHState *pg,
                                                  uint64_t *hash)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    unsigned ticket = r->sibling_ticket;
    if (!r->sibling_retry || !pgraph_gpu_boost_gl() ||
        r->sibling_generation != pgraph_glsl_dynamic_gen ||
        ticket - pump_d >= pump_w_local - pump_d) {
        return NULL;
    }
    const LibJob *job = &pump_jobs[ticket % PUMP_RING];
    if (job->compute || !job->full) {
        return NULL;
    }
    *hash = job->state_hash;
    return &job->key.shader_state;
}

/* One compatible graphics layout per device generation. The clear layout
 * remains descriptor-free. Push-enabled devices already support all values. */
/* All graphics users borrow these immutable device-generation owners.
 * The descriptor set layout is created by shader initialization first. */
static void init_pipeline_layouts(PGRAPHVkState *r)
{
    VkPipelineLayoutCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VK_CHECK(vkCreatePipelineLayout(r->device, &info, NULL,
                                   &r->clear_pipeline_layout));
    info.setLayoutCount = 1;
    info.pSetLayouts = &r->descriptor_set_layout;
    unsigned index = r->use_push_constants_for_uniform_attrs ?
                     NV2A_VERTEXSHADER_ATTRIBUTES : 0;
    VkPushConstantRange range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = NV2A_VERTEXSHADER_ATTRIBUTES * 4 * sizeof(float),
    };
    info.pushConstantRangeCount = index ? 1 : 0;
    info.pPushConstantRanges = index ? &range : NULL;
    VK_CHECK(vkCreatePipelineLayout(r->device, &info, NULL,
                                   &r->graphics_pipeline_layouts[index]));
}

/* Called after worker quiescence, GPU slot settlement and pipeline/library
 * retirement, before the shader owner's descriptor layout is destroyed. */
static void finalize_pipeline_layouts(PGRAPHVkState *r)
{
    for (unsigned i = 0; i < ARRAY_SIZE(r->graphics_pipeline_layouts); i++) {
        if (r->graphics_pipeline_layouts[i]) {
            vkDestroyPipelineLayout(r->device, r->graphics_pipeline_layouts[i], NULL);
            r->graphics_pipeline_layouts[i] = VK_NULL_HANDLE;
        }
    }
    vkDestroyPipelineLayout(r->device, r->clear_pipeline_layout, NULL);
    r->clear_pipeline_layout = VK_NULL_HANDLE;
}

static VkPipelineLayout create_pipeline_layout_size(PGRAPHVkState *r,
                                                    uint32_t push_size)
{
    const unsigned stride = 4 * sizeof(float);
    assert(push_size % stride == 0);
    unsigned index = push_size / stride;
    assert(index < ARRAY_SIZE(r->graphics_pipeline_layouts));
    assert(r->graphics_pipeline_layouts[index] != VK_NULL_HANDLE);
    return r->graphics_pipeline_layouts[index];
}

static VkPipelineLayout create_pipeline_layout(PGRAPHVkState *r,
                                               const ShaderState *st)
{
    return create_pipeline_layout_size(r, push_constant_size(r, st));
}

/* Dynamic states of a library built ahead of its draw (pump, prewarm), the
 * same list create_pipeline gives the draw's own: the library key omits
 * it. */
static int ahead_dynamic_states(PGRAPHVkState *r, const ShaderState *st,
                                VkDynamicState *out)
{
    int n = fill_dynamic_states(r, out, true);
    if (dynamic_line_width(r, st)) {
        out[n++] = VK_DYNAMIC_STATE_LINE_WIDTH;
    }
    return n;
}

/* The pre-rasterization stages: vertex, then geometry if any. */
static int pre_stages(VkPipelineShaderStageCreateInfo *out,
                      const ShaderModuleInfo *vsh,
                      const ShaderModuleInfo *geom)
{
    int n = 0;
    out[n++] = (VkPipelineShaderStageCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vsh->module,
        .pName = "main",
    };
    if (geom) {
        out[n++] = (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_GEOMETRY_BIT,
            .module = geom->module,
            .pName = "main",
        };
    }
    return n;
}

/* Rasterizer and input assembly of a pre-rasterization library built ahead
 * of its draw: cull, front face and the topology within its class are
 * dynamic there, so only the polygon mode and the class are baked. */
static void ahead_pre_state(const ShaderState *st,
                            VkPipelineRasterizationStateCreateInfo *raster,
                            VkPipelineInputAssemblyStateCreateInfo *ia)
{
    *raster = (VkPipelineRasterizationStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_TRUE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = pgraph_polygon_mode_vk_map[st->geom.polygon_front_mode],
        .lineWidth = 1.0f,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .cullMode = VK_CULL_MODE_NONE,
        .depthBiasEnable = VK_FALSE,
    };
    *ia = (VkPipelineInputAssemblyStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology_class(topology_for_state(st)),
    };
}

/* Defined with the complete pipeline builder below. */
static int pump_state(PGRAPHState *pg, const ShaderState *st,
                      const RenderPassState *rp);

/* The pass left bound at a flip need not cover earlier scene/overlay draws.
 * Borrow one native owner per flip; copy only its format metadata before
 * job filling can touch the render-pass array. No owner escapes this call. */
static bool pump_next_render_pass(PGRAPHVkState *r, RenderPassState *rp)
{
    if (!r->render_passes->len) {
        return false;
    }
    if (r->pump.next_pass >= r->render_passes->len) {
        r->pump.next_pass = 0;
    }
    const RenderPass *pass = &g_array_index(r->render_passes, RenderPass,
                                           r->pump.next_pass++);
    *rp = pass->state;
    return true;
}

void pgraph_vk_libpump_step(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r || !r->device) {
        return;
    }
    /* Full PSOs use the worker even without GPL. Adopt their completed
     * results before seed eligibility checks, otherwise a queued state can
     * keep its dynamic sibling indefinitely. No worker means no drain. */
    if (pipe_started && pump_d != pump_w_local) {
        pump_drain(pg);
    }
    /* A seeded complete key is predictable only when all register and
     * vertex-input dimensions are dynamic. GPL is not required. */
    if (!g_config.perf.shader_seeding || !r->dyn_cull_front ||
        !r->dyn_blend || !r->dyn_vertex_input ||
        (!r->color_binding && !r->zeta_binding)) {
        return;
    }
    unsigned n;
    const ShaderState *states = pgraph_vk_seed_pump_states(&n);
    if (!n) {
        return;
    }
    RenderPassState rp;
    if (!pump_next_render_pass(r, &rp)) {
        return;
    }
    int idx = -1;
    for (unsigned i = 0; i < r->pump.n; i++) {
        if (!memcmp(&r->pump.rp[i], &rp, sizeof(rp))) {
            idx = i;
        }
    }
    if (idx < 0) {
        if (r->pump.n == ARRAY_SIZE(r->pump.rp)) {
            return;
        }
        idx = r->pump.n++;
        r->pump.rp[idx] = rp;
        r->pump.pos[idx] = 0;
        r->pump.rpos[idx] = 0;
        r->pump.retry[idx] = g_array_new(FALSE, FALSE, sizeof(unsigned));
    }
    unsigned *pos = &r->pump.pos[idx];
    GArray *retry = r->pump.retry[idx];
    /* Bounded work per flip: a few states (module creation takes the words,
     * compiles run on the worker), readiness checked on hashes made once. */
    if (r->pump.state_hash_base != states || r->pump.state_hash_n != n) {
        g_free(r->pump.state_hash);
        r->pump.state_hash = g_new(uint64_t, n);
        for (unsigned i = 0; i < n; i++) {
            r->pump.state_hash[i] = xxh3_64((const uint8_t *)&states[i],
                                              sizeof(states[i]), 0);
        }
        r->pump.state_hash_base = states;
        r->pump.state_hash_n = n;
    }
    const uint64_t *state_hash = r->pump.state_hash;
    unsigned budget = 4, checks = 32;
    while (budget && checks && *pos < n) {
        checks--;
        if (!pgraph_vk_seed_spv_ready_hash(state_hash[*pos])) {
            g_array_append_val(retry, *pos);
            (*pos)++;
            continue;
        }
        if (pump_state(pg, &states[*pos], &rp) < 0) {
            return;
        }
        (*pos)++;
        budget--;
    }
    if (*pos >= n && retry->len && budget && checks) {
        /* Round robin over the retry list, a bounded slice per flip; a
         * ready entry is pumped and swapped out. */
        unsigned *rpos = &r->pump.rpos[idx];
        while (retry->len && budget && checks) {
            if (*rpos >= retry->len) {
                *rpos = 0;
            }
            unsigned si = g_array_index(retry, unsigned, *rpos);
            checks--;
            if (!pgraph_vk_seed_spv_ready_hash(state_hash[si])) {
                (*rpos)++;
                continue;
            }
            if (pump_state(pg, &states[si], &rp) < 0) {
                break;
            }
            budget--;
            g_array_remove_index_fast(retry, *rpos);
        }
    }
}

/* Queue upload-time library work in its final native slot. No ShaderState,
 * vertex data or shader payload is copied into the queue: referenced modules
 * cover the worker's last use. A full queue defers optional preparation. */
bool pgraph_vk_pre_library_ready(PGRAPHState *pg, const VkPrewarmState *pipeline,
                                  ShaderModuleInfo *vsh, ShaderModuleInfo *geom)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    pump_drain(pg);
    LibJob *job = pump_slot();
    if (!job) {
        return false;
    }
    job->ci.stageCount = pre_stages(job->stages, vsh, geom);
    job->raster = pipeline->raster;
    job->ia = pipeline->ia;
    job->vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    job->hash = gpl_pre_hash(r, vsh->identity,
                             geom ? geom->identity : 0,
                             &pipeline->render_pass, &job->raster, &job->ia, &job->vi, &job->pre_key);
    bool found;
    gpl_lookup(r, false, job->hash, &job->pre_key, &found);
    if (!found) {
        for (unsigned i = pump_d; i < pump_w_local; i++) {
            LibJob *pending = &pump_jobs[i % PUMP_RING];
            if (!pending->compute && !pending->full && !pending->frag && pending->hash == job->hash &&
                !memcmp(&pending->pre_key, &job->pre_key, gpl_key_size(r, false))) {
                found = true;
                break;
            }
        }
    }
    if (found) {
        pgraph_vk_unref_shader_module(r, vsh);
        if (geom) {
            pgraph_vk_unref_shader_module(r, geom);
        }
        return true;
    }
    job->device = r->device;
    job->cache = r->vk_pipeline_cache;
    job->mods[job->nmods++] = vsh;
    if (geom) {
        job->mods[job->nmods++] = geom;
    }
    job->layout = create_pipeline_layout_size(r, pipeline->push_size);
    job->vp = (VkPipelineViewportStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    job->dyn = (VkPipelineDynamicStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = pipeline->num_dynamic,
    };
    assert(pipeline->num_dynamic <= ARRAY_SIZE(job->dyn_states));
    memcpy(job->dyn_states, pipeline->dynamic_states,
           pipeline->num_dynamic * sizeof(job->dyn_states[0]));
    job->lib_info = (VkGraphicsPipelineLibraryCreateInfoEXT){
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
        .flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT |
                 VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT,
    };
    job->ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    job->ci.flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
    job->ci.layout = job->layout;
    job->ci.renderPass = get_render_pass(r, (RenderPassState *)&pipeline->render_pass);
    pump_submit(job);
    return true;
}

/* Only the vertex side is known at upload time; fragment and attachment
 * state may still change before the draw. CPU compilation and library
 * creation are queued separately; an uncovered draw keeps its correct path. */
void pgraph_vk_prewarm_shaders(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!r || !r->pipeline_library || !r->dyn_cull_front ||
        !r->dyn_vertex_input || !r->device || !r->color_binding) {
        return;
    }

    int saved_prim = pg->primitive_mode;
    if (pg->primitive_mode == PRIM_TYPE_INVALID) {
        pg->primitive_mode = PRIM_TYPE_TRIANGLES;
    }
    /* Getting the state clears program_data_dirty, which the next draw needs
     * to bind the new program: kept as it was. */
    bool saved_program_dirty = pg->program_data_dirty;
    ShaderState state = pgraph_glsl_get_shader_state(pg);
    pg->program_data_dirty = saved_program_dirty;
    pg->primitive_mode = saved_prim;

    if (state.vsh.is_fixed_function) {
        return;
    }

    VkPrewarmState pipeline = {0};
    init_render_pass_state(pg, &pipeline.render_pass);
    ahead_pre_state(&state, &pipeline.raster, &pipeline.ia);
    pipeline.num_dynamic = ahead_dynamic_states(r, &state, pipeline.dynamic_states);
    pipeline.push_size = push_constant_size(r, &state);
    pgraph_vk_queue_pre_modules(pg, &state, &pipeline);
}

/* A whole pipeline's create info, for the shader state st (its modules
 * vsh, geom, psh) under the registers of the draw at hand, into job: the
 * draw path makes its own from it, the worker one for later. The caller
 * supplies cleared metadata (pump_slot or the synchronous job below);
 * queued keys are filled separately, while synchronous keys are borrowed. */
static void pipeline_job_fill(PGRAPHState *pg, const ShaderState *st,
                              const ShaderModuleInfo *vsh,
                              const ShaderModuleInfo *geom,
                              const ShaderModuleInfo *psh,
                              const PipelineKey *key, LibJob *job)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    job->full = true;
    job->device = r->device;
    job->cache = r->vk_pipeline_cache;
    uint32_t control_0 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0);
    bool depth_test = control_0 & NV_PGRAPH_CONTROL_0_ZENABLE;
    bool depth_write = !!(control_0 & NV_PGRAPH_CONTROL_0_ZWRITEENABLE);
    bool stencil_test =
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1) & NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;

    int num_active_shader_stages = 0;
    VkPipelineShaderStageCreateInfo *shader_stages = job->stages;

    shader_stages[num_active_shader_stages++] =
        (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vsh->module,
            .pName = "main",
        };
    if (geom) {
        shader_stages[num_active_shader_stages++] =
            (VkPipelineShaderStageCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_GEOMETRY_BIT,
                .module = geom->module,
                .pName = "main",
            };
    }
    shader_stages[num_active_shader_stages++] =
        (VkPipelineShaderStageCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = psh->module,
            .pName = "main",
        };

    /* The vertex input as the key holds it: nothing of it where the
     * device takes it dynamically. */
    job->vi = (VkPipelineVertexInputStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    if (!r->dyn_vertex_input) {
        job->vi.vertexBindingDescriptionCount =
            r->num_active_vertex_binding_descriptions;
        job->vi.vertexAttributeDescriptionCount =
            r->num_active_vertex_attribute_descriptions;
        job->vertex_key = key;
    }

    job->ia = (VkPipelineInputAssemblyStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = topology_for_state(st),
        .primitiveRestartEnable = VK_FALSE,
    };

    job->vp = (VkPipelineViewportStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    void *rasterizer_next_struct = NULL;

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_TRUE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode =
            pgraph_polygon_mode_vk_map[st->geom.polygon_front_mode],
        .lineWidth = 1.0f,
        .frontFace = (pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) &
                      NV_PGRAPH_SETUPRASTER_FRONTFACE) ?
                         VK_FRONT_FACE_COUNTER_CLOCKWISE :
                         VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .pNext = rasterizer_next_struct,
    };

    if (pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) & NV_PGRAPH_SETUPRASTER_CULLENABLE) {
        uint32_t cull_face = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER),
                                      NV_PGRAPH_SETUPRASTER_CULLCTRL);
        assert(cull_face < ARRAY_SIZE(pgraph_cull_face_vk_map));
        rasterizer.cullMode = pgraph_cull_face_vk_map[cull_face];
    } else {
        rasterizer.cullMode = VK_CULL_MODE_NONE;
    }
    job->raster = rasterizer;

    job->ms = (VkPipelineMultisampleStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .sampleShadingEnable = VK_FALSE,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthWriteEnable = depth_write ? VK_TRUE : VK_FALSE,
    };

    if (depth_test) {
        depth_stencil.depthTestEnable = VK_TRUE;
        uint32_t depth_func =
            GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0), NV_PGRAPH_CONTROL_0_ZFUNC);
        assert(depth_func < ARRAY_SIZE(pgraph_depth_func_vk_map));
        depth_stencil.depthCompareOp = pgraph_depth_func_vk_map[depth_func];
    }

    if (stencil_test) {
        depth_stencil.stencilTestEnable = VK_TRUE;
        uint32_t stencil_func = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1),
                                         NV_PGRAPH_CONTROL_1_STENCIL_FUNC);
        uint32_t stencil_ref = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1),
                                        NV_PGRAPH_CONTROL_1_STENCIL_REF);
        uint32_t mask_read = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1),
                                      NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ);
        uint32_t mask_write = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1),
                                       NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE);
        uint32_t op_fail = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2),
                                    NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL);
        uint32_t op_zfail = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2),
                                     NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL);
        uint32_t op_zpass = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2),
                                     NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS);

        assert(stencil_func < ARRAY_SIZE(pgraph_stencil_func_vk_map));
        assert(op_fail < ARRAY_SIZE(pgraph_stencil_op_vk_map));
        assert(op_zfail < ARRAY_SIZE(pgraph_stencil_op_vk_map));
        assert(op_zpass < ARRAY_SIZE(pgraph_stencil_op_vk_map));

        depth_stencil.front.failOp = pgraph_stencil_op_vk_map[op_fail];
        depth_stencil.front.passOp = pgraph_stencil_op_vk_map[op_zpass];
        depth_stencil.front.depthFailOp = pgraph_stencil_op_vk_map[op_zfail];
        depth_stencil.front.compareOp =
            pgraph_stencil_func_vk_map[stencil_func];
        depth_stencil.front.compareMask = mask_read;
        depth_stencil.front.writeMask = mask_write;
        depth_stencil.front.reference = stencil_ref;
        depth_stencil.back = depth_stencil.front;
    }
    if (key->render_pass_state.zeta_format != VK_FORMAT_UNDEFINED) {
        job->ds = depth_stencil;
    }

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = color_write_mask(control_0),
    };

    float blend_constant[4] = { 0, 0, 0, 0 };

    uint32_t blend = pgraph_surface_blend(pgraph_reg_r(pg, NV_PGRAPH_BLEND),
                                         pg->surface_shape.color_format);
    if (blend & NV_PGRAPH_BLEND_EN) {
        color_blend_attachment.blendEnable = VK_TRUE;

        uint32_t sfactor = GET_MASK(blend, NV_PGRAPH_BLEND_SFACTOR);
        uint32_t dfactor = GET_MASK(blend, NV_PGRAPH_BLEND_DFACTOR);
        assert(sfactor < ARRAY_SIZE(pgraph_blend_factor_vk_map));
        assert(dfactor < ARRAY_SIZE(pgraph_blend_factor_vk_map));
        color_blend_attachment.srcColorBlendFactor =
            pgraph_blend_factor_vk_map[sfactor];
        color_blend_attachment.dstColorBlendFactor =
            pgraph_blend_factor_vk_map[dfactor];
        color_blend_attachment.srcAlphaBlendFactor =
            pgraph_blend_factor_vk_map[sfactor];
        color_blend_attachment.dstAlphaBlendFactor =
            pgraph_blend_factor_vk_map[dfactor];

        uint32_t equation =
            GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_BLEND), NV_PGRAPH_BLEND_EQN);
        assert(equation < ARRAY_SIZE(pgraph_blend_equation_vk_map));

        color_blend_attachment.colorBlendOp =
            pgraph_blend_equation_vk_map[equation];
        color_blend_attachment.alphaBlendOp =
            pgraph_blend_equation_vk_map[equation];

        uint32_t blend_color = pgraph_reg_r(pg, NV_PGRAPH_BLENDCOLOR);
        pgraph_argb_pack32_to_rgba_float(blend_color, blend_constant);
    }

    job->cba = color_blend_attachment;
    job->cb = (VkPipelineColorBlendStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = key->render_pass_state.color_format != VK_FORMAT_UNDEFINED,
        .blendConstants[0] = blend_constant[0],
        .blendConstants[1] = blend_constant[1],
        .blendConstants[2] = blend_constant[2],
        .blendConstants[3] = blend_constant[3],
    };

    int num_dynamic_states = fill_dynamic_states(r, job->dyn_states, true);

    job->has_dynamic_line_width = dynamic_line_width(r, st);
    if (job->has_dynamic_line_width) {
        job->dyn_states[num_dynamic_states++] = VK_DYNAMIC_STATE_LINE_WIDTH;
    }

    job->dyn = (VkPipelineDynamicStateCreateInfo){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = num_dynamic_states,
    };

    // FIXME: Dither
    // if (pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0) &
    //         NV_PGRAPH_CONTROL_0_DITHERENABLE))
    // FIXME: point size
    // FIXME: Edge Antialiasing
    // bool anti_aliasing = GET_MASK(pgraph_reg_r(pg, NV_PGRAPH_ANTIALIASING),
    // NV_PGRAPH_ANTIALIASING_ENABLE);
    // if (!anti_aliasing && pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) &
    //                           NV_PGRAPH_SETUPRASTER_LINESMOOTHENABLE) {
    // FIXME: VK_EXT_line_rasterization
    // }

    // if (!anti_aliasing && pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER) &
    //                           NV_PGRAPH_SETUPRASTER_POLYSMOOTHENABLE) {
    // FIXME: No direct analog. Just do it with MSAA.
    // }


    job->layout = create_pipeline_layout(r, st);
    job->ci = (VkGraphicsPipelineCreateInfo){
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = num_active_shader_stages,
        .layout = job->layout,
        .renderPass = get_render_pass(r, (RenderPassState *)&key->render_pass_state),
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
    };
    lib_job_link(job);
}

static bool pipeline_ready(PGRAPHVkState *r, uint64_t hash,
                           const PipelineKey *key)
{
    unsigned bin = lru_hash_to_bin(&r->pipeline_cache, hash);
    LruNode *node;
    QTAILQ_FOREACH(node, &r->pipeline_cache.bins[bin], next_bin) {
        if (node->hash == hash) {
            PipelineBinding *binding = container_of(node, PipelineBinding, node);
            if (binding->pipeline != VK_NULL_HANDLE &&
                !memcmp(&binding->key, key, sizeof(*key))) {
                return true;
            }
        }
    }
    return false;
}

/* Fully dynamic seeded PSOs use the same exact key as a draw. Snapshot
 * the shader-state identity once into final slot storage: the bank can be
 * released after shutdown drains the jobs, and no temporary key/word buffer
 * is handed off. Module references cover the last worker consumer. */
static int pump_state(PGRAPHState *pg, const ShaderState *st,
                      const RenderPassState *rp)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(r->dyn_cull_front && r->dyn_blend && r->dyn_vertex_input);
    LibJob *job = pump_slot();
    if (!job) {
        return -1;
    }
    PipelineKey *key = &job->key;
    memset(key, 0, offsetof(PipelineKey, shader_state));
    const size_t after_shader = offsetof(PipelineKey, shader_state) +
                                sizeof(key->shader_state);
    memset((char *)key + after_shader, 0, sizeof(*key) - after_shader);
    memcpy(&key->shader_state, st, sizeof(key->shader_state));
    key->render_pass_state = *rp;
    uint64_t hash = xxh3_64((const uint8_t *)key, sizeof(*key), 0);
    if (pipeline_ready(r, hash, key) || pump_full_in_flight(hash, key)) {
        return 0;
    }
    ShaderModuleInfo *geom, *vsh, *psh;
    pgraph_vk_ref_state_modules(r, &key->shader_state, &geom, &vsh, &psh);
    pipeline_job_fill(pg, &key->shader_state, vsh, geom, psh, key, job);
    job->hash = hash;
    job->state_hash = xxh3_64((const uint8_t *)&key->shader_state,
                                sizeof(key->shader_state), 0);
    job->mods[job->nmods++] = vsh;
    if (geom) {
        job->mods[job->nmods++] = geom;
    }
    job->mods[job->nmods++] = psh;
    pump_submit(job);
    pgraph_vk_spec_pipeline_queued(job->state_hash);
    return 1;
}

/* A state met for the first time draws with its dynamic sibling while the
 * worker makes its pipeline for the draw's state - the key the sibling
 * draws with, its shaders in place of the sibling's - into the pipeline
 * cache, where its next meeting finds it. */
static void pipeline_ahead(PGRAPHState *pg, const PipelineKey *draw_key)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!r->ahead_valid) {
        return;
    }
    r->ahead_valid = false;
    pump_drain(pg);
    LibJob *job = pump_slot();
    if (!job) {
        return; /* the ring is full: the next meeting queues it */
    }
    /* Build the final immutable key in its reserved slot. The fill below
     * does not clear the slot; release publication happens in pump_submit. */
    PipelineKey *key = &job->key;
    /* The speculative shader replaces the draw shader. Copy only the
     * surrounding bytes from the draw key, including all padding. */
    memcpy(key, draw_key, offsetof(PipelineKey, shader_state));
    const size_t after_shader = offsetof(PipelineKey, shader_state) +
                                sizeof(key->shader_state);
    memcpy((char *)key + after_shader, (const char *)draw_key + after_shader,
           sizeof(*key) - after_shader);
    memcpy(&key->shader_state, &r->ahead_state, sizeof(ShaderState));
    uint64_t hash = xxh3_64((const uint8_t *)key, sizeof(*key), 0);
    if (pipeline_ready(r, hash, key)) {
        pgraph_vk_spec_pipeline_landed(r->ahead_hash);
        return;
    }
    if (pump_full_in_flight(hash, key)) {
        /* Another draw already queued this PSO. It is not ready: keep the
         * correct sibling bound until pump_drain lands the actual result.
         * Premature PIPE_LANDED makes the next draw adopt specialization
         * and synchronously compile a duplicate while its worker runs. */
        pgraph_vk_spec_pipeline_queued(r->ahead_hash);
        return;
    }
    ShaderModuleInfo *geom, *vsh, *psh;
    pgraph_vk_ref_state_modules(r, &r->ahead_state, &geom, &vsh, &psh);
    pipeline_job_fill(pg, &r->ahead_state, vsh, geom, psh, key, job);
    job->hash = hash;
    job->state_hash = r->ahead_hash;
    job->mods[job->nmods++] = vsh;
    if (geom) {
        job->mods[job->nmods++] = geom;
    }
    job->mods[job->nmods++] = psh;
    pump_submit(job);
    pgraph_vk_spec_pipeline_queued(r->ahead_hash);
}

/* A shader-level LANDED/seen marker does not prove a PSO for this render
 * pass and static draw state. Reuse a fully ready exact sibling when one
 * exists; only resident modules may fill its binding, without compilation. */
static bool pipeline_use_ready_sibling(PGRAPHState *pg, PipelineKey *key,
                                        uint64_t full_hash)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!pgraph_gpu_boost_gl() || key->shader_state.psh.combiner_dynamic) {
        return false;
    }
    /* Project only fragment metadata in the existing draw key. The vertex
     * program is never cloned for this lookup. Restore every byte on miss. */
    PshState original;
    memcpy(&original, &key->shader_state.psh, sizeof(original));
    if (!pgraph_glsl_psh_canonicalize_dynamic(&key->shader_state.psh)) {
        memcpy(&key->shader_state.psh, &original, sizeof(original));
        return false;
    }
    uint64_t shader_hash = xxh3_64((const uint8_t *)&key->shader_state,
                                     sizeof(key->shader_state), 0);
    uint64_t sibling_hash = xxh3_64((const uint8_t *)key, sizeof(*key), 0);
    ShaderBinding *sibling = NULL;
    if (pipeline_ready(r, sibling_hash, key)) {
        sibling = pgraph_vk_materialize_ready_shader_binding(r,
                                        &key->shader_state, shader_hash);
    }
    bool ready = sibling != NULL;
    PshState projected;
    if (ready) {
        memcpy(&projected, &key->shader_state.psh, sizeof(projected));
    }
    memcpy(&key->shader_state.psh, &original, sizeof(original));
    if (!ready) {
        return false;
    }

    /* No cache mutation or draining while the binding is borrowed. Full
     * jobs pin native modules and snapshot the exact key once into their
     * final slot; cache nodes may be recycled before worker completion. */
    if (!pump_full_in_flight(full_hash, key)) {
        LibJob *job = pump_slot();
        if (job) {
            ShaderBinding *full = r->shader_binding;
            memcpy(&job->key, key, sizeof(job->key));
            ShaderModuleInfo *vsh = full->vsh.module_info;
            ShaderModuleInfo *geom = full->geom.module_info;
            ShaderModuleInfo *psh = full->psh.module_info;
            pgraph_vk_ref_shader_module(vsh);
            if (geom) {
                pgraph_vk_ref_shader_module(geom);
            }
            pgraph_vk_ref_shader_module(psh);
            pipeline_job_fill(pg, &job->key.shader_state, vsh, geom, psh,
                               &job->key, job);
            job->hash = full_hash;
            job->state_hash = full->node.hash;
            job->mods[job->nmods++] = vsh;
            if (geom) {
                job->mods[job->nmods++] = geom;
            }
            job->mods[job->nmods++] = psh;
            pump_submit(job);
            pgraph_vk_spec_pipeline_queued(job->state_hash);
            r->sibling_ticket = pump_w_local - 1;
            r->sibling_generation = pgraph_glsl_dynamic_gen;
            r->sibling_retry = true;
        }
    }
    memcpy(&key->shader_state.psh, &projected, sizeof(projected));
    pgraph_vk_use_ready_shader_binding(pg, sibling);
    /* A published job owns a key the next draw can borrow before draining.
     * Otherwise preserve the existing full-queue/in-flight retry capture. */
    if (!r->sibling_retry) {
        pg->program_data_dirty = true;
    }
    /* Proven-hit promotion cannot evict a borrowed ready pipeline. */
    LruNode *node = lru_lookup(&r->pipeline_cache, sibling_hash, key);
    PipelineBinding *binding = container_of(node, PipelineBinding, node);
    assert(binding->pipeline != VK_NULL_HANDLE);
    r->pipeline_binding_changed = r->pipeline_binding != binding;
    r->pipeline_binding = binding;
    return true;
}

static void create_pipeline(PGRAPHState *pg)
{
    NV2A_VK_DGROUP_BEGIN("Creating pipeline");

    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;

    pgraph_vk_bind_textures(d);
    pgraph_vk_bind_shaders(pg);

    // FIXME: If nothing was dirty, don't even try creating the key or hashing.
    //        Just use the same pipeline.
    bool pipeline_dirty = check_pipeline_dirty(pg);

    pgraph_clear_dirty_reg_map(pg);
    // FIXME: We could clear less

    if (r->pipeline_binding && !pipeline_dirty && !r->ahead_valid) {
        NV2A_VK_DPRINTF("Cache hit");
        NV2A_VK_DGROUP_END();
        return;
    }

    PipelineKey key;
    init_pipeline_key(pg, &key);
    pipeline_ahead(pg, &key);
    uint64_t hash = xxh3_64((void *)&key, sizeof(key), 0);

    LruNode *node = lru_lookup(&r->pipeline_cache, hash, &key);
    PipelineBinding *snode = container_of(node, PipelineBinding, node);
    if (snode->pipeline != VK_NULL_HANDLE) {
        NV2A_VK_DPRINTF("Cache hit");
        r->sibling_retry = false;
        r->pipeline_binding_changed = r->pipeline_binding != snode;
        r->pipeline_binding = snode;
        NV2A_VK_DGROUP_END();
        return;
    }

    memcpy(&snode->key, &key, sizeof(key));
    if (pump_take_pipeline(pg, snode, hash, &snode->key)) {
        r->sibling_retry = false;
        r->pipeline_binding_changed = r->pipeline_binding != snode;
        r->pipeline_binding = snode;
        NV2A_VK_DGROUP_END();
        return;
    }
    if (pipeline_use_ready_sibling(pg, &key, hash)) {
        NV2A_VK_DGROUP_END();
        return;
    }
    NV2A_VK_DPRINTF("Cache miss");
    nv2a_profile_inc_counter(NV2A_PROF_PIPELINE_GEN);

    /* The job holds the create info; it never leaves this thread. */
    static LibJob job;
    pipeline_job_reset(&job);
    pipeline_job_fill(pg, &r->shader_binding->state,
                      r->shader_binding->vsh.module_info,
                      r->shader_binding->geom.module_info,
                      r->shader_binding->psh.module_info, &key, &job);
    snode->has_dynamic_line_width = job.has_dynamic_line_width;
    VkPipelineLayout layout = job.layout;

    VkPipeline pipeline;
    if (r->pipeline_library) {
        int num_active_shader_stages = job.ci.stageCount;
        /* The fragment stage is the last one. */
        VkPipeline pre = gpl_get_pre(pg, &job.ci, &key.render_pass_state, layout,
                                     num_active_shader_stages - 1,
                                     r->shader_binding->vsh.module_info,
                                     r->shader_binding->geom.module_info);
        VkPipeline frag = gpl_get_frag(pg, &job.ci, &key, layout,
                                       num_active_shader_stages,
                                       r->shader_binding->psh.module_info);
        VkPipeline libs[2] = { pre, frag };
        VkPipelineLibraryCreateInfoKHR link_info = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR,
            .libraryCount = 2,
            .pLibraries = libs,
        };
        VkGraphicsPipelineCreateInfo link = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .pNext = &link_info,
            .layout = layout,
            .renderPass = job.ci.renderPass,
            .basePipelineHandle = VK_NULL_HANDLE,
        };
        VK_CHECK(vkCreateGraphicsPipelines(r->device, r->vk_pipeline_cache,
                                           1, &link, NULL, &pipeline));
    } else {
        VK_CHECK(vkCreateGraphicsPipelines(r->device, r->vk_pipeline_cache, 1,
                                           &job.ci, NULL, &pipeline));
    }

    snode->pipeline = pipeline;
    snode->layout = layout;
    snode->render_pass = job.ci.renderPass;
    snode->draw_time = pg->draw_time;

    r->pipeline_binding = snode;
    r->pipeline_binding_changed = true;

    NV2A_VK_DGROUP_END();
}

static void push_vertex_attr_values(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!r->use_push_constants_for_uniform_attrs) {
        return;
    }

    // FIXME: Partial updates

    float values[NV2A_VERTEXSHADER_ATTRIBUTES][4];
    int num_uniform_attrs = 0;

    pgraph_get_inline_values(pg, r->shader_binding->state.vsh.uniform_attrs,
                             values, &num_uniform_attrs);

    if (num_uniform_attrs > 0) {
        vkCmdPushConstants(r->command_buffer, r->pipeline_binding->layout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0,
                           num_uniform_attrs * 4 * sizeof(float),
                           &values);
    }
}

static void bind_descriptor_sets(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(r->descriptor_set_index >= r->descriptor_set_base + 1);
    /* Vulkan orders dynamic offsets by binding number: VS, then PS. */
    assert(VSH_UBO_BINDING < PSH_UBO_BINDING);
    assert(r->uniform_buffer_offsets[0] <= UINT32_MAX);
    assert(r->uniform_buffer_offsets[1] <= UINT32_MAX);
    uint32_t offsets[] = { r->uniform_buffer_offsets[0],
                           r->uniform_buffer_offsets[1] };

    vkCmdBindDescriptorSets(r->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            r->pipeline_binding->layout, 0, 1,
                            &r->descriptor_sets[r->descriptor_set_index - 1],
                            ARRAY_SIZE(offsets), offsets);
}

static void begin_query(PGRAPHVkState *r)
{
    assert(r->in_command_buffer);
    assert(r->host_query_reset ? r->in_render_pass : !r->in_render_pass);
    assert(!r->query_in_flight);

    // FIXME: We should handle this. Make the query buffer bigger, but at least
    // flush current queries.
    assert(r->num_queries_in_flight < r->max_queries_in_flight);

    nv2a_profile_inc_counter(NV2A_PROF_QUERY);
    /* Reset on the host when possible: a reset recorded in the command buffer
     * has not run yet when the report path reads the recycled slot, which
     * then returns its previous result as available (MEASURED). */
    if (r->host_query_reset) {
        vkResetQueryPool(r->device, r->query_pool, r->num_queries_in_flight, 1);
    } else {
        vkCmdResetQueryPool(r->command_buffer, r->query_pool,
                            r->num_queries_in_flight, 1);
    }
    vkCmdBeginQuery(r->command_buffer, r->query_pool, r->num_queries_in_flight,
                    VK_QUERY_CONTROL_PRECISE_BIT);

    r->query_in_flight = true;
    r->query_in_render_pass = r->in_render_pass;
    r->new_query_needed = false;
    r->num_queries_in_flight++;
}

static void end_query(PGRAPHVkState *r)
{
    assert(r->in_command_buffer);
    assert(r->query_in_flight);
    assert(r->query_in_render_pass == r->in_render_pass);

    vkCmdEndQuery(r->command_buffer, r->query_pool,
                  r->num_queries_in_flight - 1);
    r->query_in_flight = false;
    r->query_in_render_pass = false;
}

static void sync_staging_buffers(PGRAPHState *pg, VkCommandBuffer cmd)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    VkBufferMemoryBarrier barriers[ARRAY_SIZE(pgraph_vk_staged_buffers)];
    unsigned count = 0;
    VkPipelineStageFlags stages = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(pgraph_vk_staged_buffers); i++) {
        int index_src = pgraph_vk_staged_buffers[i].staging;
        int index_dst = pgraph_vk_staged_buffers[i].device;
        StorageBuffer *b_src = &r->storage_buffers[index_src];
        StorageBuffer *b_dst = &r->storage_buffers[index_dst];

        if (!b_src->buffer_offset) {
            continue;
        }

        if (!(b_src->properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VK_CHECK(xemu_vk_flush_allocation(r->allocator, b_src->allocation, 0,
                                        b_src->buffer_offset));
        }
        VkBufferCopy copy_region = { .size = b_src->buffer_offset };
        vkCmdCopyBuffer(cmd, b_src->buffer, b_dst->buffer, 1, &copy_region);

        VkAccessFlags dst_access_mask;
        VkPipelineStageFlags dst_stage_mask;

        switch (index_dst) {
        case BUFFER_INDEX:
            dst_access_mask = VK_ACCESS_INDEX_READ_BIT;
            dst_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            break;
        case BUFFER_VERTEX_INLINE:
            dst_access_mask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            dst_stage_mask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            break;
        case BUFFER_UNIFORM:
            dst_access_mask = VK_ACCESS_UNIFORM_READ_BIT;
            dst_stage_mask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            break;
        default:
            assert(0);
            break;
        }

        barriers[count++] = (VkBufferMemoryBarrier) {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = dst_access_mask,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = b_dst->buffer,
            .size = b_src->buffer_offset
        };
        stages |= dst_stage_mask;

        b_src->buffer_offset = 0;
    }
    if (count) {
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, stages, 0,
                             0, NULL, count, barriers, 0, NULL);
    }
}

/* Host writes and non-coherent flushes finish before vkQueueSubmit, whose
 * host-write domain operation supplies their device visibility:
 * https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-submission-host-writes */
static void flush_memory_buffer(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (r->vertex_ram_dirty.size) {
        if (!r->vertex_ram_coherent) {
            VK_CHECK(xemu_vk_flush_allocation(
                r->allocator, r->storage_buffers[BUFFER_VERTEX_RAM].allocation,
                r->vertex_ram_dirty.addr, r->vertex_ram_dirty.size));
        }
        r->vertex_ram_dirty.size = 0;
    }
    for (int i = 0; i < 2; i++) {
        VkDeviceSize start = r->vertex_arena_dirty_start[i];
        if (start != UINT64_MAX) {
            VkDeviceSize end = r->vertex_arena[i].buffer_offset;
            assert(start < end);
            if (!r->vertex_arena_coherent[i]) {
                VK_CHECK(xemu_vk_flush_allocation(r->allocator,
                                            r->vertex_arena[i].allocation,
                                            start, end - start));
            }
            r->vertex_arena_dirty_start[i] = UINT64_MAX;
        }
    }
}

static void begin_render_pass(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(r->in_command_buffer);
    assert(!r->in_render_pass);

    nv2a_profile_inc_counter(NV2A_PROF_PIPELINE_RENDERPASSES);

    unsigned int vp_width = pg->surface_binding_dim.width,
                 vp_height = pg->surface_binding_dim.height;
    pgraph_apply_scaling_factor(pg, &vp_width, &vp_height);

    assert(r->framebuffer != VK_NULL_HANDLE);

    VkRenderPassBeginInfo render_pass_begin_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = r->render_pass,
        .framebuffer = r->framebuffer,
        .renderArea.extent.width = vp_width,
        .renderArea.extent.height = vp_height,
        .clearValueCount = 0,
        .pClearValues = NULL,
    };
    vkCmdBeginRenderPass(r->command_buffer, &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);
    r->in_render_pass = true;

}

/* End the segment's queries inside the current render pass instance: a
 * query begun inside one must end inside it. */
static void vk_cost_close_recorded_queries(PGRAPHVkState *r)
{
    if (!r->cost_recording) {
        return;
    }
    vkCmdEndQuery(r->command_buffer, r->cost_stats_pool,
                  r->cost_recording_slot);
    r->cost_seg_recorded[r->cost_recording_slot] = true;
    if (r->cost_recording_samples) {
        vkCmdEndQuery(r->command_buffer, r->cost_occl_pool,
                      r->cost_recording_slot);
        r->cost_seg_recorded_samples[r->cost_recording_slot] = true;
        r->cost_recording_samples = false;
    }
    r->cost_recording = false;
}

static void end_render_pass(PGRAPHVkState *r)
{
    if (r->in_render_pass) {
        /* A query begun here cannot span the next pass. Outside-pass
         * queries on devices without host reset retain their old scope. */
        if (r->query_in_flight && r->query_in_render_pass) {
            end_query(r);
        }
        /* Cost queries cannot outlive the render pass: close them with the
         * segment; the next draw opens a new one. */
        if (r->cost_recording) {
            vk_cost_close_recorded_queries(r);
            xemu_cost_backend_interrupt();
        }
        vkCmdEndRenderPass(r->command_buffer);
        r->in_render_pass = false;
    }
}

/* Cost-model fragment counting, VK backend (core in pgraph/cost.c). The
 * begin is deferred from SET_BEGIN_END, where no command buffer is sure to
 * exist, to begin_draw: recorded inside the render pass, right before the
 * draw it covers. The queries end inside the same render pass instance; a
 * begin that meets no draw is never recorded and reads zero, as on GL. */
static bool vk_cost_probe_active(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (!xemu_cost_model_active()) {
        return false;
    }
    if (r->cost_stats_pool == VK_NULL_HANDLE) {
        static bool warned;
        if (!warned) {
            warned = true;
            fprintf(stderr, "xemu: real-hw-speed: "
                    "pipelineStatisticsQuery missing -- "
                    "cost model running without fragments (degraded)\n");
        }
        return false;
    }
    return true;
}

static bool vk_cost_seg_begin(NV2AState *d, int slot, bool want_samples)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;
    r->cost_pending_slot = slot;
    r->cost_seg_recorded[slot] = false;
    r->cost_seg_recorded_samples[slot] = false;
    /* Decided now; if a game occlusion query is open when the begin is
     * recorded, vk_cost_materialize marks the samples interrupted. */
    r->cost_pending_samples = want_samples && !r->query_in_flight;
    return r->cost_pending_samples;
}

static void vk_cost_seg_end(NV2AState *d)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;
    r->cost_pending_slot = -1; /* cancel an unmet begin: slot reads zero */
    if (!r->cost_recording) {
        return;
    }
    assert(r->in_command_buffer);
    /* Recording implies the render pass the queries began in is still
     * the current instance (end_render_pass closes them first). */
    vk_cost_close_recorded_queries(r);
}

/* Begin the pending segment's queries inside the render pass, so segments
 * never split one; their resets were recorded at command buffer begin. */
static void vk_cost_materialize(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (pg->clearing || !r->cost_pools_reset) {
        return;
    }
    /* A mid-recording finish closed the segment and cancelled its begin, but
     * the draw that caused it still runs: open a segment for it. */
    if (r->cost_pending_slot < 0 && !xemu_cost_q_open &&
        vk_cost_probe_active(pg)) {
        xemu_cost_draw_begin(container_of(pg, NV2AState, pgraph),
                             &pgraph_vk_cost_ops);
    }
    int slot = r->cost_pending_slot;
    if (slot < 0) {
        return;
    }
    assert(r->in_render_pass);
    r->cost_pending_slot = -1;
    vkCmdBeginQuery(r->command_buffer, r->cost_stats_pool, slot, 0);
    r->cost_recording = true;
    r->cost_recording_slot = slot;
    if (r->cost_pending_samples) {
        if (!r->query_in_flight) {
            vkCmdBeginQuery(r->command_buffer, r->cost_occl_pool, slot,
                            VK_QUERY_CONTROL_PRECISE_BIT);
            r->cost_recording_samples = true;
        } else {
            xemu_cost_samples_interrupted();
        }
        r->cost_pending_samples = false;
    }
}

static void vk_cost_seg_read(NV2AState *d, int slot, uint64_t *invocations,
                             uint64_t *samples)
{
    PGRAPHVkState *r = d->pgraph.vk_renderer_state;
    assert(!r->in_command_buffer);
    *invocations = 0;
    if (r->cost_seg_recorded[slot]) {
        VK_CHECK(vkGetQueryPoolResults(
            r->device, r->cost_stats_pool, slot, 1, sizeof(uint64_t),
            invocations, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        r->cost_seg_recorded[slot] = false;
    }
    if (samples) {
        *samples = 0;
        if (r->cost_seg_recorded_samples[slot]) {
            VK_CHECK(vkGetQueryPoolResults(
                r->device, r->cost_occl_pool, slot, 1, sizeof(uint64_t),
                samples, sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
            r->cost_seg_recorded_samples[slot] = false;
        }
    }
}

const XemuCostQueryOps pgraph_vk_cost_ops = {
    .seg_begin = vk_cost_seg_begin,
    .seg_end = vk_cost_seg_end,
    .seg_read = vk_cost_seg_read,
};

static void teardown_after_settle(PGRAPHState *pg);

const enum NV2A_PROF_COUNTERS_ENUM finish_reason_to_counter_enum[] = {
    [VK_FINISH_REASON_VERTEX_BUFFER_DIRTY] = NV2A_PROF_FINISH_VERTEX_BUFFER_DIRTY,
    [VK_FINISH_REASON_SURFACE_CREATE] = NV2A_PROF_FINISH_SURFACE_CREATE,
    [VK_FINISH_REASON_SURFACE_DOWN] = NV2A_PROF_FINISH_SURFACE_DOWN,
    [VK_FINISH_REASON_NEED_BUFFER_SPACE] = NV2A_PROF_FINISH_NEED_BUFFER_SPACE,
    [VK_FINISH_REASON_FRAMEBUFFER_DIRTY] = NV2A_PROF_FINISH_FRAMEBUFFER_DIRTY,
    [VK_FINISH_REASON_PRESENTING] = NV2A_PROF_FINISH_PRESENTING,
    [VK_FINISH_REASON_FLIP_STALL] = NV2A_PROF_FINISH_FLIP_STALL,
    [VK_FINISH_REASON_FLUSH] = NV2A_PROF_FINISH_FLUSH,
    [VK_FINISH_REASON_STALLED] = NV2A_PROF_FINISH_STALLED,
    [VK_FINISH_REASON_SURFACE_UP] = NV2A_PROF_FINISH_SURFACE_UP,
    [VK_FINISH_REASON_TEXTURE_UP] = NV2A_PROF_FINISH_TEXTURE_UP,
    [VK_FINISH_REASON_EAGER] = NV2A_PROF_FINISH_EAGER,
};

/* The reasons that leave the fence pending, rotation or not: the next
 * reclaim waits, and nothing touches GPU-owned memory before then (STALLED
 * reads reports at availability; the download and upload paths use the aux
 * buffer, submitted after this; an eager submission only starts the GPU
 * early). The others settle everything. */
static bool finish_is_async(FinishReason why)
{
    /* The flip and the display read nothing back: the frame's submission
     * runs on while the guest starts its next, and whatever reads its
     * results (the display's draw, the next slot's begin) is ordered
     * after it on the queue or waits for its fence then. With the cost
     * model on, the flip reads the frame's queries: synchronous. */
    if (why == VK_FINISH_REASON_FLIP_STALL ||
        why == VK_FINISH_REASON_PRESENTING) {
        return !xemu_cost_model_active();
    }
    return why == VK_FINISH_REASON_STALLED ||
           why == VK_FINISH_REASON_SURFACE_DOWN ||
           why == VK_FINISH_REASON_SURFACE_UP ||
           why == VK_FINISH_REASON_TEXTURE_UP ||
           why == VK_FINISH_REASON_EAGER;
}

/* Move to the other slot without waiting. A submission that staged data
 * keeps its staging and device buffers (its recorded binds hold their
 * handles): the next recording gets the other set. */
static void rotate_slot(PGRAPHVkState *r, bool staged)
{
    for (int i = 0; i < ARRAY_SIZE(pgraph_vk_staged_buffers); i++) {
        const StagedBuffer *f = &pgraph_vk_staged_buffers[i];
        if (staged) {
            StorageBuffer tmp = r->storage_buffers[f->staging];
            r->storage_buffers[f->staging] = r->alt_staging[i];
            r->alt_staging[i] = tmp;
            tmp = r->storage_buffers[f->device];
            r->storage_buffers[f->device] = r->alt_device[i];
            r->alt_device[i] = tmp;
        }
        /* The set now installed was last read by a submission on the slot
         * recorded next, whose fence the next begin waits for. */
        r->storage_buffers[f->staging].buffer_offset = 0;
    }
    r->slot = (r->slot + 1) % PGRAPH_VK_SLOTS;
    r->command_buffer = r->slot_main[r->slot];
}

void pgraph_vk_finish(PGRAPHState *pg, FinishReason finish_reason)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    pgraph_materialize_inline_array(pg);

    assert(!r->in_draw);
    assert(r->debug_depth == 0);

    if (r->in_command_buffer) {
        nv2a_profile_inc_counter(finish_reason_to_counter_enum[finish_reason]);

        if (r->in_render_pass) {
            end_render_pass(r);
        }
        if (r->query_in_flight) {
            end_query(r);
        }
        /* Cost queries cannot span command buffers: close the open segment
         * (the next draw reopens one; the resolve is linear). */
        xemu_cost_frame_close(container_of(pg, NV2AState, pgraph),
                              &pgraph_vk_cost_ops);

        VK_CHECK(vkEndCommandBuffer(r->command_buffer));

        /* This slot's staging-sync command buffer: free, as its fence settled
         * before the slot was begun again. */
        VkCommandBuffer sync_cmd = r->slot_sync[r->slot];
        VkCommandBufferBeginInfo sync_begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        /* Read before the sync copies below zero the staging offsets: whether
         * this submission staged data decides the set swap at rotation. */
        bool staged_this_submit = false;
        for (int i = 0; i < ARRAY_SIZE(pgraph_vk_staged_buffers); i++) {
            staged_this_submit |=
                r->storage_buffers[pgraph_vk_staged_buffers[i].staging]
                    .buffer_offset != 0;
        }

        if (staged_this_submit) {
            VK_CHECK(vkBeginCommandBuffer(sync_cmd, &sync_begin_info));
            sync_staging_buffers(pg, sync_cmd);
            VK_CHECK(vkEndCommandBuffer(sync_cmd));
        }
        flush_memory_buffer(pg);

        /* The sync copies' transfer barriers cover later command buffers on
         * this queue, including both uniform consumers. One submission needs
         * no internal binary semaphore. Without copies, omit sync entirely. */
        VkCommandBuffer commands[] = { sync_cmd, r->command_buffer };
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = staged_this_submit ? 2 : 1,
            .pCommandBuffers = commands + (staged_this_submit ? 0 : 1),
        };
        nv2a_profile_inc_counter(NV2A_PROF_QUEUE_SUBMIT);
        vkResetFences(r->device, 1, &r->slot_fence[r->slot]);
        /* through the core's wrapper: the queue is RetroArch's, shared */
        pgraph_vk_queue_submit(r, 1, &submit_info, r->slot_fence[r->slot]);
        r->submit_count += 1;
        r->slot_submit[r->slot] = r->submit_count;

        /* Every query so far is closed (end_query above) and submitted. */
        r->queries_submitted = r->num_queries_in_flight;

        /* This recording is submitted: its COW-arena references are now
         * guarded by the slot fence alone. */
        r->vertex_arena_used = false;

        // Periodically check memory budget
        const int max_num_submits_before_budget_update = 5;
        if (finish_reason == VK_FINISH_REASON_FLIP_STALL ||
            (r->submit_count - r->allocator_last_submit_index) >
                max_num_submits_before_budget_update) {

            // Refresh the heap budget without allocating or growing its backing.
            xemu_vk_update_budget(r->allocator, r->submit_count);
            r->allocator_last_submit_index = r->submit_count;
            r->budget_check_pending = true;
        }

        /* Separate this recording's final draw stamp from the next one's
         * start, even if no draw happens between the two. Otherwise that
         * final surface still looks pending and display/readback/upload
         * paths unnecessarily submit the new recording. This does not
         * retire any work: slot fences and submit serials still own it. */
        pg->draw_time++;
        r->in_command_buffer = false;
        r->slot_fence_pending[r->slot] = true;

        bool async_ok = finish_is_async(finish_reason);
        if (async_ok) {
            rotate_slot(r, staged_this_submit);
        } else {
            /* Synchronous: settle every slot, then tear down at this safe
             * point (nothing of the next recording exists yet). */
            pgraph_vk_wait_all_slots(r);
            teardown_after_settle(pg);
        }
    } else if (!finish_is_async(finish_reason)) {
        /* Nothing recorded since an asynchronous finish, whose submission
         * may still run: a synchronous one settles it all the same. */
        pgraph_vk_settle_all_slots(pg);
    }

    NV2AState *d = container_of(pg, NV2AState, pgraph);
    pgraph_vk_process_pending_reports_internal(d);
}

/* Deferred destruction (renderer.h: evictions under rotation, framebuffers):
 * the entries whose last submission has retired, i.e. up to
 * settled_submit; all of them once every slot has settled. */
static void trash_drain_retired(PGRAPHVkState *r)
{
    int kept = 0;
    bool display_polled = false;
    bool aux_polled = false;
    for (int i = 0; i < r->trash_n; i++) {
        struct XemuVkTrashEntry *e = &r->trash[i];
        if ((int32_t)(e->retire_after - r->settled_submit) > 0) {
            r->trash[kept++] = *e;
            continue;
        }
        if (e->display_after > r->display.settled) {
            if (!display_polled) {
                pgraph_vk_poll_display_retired(r);
                display_polled = true;
            }
            if (e->display_after > r->display.settled) {
                r->trash[kept++] = *e;
                continue;
            }
        }
        if (e->aux_after > r->aux_settled) {
            if (!aux_polled) {
                pgraph_vk_poll_aux_retired(r);
                aux_polled = true;
            }
            if (e->aux_after > r->aux_settled) {
                r->trash[kept++] = *e;
                continue;
            }
        }
        if (e->pipeline) {
            vkDestroyPipeline(r->device, e->pipeline, NULL);
        }
        if (e->layout) {
            vkDestroyPipelineLayout(r->device, e->layout, NULL);
        }
        if (e->sampler) {
            vkDestroySampler(r->device, e->sampler, NULL);
        }
        if (e->framebuffer) {
            vkDestroyFramebuffer(r->device, e->framebuffer, NULL);
        }
        if (e->image_view && !e->pool_view) {
            vkDestroyImageView(r->device, e->image_view, NULL);
        }
        if (e->image) {
            bool pooled = e->pool_ok && (e->pool_view ?
                pgraph_vk_image_pool_put_with_view(r, e->image, e->allocation,
                    &e->image_ci, e->image_view) :
                pgraph_vk_image_pool_put(r, e->image, e->allocation, &e->image_ci));
            if (!pooled) {
                if (e->pool_view && e->image_view) {
                    vkDestroyImageView(r->device, e->image_view, NULL);
                }
                xemu_vk_destroy_image(r->allocator, e->image, e->allocation);
            }
        }
    }
    r->trash_n = kept;
}

/* Observe same-queue completion without releasing slots or current bindings.
 * Safe both during allocation recovery and normal retirement polling. */
static void poll_completed_draw_submissions(PGRAPHVkState *r)
{
    /* A completed newest submission covers older slots on the same queue.
     * Query it first, then retain partial-completion checks if still busy. */
    int newest = -1;
    uint32_t newest_submit = r->settled_submit;
    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        if (r->slot_fence_pending[s] &&
            (int32_t)(r->slot_submit[s] - newest_submit) > 0) {
            newest = s;
            newest_submit = r->slot_submit[s];
        }
    }
    if (newest >= 0 &&
        vkGetFenceStatus(r->device, r->slot_fence[newest]) == VK_SUCCESS) {
        r->settled_submit = newest_submit;
    }

    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        if (s != newest && r->slot_fence_pending[s] &&
            (int32_t)(r->slot_submit[s] - r->settled_submit) > 0 &&
            vkGetFenceStatus(r->device, r->slot_fence[s]) == VK_SUCCESS) {
            r->settled_submit = r->slot_submit[s];
        }
    }
}

/* An allocation retry can run midway through binding a texture or surface.
 * Observe completed work and free only its deferred garbage; the normal
 * settle path also tears down current bindings and must not run here. */
void pgraph_vk_reclaim_retired_images(PGRAPHVkState *r)
{
    poll_completed_draw_submissions(r);
    /* This also checks independent display and upload retirement. Leave
     * slot flags/fences and timestamp resolution to normal slot reuse. */
    trash_drain_retired(r);
    pgraph_vk_image_pool_flush(r);
}

void pgraph_vk_trash_drain(PGRAPHVkState *r)
{
    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        assert(!r->slot_fence_pending[s]);
    }
    r->settled_submit = r->submit_count;
    trash_drain_retired(r);
    assert(r->trash_n == 0 || r->display.settled < r->display.submitted ||
           r->aux_settled < r->aux_submitted);
}

/* The open recording may still reference what is pushed here, so nothing is
 * destroyed early: the list grows instead, and is drained once everything
 * has settled. */
void pgraph_vk_trash_push(PGRAPHVkState *r, const struct XemuVkTrashEntry *e)
{
    if (r->trash_n == r->trash_cap) {
        r->trash_cap = r->trash_cap ? r->trash_cap * 2 : 512;
        r->trash = g_renew(struct XemuVkTrashEntry, r->trash, r->trash_cap);
    }
    r->trash[r->trash_n] = *e;
    /* the open recording, if any, is the last that may use it */
    r->trash[r->trash_n].retire_after =
        r->submit_count + (r->in_command_buffer ? 1 : 0);
    r->trash[r->trash_n].display_after = r->display.submitted;
    r->trash_n++;
}

void pgraph_vk_slot_retired(PGRAPHState *pg, int slot)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    lr_gpu_ram_drop((uintptr_t)r->slot_main[slot]);
    lr_gpu_ram_drop((uintptr_t)r->slot_sync[slot]);

    if ((int32_t)(r->slot_submit[slot] - r->settled_submit) > 0) {
        r->settled_submit = r->slot_submit[slot];
    }
    trash_drain_retired(r);

}

void pgraph_vk_settle_poll(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    bool pending = false;

    poll_completed_draw_submissions(r);

    /* Reuse allocation-retry or later same-queue completion proofs. Slot
     * cleanup still runs here exactly once, even when no query is needed. */
    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        if (r->slot_fence_pending[s] &&
            (int32_t)(r->slot_submit[s] - r->settled_submit) <= 0) {
            r->slot_fence_pending[s] = false;
            pgraph_vk_slot_retired(pg, s);
        }
        pending |= r->slot_fence_pending[s];
    }
    if (!pending && !r->in_command_buffer) {
        teardown_after_settle(pg);
    }
}

/* Wait for the current slot's submission, if pending, before the slot is
 * recorded again. Idempotent. */
void pgraph_vk_reclaim_command_buffer(PGRAPHState *pg, bool reuse)
{
    (void)reuse;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (r->slot_fence_pending[r->slot]) {
        /* Allocation-retry reclamation can already prove this submission
         * complete while leaving slot cleanup here. Reuse that serial proof
         * instead of asking the driver to wait for completed work again. */
        if ((int32_t)(r->slot_submit[r->slot] - r->settled_submit) > 0) {
            VK_CHECK(vkWaitForFences(r->device, 1, &r->slot_fence[r->slot],
                                     VK_TRUE, UINT64_MAX));
        }
        r->slot_fence_pending[r->slot] = false;
        pgraph_vk_slot_retired(pg, r->slot);
    }
    /* No teardown here: reclaim also runs in the middle of a draw's setup
     * (begin_pre_draw), where it would destroy the framebuffer just made;
     * see teardown_after_settle. */
}

/* Wait for every slot's pending submission. */
void pgraph_vk_wait_all_slots(PGRAPHVkState *r)
{
    /* All command slots submit to the same queue. The latest pending
     * submission's fence covers the earlier commands too; already proven
     * serials need no driver wait. Do not release ownership until it returns. */
    int latest = -1;
    uint32_t latest_submit = r->settled_submit;
    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        if (r->slot_fence_pending[s] &&
            (int32_t)(r->slot_submit[s] - latest_submit) > 0) {
            latest = s;
            latest_submit = r->slot_submit[s];
        }
    }
    if (latest >= 0) {
        VK_CHECK(vkWaitForFences(r->device, 1, &r->slot_fence[latest], VK_TRUE,
                                 UINT64_MAX));
    }
    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        if (r->slot_fence_pending[s]) {
            r->slot_fence_pending[s] = false;
            lr_gpu_ram_drop((uintptr_t)r->slot_main[s]);
            lr_gpu_ram_drop((uintptr_t)r->slot_sync[s]);
        }
    }
    r->settled_submit = r->submit_count;
    trash_drain_retired(r);

}

/* Global teardown (descriptor sets, trash, budget); only legal with
 * every slot settled and no recording open. */
static void teardown_after_settle(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    for (int s = 0; s < PGRAPH_VK_SLOTS; s++) {
        assert(!r->slot_fence_pending[s]);
    }
    pgraph_vk_trash_drain(r);

    /* Every submission so far has retired. */
    r->settled_submit = r->submit_count;

    /* Nothing unsettled reads the mirror now: a draw between sync and bind
     * re-marks its spans at bind. Arena snapshots survive (append-only). */
    bitmap_clear(r->vertex_consumed_bitmap, 0, r->bitmap_size);

    if (r->budget_check_pending) {
        r->budget_check_pending = false;
        pgraph_vk_check_memory_budget(pg);
    }
}

/* Settle every in-flight submission and, outside a recording, run the
 * global teardown (clears the vertex consumed marks). */
void pgraph_vk_settle_all_slots(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    pgraph_vk_wait_all_slots(r);
    if (!r->in_command_buffer) {
        teardown_after_settle(pg);
    }
}

/* Everything submitted has run and the reports it answers are in guest RAM,
 * as a snapshot has to see it: a report left queued would never complete
 * after a load. */
void pgraph_vk_settle_reports(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (r->in_command_buffer) {
        /* Synchronous: every slot settles, then the reports are served. */
        pgraph_vk_finish(pg, VK_FINISH_REASON_FLUSH);
        return;
    }
    pgraph_vk_wait_all_slots(r);
    pgraph_vk_process_pending_reports_internal(
        container_of(pg, NV2AState, pgraph));
}

void pgraph_vk_begin_command_buffer(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(!r->in_command_buffer);

    /* An async finish may have left this slot's fence pending: wait for it
     * before the slot's command buffer is reused. */
    {
        pgraph_vk_reclaim_command_buffer(pg, true);
    }

    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    r->command_buffer = r->slot_main[r->slot];
    /* this slot's half of the descriptor sets, its last reader retired */
    r->descriptor_set_base =
        r->slot * (ARRAY_SIZE(r->descriptor_sets) / PGRAPH_VK_SLOTS);
    r->descriptor_set_index = r->descriptor_set_base;
    VK_CHECK(vkBeginCommandBuffer(r->command_buffer,
                                  &command_buffer_begin_info));

    r->command_buffer_start_time = pg->draw_time;
    r->in_command_buffer = true;
    r->dyn_last.valid = false;
    r->vi_last.valid = false;

    /* Reset, outside any render pass, every cost-segment slot this frame may
     * still use, so segments begin and end inside render passes; the lower
     * slots hold earlier results the flip still reads. */
    r->cost_pools_reset = false;
    if (r->cost_stats_pool != VK_NULL_HANDLE && xemu_cost_model_active() &&
        xemu_cost_segn < XEMU_COST_SEGS) {
        vkCmdResetQueryPool(r->command_buffer, r->cost_stats_pool,
                            xemu_cost_segn, XEMU_COST_SEGS - xemu_cost_segn);
        vkCmdResetQueryPool(r->command_buffer, r->cost_occl_pool,
                            xemu_cost_segn, XEMU_COST_SEGS - xemu_cost_segn);
        r->cost_pools_reset = true;
    }
}

/* Dynamic state is emitted when its registers change, and after every
 * pipeline bind (binding a pipeline with static state undefines it). */
static void emit_dynamic_state(PGRAPHState *pg, bool force)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    /* Clear pipelines own their static blend/mask/depth state. Emitting
     * normal draw registers here clobbers that state on some drivers. */
    if (pg->clearing) {
        r->dyn_last.valid = false;
        return;
    }
    VkCommandBuffer cmd = r->command_buffer;

    uint32_t blend = pgraph_surface_blend(pgraph_reg_r(pg, NV_PGRAPH_BLEND),
                                         pg->surface_shape.color_format);
    uint32_t blendcolor = pgraph_reg_r(pg, NV_PGRAPH_BLENDCOLOR);
    uint32_t control_0 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0);
    uint32_t setupraster = pgraph_reg_r(pg, NV_PGRAPH_SETUPRASTER);
    uint32_t control_1 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1);
    uint32_t control_2 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_2);
    /* Clears run this path with no shader bound. */
    uint32_t topo = r->shader_binding ?
                        get_primitive_topology(pg) :
                        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    if (!force && r->dyn_last.valid && r->dyn_last.blend == blend &&
        r->dyn_last.blendcolor == blendcolor &&
        r->dyn_last.control_0 == control_0 &&
        r->dyn_last.control_1 == control_1 &&
        r->dyn_last.control_2 == control_2 &&
        r->dyn_last.topology == topo &&
        r->dyn_last.setupraster == setupraster) {
        return;
    }

    float blend_constant[4];
    pgraph_argb_pack32_to_rgba_float(blendcolor, blend_constant);
    vkCmdSetBlendConstants(cmd, blend_constant);

    if (r->dyn_cull_front) {
        VkCullModeFlags cull = VK_CULL_MODE_NONE;
        if (setupraster & NV_PGRAPH_SETUPRASTER_CULLENABLE) {
            uint32_t cull_face =
                GET_MASK(setupraster, NV_PGRAPH_SETUPRASTER_CULLCTRL);
            assert(cull_face < ARRAY_SIZE(pgraph_cull_face_vk_map));
            cull = pgraph_cull_face_vk_map[cull_face];
        }
        vkCmdSetCullModeEXT(cmd, cull);
        vkCmdSetFrontFaceEXT(cmd,
                             (setupraster & NV_PGRAPH_SETUPRASTER_FRONTFACE) ?
                                 VK_FRONT_FACE_COUNTER_CLOCKWISE :
                                 VK_FRONT_FACE_CLOCKWISE);
    }

    if (r->dyn_blend) {
        VkColorComponentFlags write_mask = color_write_mask(control_0);
        vkCmdSetColorWriteMaskEXT(cmd, 0, 1, &write_mask);

        VkBool32 blend_enable = (blend & NV_PGRAPH_BLEND_EN) ? VK_TRUE :
                                                               VK_FALSE;
        vkCmdSetColorBlendEnableEXT(cmd, 0, 1, &blend_enable);

        uint32_t sfactor = GET_MASK(blend, NV_PGRAPH_BLEND_SFACTOR);
        uint32_t dfactor = GET_MASK(blend, NV_PGRAPH_BLEND_DFACTOR);
        uint32_t equation = GET_MASK(blend, NV_PGRAPH_BLEND_EQN);
        assert(sfactor < ARRAY_SIZE(pgraph_blend_factor_vk_map));
        assert(dfactor < ARRAY_SIZE(pgraph_blend_factor_vk_map));
        assert(equation < ARRAY_SIZE(pgraph_blend_equation_vk_map));
        VkColorBlendEquationEXT eq = {
            .srcColorBlendFactor = pgraph_blend_factor_vk_map[sfactor],
            .dstColorBlendFactor = pgraph_blend_factor_vk_map[dfactor],
            .colorBlendOp = pgraph_blend_equation_vk_map[equation],
            .srcAlphaBlendFactor = pgraph_blend_factor_vk_map[sfactor],
            .dstAlphaBlendFactor = pgraph_blend_factor_vk_map[dfactor],
            .alphaBlendOp = pgraph_blend_equation_vk_map[equation],
        };
        vkCmdSetColorBlendEquationEXT(cmd, 0, 1, &eq);
    }

    if (r->dyn_cull_front) {
        bool depth_test = control_0 & NV_PGRAPH_CONTROL_0_ZENABLE;
        bool stencil_test = control_1 & NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;
        uint32_t zfunc = GET_MASK(control_0, NV_PGRAPH_CONTROL_0_ZFUNC);
        assert(zfunc < ARRAY_SIZE(pgraph_depth_func_vk_map));
        vkCmdSetDepthTestEnableEXT(cmd, depth_test);
        vkCmdSetDepthWriteEnableEXT(
            cmd, !!(control_0 & NV_PGRAPH_CONTROL_0_ZWRITEENABLE));
        vkCmdSetDepthCompareOpEXT(cmd, pgraph_depth_func_vk_map[zfunc]);
        vkCmdSetDepthBoundsTestEnableEXT(cmd, VK_FALSE);
        vkCmdSetStencilTestEnableEXT(cmd, stencil_test);
        if (stencil_test) {
            uint32_t sfunc =
                GET_MASK(control_1, NV_PGRAPH_CONTROL_1_STENCIL_FUNC);
            uint32_t fail =
                GET_MASK(control_2, NV_PGRAPH_CONTROL_2_STENCIL_OP_FAIL);
            uint32_t zfail =
                GET_MASK(control_2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZFAIL);
            uint32_t zpass =
                GET_MASK(control_2, NV_PGRAPH_CONTROL_2_STENCIL_OP_ZPASS);
            assert(sfunc < ARRAY_SIZE(pgraph_stencil_func_vk_map));
            assert(fail < ARRAY_SIZE(pgraph_stencil_op_vk_map));
            assert(zfail < ARRAY_SIZE(pgraph_stencil_op_vk_map));
            assert(zpass < ARRAY_SIZE(pgraph_stencil_op_vk_map));
            vkCmdSetStencilOpEXT(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                                 pgraph_stencil_op_vk_map[fail],
                                 pgraph_stencil_op_vk_map[zpass],
                                 pgraph_stencil_op_vk_map[zfail],
                                 pgraph_stencil_func_vk_map[sfunc]);
        } else {
            vkCmdSetStencilOpEXT(cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
                                 VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP,
                                 VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS);
        }
        vkCmdSetStencilCompareMask(
            cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
            GET_MASK(control_1, NV_PGRAPH_CONTROL_1_STENCIL_MASK_READ));
        vkCmdSetStencilWriteMask(
            cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
            GET_MASK(control_1, NV_PGRAPH_CONTROL_1_STENCIL_MASK_WRITE));
        vkCmdSetStencilReference(
            cmd, VK_STENCIL_FACE_FRONT_AND_BACK,
            GET_MASK(control_1, NV_PGRAPH_CONTROL_1_STENCIL_REF));
        vkCmdSetPrimitiveTopologyEXT(cmd, topo);
    }

    r->dyn_last.valid = true;
    r->dyn_last.blend = blend;
    r->dyn_last.blendcolor = blendcolor;
    r->dyn_last.control_0 = control_0;
    r->dyn_last.setupraster = setupraster;
    r->dyn_last.control_1 = control_1;
    r->dyn_last.control_2 = control_2;
    r->dyn_last.topology = topo;
}

static void emit_dynamic_vertex_input(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    int nb = r->num_active_vertex_binding_descriptions;
    int na = r->num_active_vertex_attribute_descriptions;

    /* A clear draws nothing, and its pipeline may take the vertex input
     * statically, which leaves the dynamic state undefined afterwards. */
    if (pg->clearing) {
        r->vi_last.valid = false;
        return;
    }
    if (r->vi_last.valid && r->vi_last.nb == nb && r->vi_last.na == na &&
        !memcmp(r->vi_last.bind, r->vertex_binding_descriptions,
                nb * sizeof(r->vi_last.bind[0])) &&
        !memcmp(r->vi_last.attr, r->vertex_attribute_descriptions,
                na * sizeof(r->vi_last.attr[0]))) {
        return;
    }

    VkVertexInputBindingDescription2EXT bindings[NV2A_VERTEXSHADER_ATTRIBUTES];
    VkVertexInputAttributeDescription2EXT
        attributes[NV2A_VERTEXSHADER_ATTRIBUTES];
    for (int i = 0; i < r->num_active_vertex_binding_descriptions; i++) {
        bindings[i] = (VkVertexInputBindingDescription2EXT){
            .sType =
                VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
            .binding = r->vertex_binding_descriptions[i].binding,
            .stride = r->vertex_binding_descriptions[i].stride,
            .inputRate = r->vertex_binding_descriptions[i].inputRate,
            .divisor = 1,
        };
    }
    for (int i = 0; i < r->num_active_vertex_attribute_descriptions; i++) {
        attributes[i] = (VkVertexInputAttributeDescription2EXT){
            .sType =
                VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
            .location = r->vertex_attribute_descriptions[i].location,
            .binding = r->vertex_attribute_descriptions[i].binding,
            .format = r->vertex_attribute_descriptions[i].format,
            .offset = r->vertex_attribute_descriptions[i].offset,
        };
    }
    nv2a_profile_inc_counter(NV2A_PROF_VERTEX_INPUT_SET);
    vkCmdSetVertexInputEXT(r->command_buffer,
                           r->num_active_vertex_binding_descriptions, bindings,
                           r->num_active_vertex_attribute_descriptions,
                           attributes);
    r->vi_last.valid = true;
    r->vi_last.nb = nb;
    r->vi_last.na = na;
    memcpy(r->vi_last.bind, r->vertex_binding_descriptions,
           nb * sizeof(r->vi_last.bind[0]));
    memcpy(r->vi_last.attr, r->vertex_attribute_descriptions,
           na * sizeof(r->vi_last.attr[0]));
}

// FIXME: Refactor below

void pgraph_vk_ensure_command_buffer(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!r->in_command_buffer) {
        pgraph_vk_begin_command_buffer(pg);
    }
}

void pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    end_render_pass(r);
    if (r->query_in_flight) {
        end_query(r);
    }
}

VkCommandBuffer pgraph_vk_begin_nondraw_commands(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    pgraph_vk_ensure_command_buffer(pg);
    pgraph_vk_ensure_not_in_render_pass(pg);
    return r->command_buffer;
}

void pgraph_vk_end_nondraw_commands(PGRAPHState *pg, VkCommandBuffer cmd)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(cmd == r->command_buffer);
}

// FIXME: Add more metrics for determining command buffer 'fullness' and
// conservatively flush. Unfortunately there doesn't appear to be a good
// way to determine what the actual maximum capacity of a command buffer
// is, but we are obviously not supposed to endlessly append to one command
// buffer. For other reasons though (like descriptor set amount, surface
// changes, etc) we do flush often.

/* Only masked color clears rasterize a triangle. Attachment clears need
 * the compatible render pass/framebuffer, never a graphics pipeline. */
static bool clear_needs_color_draw(PGRAPHVkState *r)
{
    uint32_t color = r->clear_parameter & NV097_CLEAR_SURFACE_COLOR;
    return r->color_binding && color && color != NV097_CLEAR_SURFACE_COLOR;
}

static void begin_pre_draw(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* Reclaim first: with rotation off, the cache evictions below destroy at
     * once what the pending submission on this slot may still read. */
    pgraph_vk_reclaim_command_buffer(pg, true);

    /* In-place sampling: a surface sampled in place returns to its attachment
     * layout before it is drawn to; the transition orders the reads first. */
    if (r->color_binding && r->color_binding->in_shader_read) {
        pgraph_vk_surface_ensure_attachment_layout(pg, r->color_binding);
    }
    if (r->zeta_binding && r->zeta_binding->in_shader_read) {
        pgraph_vk_surface_ensure_attachment_layout(pg, r->zeta_binding);
    }

    assert(r->color_binding || r->zeta_binding);
    assert(!r->color_binding || r->color_binding->initialized);
    assert(!r->zeta_binding || r->zeta_binding->initialized);

    VkRenderPass render_pass;
    if (pg->clearing && !clear_needs_color_draw(r)) {
        RenderPassState state;
        init_render_pass_state(pg, &state);
        render_pass = get_render_pass(r, &state);
    } else {
        if (pg->clearing) {
            create_clear_pipeline(pg);
        } else {
            create_pipeline(pg);
        }
        render_pass = r->pipeline_binding->render_pass;
    }

    bool render_pass_dirty = render_pass != r->render_pass;

    if (r->framebuffer_dirty || render_pass_dirty) {
        pgraph_vk_ensure_not_in_render_pass(pg);
    }
    if (render_pass_dirty) {
        r->render_pass = render_pass;
        r->framebuffer_dirty = true;
    }
    /* update_descriptor_sets may finish the recording (descriptor pool full):
     * ensure it again after, and look up a framebuffer retired meanwhile. */
    pgraph_vk_ensure_command_buffer(pg);
    if (r->framebuffer_dirty) {
        create_frame_buffer(pg);
        r->framebuffer_dirty = false;
    }
    if (!pg->clearing) {
        pgraph_vk_update_descriptor_sets(pg);
    }
    pgraph_vk_ensure_command_buffer(pg);
    if (r->framebuffer == VK_NULL_HANDLE) {
        create_frame_buffer(pg);
    }
}

static float clamp_line_width_to_device_limits(PGRAPHState *pg, float width)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    float min_width = r->device_props.limits.lineWidthRange[0];
    float max_width = r->device_props.limits.lineWidthRange[1];
    float granularity = r->device_props.limits.lineWidthGranularity;

    if (granularity != 0.0f) {
        float steps = roundf((width - min_width) / granularity);
        width = min_width + steps * granularity;
    }
    return fminf(fmaxf(min_width, width), max_width);
}

static void begin_draw(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(r->in_command_buffer);

    bool must_bind_pipeline = r->pipeline_binding_changed;

    if (pg->clearing) {
        end_render_pass(r);
    }

    if (r->host_query_reset) {
        /* Host reset already makes a free slot unavailable before the CPU
         * can poll it. No reset command or pass split is needed here. */
        bool want_query = !pg->clearing && pg->zpass_pixel_count_enable;
        if (r->query_in_flight && (r->new_query_needed || !want_query)) {
            if (r->cost_recording) {
                vk_cost_close_recorded_queries(r);
                xemu_cost_backend_interrupt();
            }
            end_query(r);
        }
        if (want_query && !r->query_in_flight) {
            if (!r->in_render_pass) {
                begin_render_pass(pg);
                must_bind_pipeline = true;
            }
            /* Cost-model occlusion and game occlusion cannot overlap, even
             * though their pools differ. Close the segment, not the pass. */
            if (r->cost_recording) {
                vk_cost_close_recorded_queries(r);
                xemu_cost_backend_interrupt();
            }
            begin_query(r);
        }
    } else if (!pg->clearing && pg->zpass_pixel_count_enable) {
        /* A recorded reset must remain outside the pass on these devices. */
        if (r->new_query_needed && r->query_in_flight) {
            end_render_pass(r);
            end_query(r);
        }
        if (!r->query_in_flight) {
            /* end_render_pass also closes our cost segment's occlusion
             * query, so the game's never overlaps it. */
            end_render_pass(r);
            begin_query(r);
        }
    } else if (r->query_in_flight) {
        end_render_pass(r);
        end_query(r);
    }

    if (!r->in_render_pass) {
        begin_render_pass(pg);
        must_bind_pipeline = true;
    }

    vk_cost_materialize(pg);

    if (pg->clearing && !clear_needs_color_draw(r)) {
        /* No draw follows: do not bind/dereference a possibly absent PSO.
         * Preserve the clear's dynamic-state invalidation and draw scope. */
        r->dyn_last.valid = false;
        r->vi_last.valid = false;
        r->in_draw = true;
        return;
    }

    if (must_bind_pipeline) {
        nv2a_profile_inc_counter(NV2A_PROF_PIPELINE_BIND);
        vkCmdBindPipeline(r->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          r->pipeline_binding->pipeline);
        r->pipeline_binding->draw_time = pg->draw_time;

        unsigned int vp_width = pg->surface_binding_dim.width,
                     vp_height = pg->surface_binding_dim.height;
        pgraph_apply_scaling_factor(pg, &vp_width, &vp_height);

        VkViewport viewport = {
            .width = vp_width,
            .height = vp_height,
            .minDepth = 0.0,
            .maxDepth = 1.0,
        };
        vkCmdSetViewport(r->command_buffer, 0, 1, &viewport);

        /* Surface clip */
        /* FIXME: Consider moving to PSH w/ window clip */
        unsigned int xmin = pg->surface_shape.clip_x,
                     ymin = pg->surface_shape.clip_y;

        unsigned int scissor_width = pg->surface_shape.clip_width,
                     scissor_height = pg->surface_shape.clip_height;

        pgraph_apply_anti_aliasing_factor(pg, &xmin, &ymin);
        pgraph_apply_anti_aliasing_factor(pg, &scissor_width, &scissor_height);

        pgraph_apply_scaling_factor(pg, &xmin, &ymin);
        pgraph_apply_scaling_factor(pg, &scissor_width, &scissor_height);

        VkRect2D scissor = {
            .offset.x = xmin,
            .offset.y = ymin,
            .extent.width = scissor_width,
            .extent.height = scissor_height,
        };
        vkCmdSetScissor(r->command_buffer, 0, 1, &scissor);
    }

    /* NV097_SET_LINE_WIDTH is in 1/8 pixel, scaled to the internal
     * resolution like the GL path. Kept apart from the other dynamic state:
     * the guest re-poses it per segment, which must not re-emit blend and
     * cull, and a pipeline bind always sets it again. */
    if (r->pipeline_binding->has_dynamic_line_width) {
        float line_width = clamp_line_width_to_device_limits(
            pg, (pg->line_width / 8.0f) * pg->surface_scale_factor);
        if (must_bind_pipeline || !r->dyn_last.valid ||
            line_width != r->dyn_last.line_width) {
            vkCmdSetLineWidth(r->command_buffer, line_width);
            r->dyn_last.line_width = line_width;
        }
    }

    emit_dynamic_state(pg, must_bind_pipeline);
    if (r->dyn_vertex_input) {
        emit_dynamic_vertex_input(pg);
    }

    if (!pg->clearing) {
        bind_descriptor_sets(pg);
        push_vertex_attr_values(pg);
    }

    r->in_draw = true;
}

static void end_draw(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    assert(r->in_command_buffer);
    assert(r->in_render_pass);

    if (pg->clearing) {
        end_render_pass(r);
    }

    r->in_draw = false;
}

static void eager_submit_after_draw(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    /* The NV2A consumes its pushbuffer continuously: every 128 draws
     * [HEURISTIC], submit the recording if the next ring slot has retired
     * (never wait), also in scenes that seldom switch target (surface.c). */
    const unsigned eager_draws = 128;
    if (r->in_command_buffer &&
        ++r->eager_draw_count >= eager_draws) {
        int next = (r->slot + 1) % PGRAPH_VK_SLOTS;
        bool slot_free = !r->slot_fence_pending[next];
        if (!slot_free &&
            ((int32_t)(r->slot_submit[next] - r->settled_submit) <= 0 ||
             vkGetFenceStatus(r->device, r->slot_fence[next]) == VK_SUCCESS)) {
            /* Completion is proved by a serial or fence. The next
             * draw must not wait again for the already completed slot. */
            r->slot_fence_pending[next] = false;
            pgraph_vk_slot_retired(pg, next);
            slot_free = true;
        }
        if (slot_free) {
            r->eager_draw_count = 0;
            pgraph_vk_finish(pg, VK_FINISH_REASON_EAGER);
        } else {
            r->eager_draw_count = eager_draws;
        }
    }
}

void pgraph_vk_draw_end(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    uint32_t control_0 = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0);
    bool mask_alpha = control_0 & NV_PGRAPH_CONTROL_0_ALPHA_WRITE_ENABLE;
    bool mask_red = control_0 & NV_PGRAPH_CONTROL_0_RED_WRITE_ENABLE;
    bool mask_green = control_0 & NV_PGRAPH_CONTROL_0_GREEN_WRITE_ENABLE;
    bool mask_blue = control_0 & NV_PGRAPH_CONTROL_0_BLUE_WRITE_ENABLE;
    bool color_write = mask_alpha || mask_red || mask_green || mask_blue;
    bool depth_test = control_0 & NV_PGRAPH_CONTROL_0_ZENABLE;
    bool stencil_test =
        pgraph_reg_r(pg, NV_PGRAPH_CONTROL_1) & NV_PGRAPH_CONTROL_1_STENCIL_TEST_ENABLE;
    bool is_nop_draw = !(color_write || depth_test || stencil_test);

    if (is_nop_draw) {
        // FIXME: Check PGRAPH register 0x880.
        // HW uses bit 11 in 0x880 to enable or disable a color/zeta limit
        // check that will raise an exception in the case that a draw should
        // modify the color and/or zeta buffer but the target(s) are masked
        // off. This check only seems to trigger during the fragment
        // processing, it is legal to attempt a draw that is entirely
        // clipped regardless of 0x880. See xemu#635 for context.
        NV2A_VK_DPRINTF("nop draw!\n");
        return;
    }

    pgraph_vk_flush_draw(d);

    if (vk_cost_probe_active(pg)) {
        /* Tally after the flush, unlike GL: a finish during vertex streaming
         * reopens the segment, and only then is segn-1 the slot that holds
         * this draw's fragments. */
        xemu_cost_draw_end_tally(pg);
    }

    pg->draw_time++;
    if (r->color_binding && pgraph_color_write_enabled(pg)) {
        r->color_binding->draw_time = pg->draw_time;
    }
    if (r->zeta_binding && pgraph_zeta_write_enabled(pg)) {
        r->zeta_binding->draw_time = pg->draw_time;
    }

    pgraph_vk_set_surface_dirty(pg, color_write, depth_test || stencil_test);

    eager_submit_after_draw(pg);
}

static int compare_memory_sync_requirement_by_addr(const void *p1,
                                                   const void *p2)
{
    const MemorySyncRequirement *l = p1, *r = p2;
    if (l->addr < r->addr)
        return -1;
    if (l->addr > r->addr)
        return 1;
    return 0;
}

/* Where a synced range's vertex data is read from this draw: its arena
 * snapshot when cow is set, the mirror otherwise. */
typedef struct VertexCowDispatch {
    bool cow;
    hwaddr lo;
    int slot;
    VkDeviceSize arena_offset;
} VertexCowDispatch;

/* Most adjacent draws revisit a few immutable snapshots. Keep a tiny
 * render-thread-owned index in front of the full containment search. */
static void vertex_cow_remember(PGRAPHVkState *r, unsigned index)
{
    unsigned key = index + 1;
    unsigned n = 0;
    while (n + 1 < ARRAY_SIZE(r->vertex_cow_recent) &&
           r->vertex_cow_recent[n] != key) {
        n++;
    }
    for (; n; n--) {
        r->vertex_cow_recent[n] = r->vertex_cow_recent[n - 1];
    }
    r->vertex_cow_recent[0] = key;
}

static bool vertex_cow_contains(PGRAPHVkState *r,
                                 const VertexCowCacheEntry *e,
                                 hwaddr lo, hwaddr end)
{
    return e->valid && e->addr <= lo && end <= e->addr + e->size &&
           e->epoch == r->vertex_arena_epoch[e->slot];
}

/* A valid snapshot containing [lo, end): containment, not equality, since
 * draws of one buffer use varying element windows, and any subrange of a
 * valid snapshot is exact. */
static VertexCowCacheEntry *vertex_cow_lookup(PGRAPHVkState *r, hwaddr lo,
                                              hwaddr end)
{
    for (unsigned i = 0; i < ARRAY_SIZE(r->vertex_cow_recent); i++) {
        unsigned key = r->vertex_cow_recent[i];
        if (!key) {
            continue;
        }
        VertexCowCacheEntry *e = &r->vertex_cow_cache[key - 1];
        if (vertex_cow_contains(r, e, lo, end)) {
            if (i) {
                vertex_cow_remember(r, key - 1);
            }
            return e;
        }
    }
    for (unsigned c = 0; c < ARRAY_SIZE(r->vertex_cow_cache); c++) {
        VertexCowCacheEntry *e = &r->vertex_cow_cache[c];
        if (vertex_cow_contains(r, e, lo, end)) {
            vertex_cow_remember(r, c);
            return e;
        }
    }
    return NULL;
}

/* Cache the snapshot of [lo, lo + size) at arena offset dst for reuse by
 * later draws of any contained window. */
static void vertex_cow_store(PGRAPHVkState *r, hwaddr lo, VkDeviceSize size,
                             VkDeviceSize dst, unsigned slot)
{
    VertexCowCacheEntry *store = NULL;
    for (int c = 0; c < ARRAY_SIZE(r->vertex_cow_cache); c++) {
        VertexCowCacheEntry *e = &r->vertex_cow_cache[c];
        if (e->valid && e->addr == lo && e->size == size) {
            store = e;
            break;
        }
        if (!store && !e->valid) {
            store = e;
        }
    }
    if (!store) {
        store = &r->vertex_cow_cache[r->vertex_cow_cache_rr++ %
                                     ARRAY_SIZE(r->vertex_cow_cache)];
    }
    *store = (VertexCowCacheEntry){
        .addr = lo,
        .size = size,
        .arena_offset = dst,
        .epoch = r->vertex_arena_epoch[slot],
        .slot = slot,
        .valid = true,
    };
    r->vertex_cow_tail[slot] = store - r->vertex_cow_cache + 1;
    vertex_cow_remember(r, store - r->vertex_cow_cache);
}

/* Only an arena's last allocation can grow without moving bytes.
 * Earlier draws keep their immutable prefix. On non-coherent memory, the
 * tail must start in a separate atom from any existing reader. */
static VertexCowCacheEntry *vertex_cow_extendable(PGRAPHVkState *r,
                                                  hwaddr lo,
                                                  VkDeviceSize size,
                                                  unsigned slot)
{
    unsigned key = r->vertex_cow_tail[slot];
    if (!key || key > ARRAY_SIZE(r->vertex_cow_cache)) {
        return NULL;
    }
    VertexCowCacheEntry *e = &r->vertex_cow_cache[key - 1];
    StorageBuffer *arena = &r->vertex_arena[slot];
    if (!e->valid || e->slot != slot ||
        e->epoch != r->vertex_arena_epoch[slot] || e->addr != lo ||
        size <= e->size || e->arena_offset > arena->buffer_offset ||
        e->size != arena->buffer_offset - e->arena_offset ||
        e->arena_offset > arena->buffer_size ||
        size > arena->buffer_size - e->arena_offset) {
        return NULL;
    }
    if (!r->vertex_arena_coherent[slot] &&
        arena->buffer_offset % r->device_props.limits.nonCoherentAtomSize) {
        return NULL;
    }
    return e;
}

/* A range that unsettled work reads: this draw reads a snapshot of it in an
 * arena (out), or, the arena exhausted, the mirror updated once everything
 * has settled. */
static void vertex_cow_snapshot(PGRAPHState *pg, hwaddr addr,
                                VkDeviceSize size, bool dirty,
                                VertexCowDispatch *out)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    size_t start_bit = addr / TARGET_PAGE_SIZE;
    size_t end_bit = (addr + size) / TARGET_PAGE_SIZE;

    /* Extend the copy downwards so every attribute binding into this range
     * gets a non-negative buffer offset. */
    hwaddr lo = addr;
    for (int a = 0; a < NV2A_VERTEXSHADER_ATTRIBUTES; a++) {
        if (!r->vertex_attr_span[a].size ||
            r->vertex_attribute_to_description_location[a] < 0) {
            continue;
        }
        hwaddr span_page = r->vertex_attr_span[a].addr & TARGET_PAGE_MASK;
        if (span_page >= addr && span_page < addr + size) {
            lo = MIN(lo, r->vertex_attribute_offsets[a]);
        }
    }
    VkDeviceSize copy_size = (addr + size) - lo;

    /* Reuse a snapshot when the range is not dirty: its bytes still equal
     * guest RAM (a newer write invalidates it). */
    VertexCowCacheEntry *hit = dirty ? NULL :
                               vertex_cow_lookup(r, lo, addr + size);
    if (hit) {
        out->cow = true;
        out->lo = hit->addr;
        out->slot = hit->slot;
        out->arena_offset = hit->arena_offset;
        r->vertex_arena_pending = true;
        /* No stale marking: reuse implies !dirty, so the set of
         * mirror-behind pages is unchanged. */
        return;
    }

    if (dirty) {
        /* Snapshots overlapping a guest write hold old content a later
         * contained window would reuse. */
        pgraph_vk_vertex_cow_cache_invalidate(r, lo, copy_size);
    }
    /* Before the arena is picked: a surface download may finish the
     * recording, and under rotation that moves on to the other slot. */
    pgraph_vk_download_surfaces_in_range_if_dirty(pg, lo, copy_size);

    /* Snapshots persist across frames: the arena is append-only, so
     * appending needs no fence wait. Above 3/4 full, recycle only when all
     * submitted readers are already proved complete and this draw holds none. */
    unsigned arena_slot = r->slot & 1;
    StorageBuffer *arena = &r->vertex_arena[arena_slot];
    VertexCowCacheEntry *extend = NULL;
    if (!dirty) {
        extend = vertex_cow_extendable(r, lo, copy_size, arena_slot);
        if (!extend) {
            extend = vertex_cow_extendable(r, lo, copy_size, arena_slot ^ 1);
        }
    }
    if (extend && lo + extend->size > addr &&
        memory_region_test_and_clear_dirty(d->vram, addr,
                                            lo + extend->size - addr,
                                            DIRTY_MEMORY_NV2A)) {
        /* A CPU write during a download/settle must not reuse an older
         * prefix. Only recheck prefix pages that this draw actually reads. */
        dirty = true;
        pgraph_vk_vertex_cow_cache_invalidate(r, lo, copy_size);
        extend = NULL;
    }
    if (extend) {
        arena_slot = extend->slot;
        arena = &r->vertex_arena[arena_slot];
        /* Downloads above may rotate the slot or invalidate the prefix:
         * select it only afterwards. Never overwrite the old allocation. */
        VkDeviceSize tail = copy_size - extend->size;
        nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_1);
        if (!r->vertex_arena_coherent[arena_slot]) {
            r->vertex_arena_dirty_start[arena_slot] =
                MIN(r->vertex_arena_dirty_start[arena_slot], arena->buffer_offset);
        }
        memcpy(arena->mapped + arena->buffer_offset,
               d->vram_ptr + lo + extend->size, tail);
        arena->buffer_offset += tail;
        extend->size = copy_size;
        vertex_cow_remember(r, extend - r->vertex_cow_cache);
        *out = (VertexCowDispatch){
            .cow = true,
            .lo = extend->addr,
            .slot = extend->slot,
            .arena_offset = extend->arena_offset,
        };
        r->vertex_arena_pending = true;
        return;
    }
    /* A new non-coherent snapshot starts in its own flush atom, so making
     * it visible cannot flush an older in-flight reader's atom again. */
    VkDeviceSize alignment = r->vertex_arena_coherent[arena_slot] ? 256 :
        MAX((VkDeviceSize)256, r->device_props.limits.nonCoherentAtomSize);
    VkDeviceSize dst = ROUND_UP(arena->buffer_offset, alignment);
    /* The recording can still read the mirror: retain its consumed marks.
     * Only arena readers are absent here, so no global teardown is needed. */
    if (dst + copy_size > (arena->buffer_size / 4) * 3 &&
        !r->vertex_arena_pending && !r->vertex_arena_used &&
        (int32_t)(r->submit_count - r->settled_submit) <= 0) {
        for (int s = 0; s < 2; s++) {
            r->vertex_arena[s].buffer_offset = 0;
            r->vertex_arena_dirty_start[s] = UINT64_MAX;
            r->vertex_arena_epoch[s]++;
            r->vertex_cow_tail[s] = 0;
        }
        dst = 0;
    }
    if (dst + copy_size > arena->buffer_size) {
        /* Both arenas are append-only and bindings carry their arena index.
         * Use the other unused tail before settling any live readers. */
        unsigned other = arena_slot ^ 1;
        StorageBuffer *spare = &r->vertex_arena[other];
        VkDeviceSize spare_alignment = r->vertex_arena_coherent[other] ? 256 :
            MAX((VkDeviceSize)256, r->device_props.limits.nonCoherentAtomSize);
        VkDeviceSize spare_dst = ROUND_UP(spare->buffer_offset, spare_alignment);
        if (spare_dst <= spare->buffer_size &&
            copy_size <= spare->buffer_size - spare_dst) {
            arena_slot = other;
            arena = spare;
            dst = spare_dst;
        }
    }
    if (dst + copy_size > arena->buffer_size) {
        /* Both arenas exhausted: settle everything (consumed marks clear), then
         * the mirror path is legal again. */
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
        pgraph_vk_settle_all_slots(pg);
        pgraph_vk_update_vertex_ram_buffer(pg, addr, d->vram_ptr + addr,
                                           size);
        return;
    }

    nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_1);
    if (!r->vertex_arena_coherent[arena_slot]) {
        r->vertex_arena_dirty_start[arena_slot] =
            MIN(r->vertex_arena_dirty_start[arena_slot], dst);
    }
    memcpy(arena->mapped + dst, d->vram_ptr + lo, copy_size);
    arena->buffer_offset = dst + copy_size;
    out->cow = true;
    out->lo = lo;
    out->slot = arena_slot;
    out->arena_offset = dst;
    r->vertex_arena_pending = true;
    /* The mirror falls behind only where the guest wrote: a COW driven by
     * stale alone must not re-mark pages, or the stale set feeds itself. */
    if (dirty) {
        bitmap_set(r->vertex_stale_bitmap, start_bit, end_bit - start_bit);
    }
    vertex_cow_store(r, lo, copy_size, dst, arena_slot);
}

/* Point this draw's attribute bindings at their COW snapshots. */
static void vertex_cow_bind_attributes(PGRAPHVkState *r,
                                       const MemorySyncRequirement *merged,
                                       const VertexCowDispatch *dispatch,
                                       int num_syncs)
{
    for (int a = 0; a < NV2A_VERTEXSHADER_ATTRIBUTES; a++) {
        if (!r->vertex_attr_span[a].size ||
            r->vertex_attribute_to_description_location[a] < 0) {
            continue;
        }
        hwaddr span_page = r->vertex_attr_span[a].addr & TARGET_PAGE_MASK;
        for (int i = 0; i < num_syncs; i++) {
            if (dispatch[i].cow && span_page >= merged[i].addr &&
                span_page < merged[i].addr + merged[i].size) {
                r->vertex_attr_cow[a] = true;
                r->vertex_attr_cow_slot[a] = dispatch[i].slot;
                r->vertex_attr_cow_offset[a] =
                    dispatch[i].arena_offset +
                    (r->vertex_attribute_offsets[a] - dispatch[i].lo);
                break;
            }
        }
    }
}

/* Most draws' interleaved attributes already form one connected span.
 * Coalesce that case while scanning, without sorting or copying records.
 * A gap keeps the general sorted, in-place merge. */
static int merge_vertex_sync_ranges(MemorySyncRequirement *ranges, int count)
{
    assert(count > 0 && count <= NV2A_VERTEXSHADER_ATTRIBUTES);
    hwaddr lo = ranges[0].addr;
    hwaddr hi = lo + ranges[0].size;
    int i;
    for (i = 1; i < count; i++) {
        hwaddr end = ranges[i].addr + ranges[i].size;
        if (ranges[i].addr > hi || end < lo) {
            break;
        }
        lo = MIN(lo, ranges[i].addr);
        hi = MAX(hi, end);
    }
    if (i == count) {
        if (ranges[0].addr != lo || ranges[0].size != hi - lo) {
            ranges[0] = (MemorySyncRequirement){ lo, hi - lo };
        }
        return 1;
    }
    if (count > 1) {
        qsort(ranges, count,
              sizeof(MemorySyncRequirement),
              compare_memory_sync_requirement_by_addr);
    }

    /* Compact into the sorted input: each output is at or behind the input
     * being consumed, so no separate range array is needed. */
    int num_syncs = 1;

    for (int i = 1; i < count; i++) {
        MemorySyncRequirement *p = &ranges[num_syncs - 1];
        MemorySyncRequirement *t = &ranges[i];

        if (t->addr <= (p->addr + p->size)) {
            // Merge with previous
            hwaddr p_end_addr = p->addr + p->size;
            hwaddr t_end_addr = t->addr + t->size;
            hwaddr new_end_addr = MAX(p_end_addr, t_end_addr);
            p->size = new_end_addr - p->addr;
        } else {
            if (num_syncs != i) {
                ranges[num_syncs] = *t;
            }
            num_syncs++;
        }
    }

    return num_syncs;
}

static void sync_vertex_ram_buffer(PGRAPHState *pg)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (r->num_vertex_ram_buffer_syncs == 0) {
        return;
    }

    // Align sync requirements to page boundaries
    NV2A_VK_DGROUP_BEGIN("Sync vertex RAM buffer");

    for (int i = 0; i < r->num_vertex_ram_buffer_syncs; i++) {
        NV2A_VK_DPRINTF("Need to sync vertex memory @%" HWADDR_PRIx
                        ", %" HWADDR_PRIx " bytes",
                        r->vertex_ram_buffer_syncs[i].addr,
                        r->vertex_ram_buffer_syncs[i].size);

        hwaddr start_addr =
            r->vertex_ram_buffer_syncs[i].addr & TARGET_PAGE_MASK;
        hwaddr end_addr = r->vertex_ram_buffer_syncs[i].addr +
                          r->vertex_ram_buffer_syncs[i].size;
        end_addr = ROUND_UP(end_addr, TARGET_PAGE_SIZE);

        NV2A_VK_DPRINTF("- %d: %08" HWADDR_PRIx " %zd bytes"
                          " -> %08" HWADDR_PRIx " %zd bytes", i,
                        r->vertex_ram_buffer_syncs[i].addr,
                        r->vertex_ram_buffer_syncs[i].size, start_addr,
                        end_addr - start_addr);

        r->vertex_ram_buffer_syncs[i].addr = start_addr;
        r->vertex_ram_buffer_syncs[i].size = end_addr - start_addr;
    }

    MemorySyncRequirement *merged = r->vertex_ram_buffer_syncs;
    int num_syncs = merge_vertex_sync_ranges(
        merged, r->num_vertex_ram_buffer_syncs);

    if (num_syncs < r->num_vertex_ram_buffer_syncs) {
        NV2A_VK_DPRINTF("Reduced to %d sync checks", num_syncs);
    }

    /* A range needing upload (guest-dirty, or mirror stale) goes to the
     * mirror unless unsettled work reads one of its pages; then it is copied
     * into an arena, and only this draw's bindings point at the copy. */
    VertexCowDispatch dispatch[NV2A_VERTEXSHADER_ATTRIBUTES];
    memset(dispatch, 0, num_syncs * sizeof(dispatch[0]));

    for (int i = 0; i < num_syncs; i++) {
        hwaddr addr = merged[i].addr;
        VkDeviceSize size = merged[i].size;
        size_t start_bit = addr / TARGET_PAGE_SIZE;
        size_t end_bit = (addr + size) / TARGET_PAGE_SIZE;

        NV2A_VK_DPRINTF("- %d: %08"HWADDR_PRIx" %zd bytes", i, addr, size);

        bool dirty = memory_region_test_and_clear_dirty(d->vram, addr, size,
                                                        DIRTY_MEMORY_NV2A);
        bool stale = find_next_bit(r->vertex_stale_bitmap, end_bit,
                                   start_bit) < end_bit;
        bool consumed = find_next_bit(r->vertex_consumed_bitmap, end_bit,
                                      start_bit) < end_bit;

        if (dirty || stale) {
            if (!consumed) {
                pgraph_vk_update_vertex_ram_buffer(pg, addr,
                                                   d->vram_ptr + addr, size);
            } else {
                vertex_cow_snapshot(pg, addr, size, dirty, &dispatch[i]);
            }
        }
        bitmap_set(r->vertex_consumed_bitmap, start_bit, end_bit - start_bit);
    }

    vertex_cow_bind_attributes(r, merged, dispatch, num_syncs);

    r->num_vertex_ram_buffer_syncs = 0;

    NV2A_VK_DGROUP_END();
}

void pgraph_vk_clear_surface(NV2AState *d, uint32_t parameter)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    nv2a_profile_inc_counter(NV2A_PROF_CLEAR);

    bool write_color = (parameter & NV097_CLEAR_SURFACE_COLOR);
    bool write_zeta =
        (parameter & (NV097_CLEAR_SURFACE_Z | NV097_CLEAR_SURFACE_STENCIL));

    pg->clearing = true;
    /* Surface uploads must know whether this clear replaces all contents. */
    r->clear_parameter = parameter;

    // FIXME: If doing a full surface clear, mark the surface for full clear
    // and we can just do the clear as part of the surface load.
    pgraph_vk_surface_update(d, true, write_color, write_zeta);

    SurfaceBinding *binding = r->color_binding ?: r->zeta_binding;
    if (!binding) {
        /* Nothing bound to clear */
        pg->clearing = false;
        return;
    }

    uint32_t clearrectx = pgraph_reg_r(pg, NV_PGRAPH_CLEARRECTX);
    uint32_t clearrecty = pgraph_reg_r(pg, NV_PGRAPH_CLEARRECTY);

    unsigned int xmin = GET_MASK(clearrectx, NV_PGRAPH_CLEARRECTX_XMIN);
    unsigned int xmax = GET_MASK(clearrectx, NV_PGRAPH_CLEARRECTX_XMAX);
    unsigned int ymin = GET_MASK(clearrecty, NV_PGRAPH_CLEARRECTY_YMIN);
    unsigned int ymax = GET_MASK(clearrecty, NV_PGRAPH_CLEARRECTY_YMAX);

    NV2A_VK_DGROUP_BEGIN("CLEAR min=(%d,%d) max=(%d,%d)%s%s", xmin, ymin, xmax,
                         ymax, write_color ? " color" : "",
                         write_zeta ? " zeta" : "");

    /* The partial-channel clear is a fragment-shaded draw here, while GL's
     * glClear rasterises nothing: keep it out of the cost counts. */
    bool cost_suspended = xemu_cost_blit_suspend(d, &pgraph_vk_cost_ops);

    begin_pre_draw(pg);
    pgraph_vk_begin_debug_marker(r, r->command_buffer,
        RGBA_BLUE, "Clear %08" HWADDR_PRIx,
        binding->vram_addr);
    begin_draw(pg);

    // FIXME: What does hardware do when min >= max?
    // FIXME: What does hardware do when min >= surface size?
    xmin = MIN(xmin, binding->width - 1);
    ymin = MIN(ymin, binding->height - 1);
    xmax = MAX(xmin, MIN(xmax, binding->width - 1));
    ymax = MAX(ymin, MIN(ymax, binding->height - 1));

    unsigned int scissor_width = MAX(0, xmax - xmin + 1);
    unsigned int scissor_height = MAX(0, ymax - ymin + 1);

    pgraph_apply_anti_aliasing_factor(pg, &xmin, &ymin);
    pgraph_apply_anti_aliasing_factor(pg, &scissor_width, &scissor_height);

    pgraph_apply_scaling_factor(pg, &xmin, &ymin);
    pgraph_apply_scaling_factor(pg, &scissor_width, &scissor_height);

    VkClearRect clear_rect = {
        .rect = {
            .offset = { .x = xmin, .y = ymin },
            .extent = { .width = scissor_width, .height = scissor_height },
        },
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    int num_attachments = 0;
    VkClearAttachment attachments[2];

    if (write_color && r->color_binding) {
        const bool clear_all_color_channels =
            (parameter & NV097_CLEAR_SURFACE_COLOR) ==
            (NV097_CLEAR_SURFACE_R | NV097_CLEAR_SURFACE_G |
             NV097_CLEAR_SURFACE_B | NV097_CLEAR_SURFACE_A);

        if (clear_all_color_channels) {
            attachments[num_attachments] = (VkClearAttachment){
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .colorAttachment = 0,
            };
            pgraph_get_clear_color(
                pg, attachments[num_attachments].clearValue.color.float32);
            num_attachments++;
        } else {
            float blend_constants[4];
            pgraph_get_clear_color(pg, blend_constants);
            vkCmdSetScissor(r->command_buffer, 0, 1, &clear_rect.rect);
            vkCmdSetBlendConstants(r->command_buffer, blend_constants);
            vkCmdDraw(r->command_buffer, 3, 1, 0, 0);
            /* The clear set the blend constants: re-emit at the next draw. */
            r->dyn_last.valid = false;
        }
    }

    if (write_zeta && r->zeta_binding) {
        int stencil_value = 0;
        float depth_value = 1.0;
        pgraph_get_clear_depth_stencil_value(pg, &depth_value, &stencil_value);

        VkImageAspectFlags aspect = 0;
        if (parameter & NV097_CLEAR_SURFACE_Z) {
            aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        if ((parameter & NV097_CLEAR_SURFACE_STENCIL) &&
            (r->zeta_binding->host_fmt.aspect & VK_IMAGE_ASPECT_STENCIL_BIT)) {
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        attachments[num_attachments++] = (VkClearAttachment){
            .aspectMask = aspect,
            .clearValue.depthStencil.depth = depth_value,
            .clearValue.depthStencil.stencil = stencil_value,
        };
    }

    if (num_attachments) {
        vkCmdClearAttachments(r->command_buffer, num_attachments, attachments,
                              1, &clear_rect);
    }
    end_draw(pg);
    pgraph_vk_end_debug_marker(r, r->command_buffer);

    if (cost_suspended) {
        xemu_cost_blit_resume(d, &pgraph_vk_cost_ops);
    }

    /* still clearing: the write masks do not apply (set_surface_dirty) */
    pgraph_vk_set_surface_dirty(pg, write_color, write_zeta);
    pg->clearing = false;

    NV2A_VK_DGROUP_END();
}

#if 0
static void pgraph_vk_debug_attrs(NV2AState *d)
{
    for (int vertex_idx = 0; vertex_idx < pg->draw_arrays_count[i]; vertex_idx++) {
        NV2A_VK_DGROUP_BEGIN("Vertex %d+%d", pg->draw_arrays_start[i], vertex_idx);
        for (int attr_idx = 0; attr_idx < NV2A_VERTEXSHADER_ATTRIBUTES; attr_idx++) {
            VertexAttribute *attr = &pg->vertex_attributes[attr_idx];
            if (attr->count) {
                char *p = (char *)d->vram_ptr + r->attribute_offsets[attr_idx] + (pg->draw_arrays_start[i] + vertex_idx) * attr->stride;
                NV2A_VK_DGROUP_BEGIN("Attribute %d data at %tx", attr_idx, (ptrdiff_t)(p - (char*)d->vram_ptr));
                for (int count_idx = 0; count_idx < attr->count; count_idx++) {
                    switch (attr->format) {
                    case NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F:
                        NV2A_VK_DPRINTF("[%d] %f", count_idx, *(float*)p);
                        p += sizeof(float);
                        break;
                    default:
                        assert(0);
                        break;
                    }
                }
                NV2A_VK_DGROUP_END();
            }
        }
        NV2A_VK_DGROUP_END();
    }
}
#endif

static void bind_vertex_buffer(PGRAPHState *pg, uint16_t inline_map,
                               VkDeviceSize offset)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    r->vertex_arena_pending = false;

    if (r->num_active_vertex_binding_descriptions == 0) {
        return;
    }

    VkBuffer buffers[NV2A_VERTEXSHADER_ATTRIBUTES];
    VkDeviceSize offsets[NV2A_VERTEXSHADER_ATTRIBUTES];

    for (int i = 0; i < r->num_active_vertex_binding_descriptions; i++) {
        int attr_idx = r->vertex_attribute_descriptions[i].location;
        if (inline_map & (1 << attr_idx)) {
            buffers[i] = r->storage_buffers[BUFFER_VERTEX_INLINE].buffer;
            offsets[i] = offset + r->vertex_attribute_offsets[attr_idx];
        } else if (r->vertex_attr_cow[attr_idx]) {
            buffers[i] =
                r->vertex_arena[r->vertex_attr_cow_slot[attr_idx]].buffer;
            offsets[i] = r->vertex_attr_cow_offset[attr_idx];
            r->vertex_arena_used = true;
        } else {
            buffers[i] = r->storage_buffers[BUFFER_VERTEX_RAM].buffer;
            offsets[i] = offset + r->vertex_attribute_offsets[attr_idx];
            /* Self-healing consumed marks: a teardown between sync and
             * bind cleared them; restore before this draw records. */
            if (r->vertex_attr_span[attr_idx].size) {
                size_t sb = (r->vertex_attr_span[attr_idx].addr &
                             TARGET_PAGE_MASK) / TARGET_PAGE_SIZE;
                size_t eb = TARGET_PAGE_ALIGN(
                                r->vertex_attr_span[attr_idx].addr +
                                r->vertex_attr_span[attr_idx].size) /
                            TARGET_PAGE_SIZE;
                bitmap_set(r->vertex_consumed_bitmap, sb, eb - sb);
            }
        }
    }

    vkCmdBindVertexBuffers(r->command_buffer, 0,
                           r->num_active_vertex_binding_descriptions, buffers,
                           offsets);
}

static void bind_inline_vertex_buffer(PGRAPHState *pg, VkDeviceSize offset)
{
    bind_vertex_buffer(pg, 0xffff, offset);
}

void pgraph_vk_set_surface_dirty(PGRAPHState *pg, bool color, bool zeta)
{
    NV2A_DPRINTF("pgraph_set_surface_dirty(%d, %d) -- %d %d\n", color, zeta,
                 pgraph_color_write_enabled(pg), pgraph_zeta_write_enabled(pg));

    PGRAPHVkState *r = pg->vk_renderer_state;

    /* A draw writes only what the write masks let through. A clear writes
     * whatever they say: the NV2A ignores them for CLEAR_SURFACE
     * (nxdk_pgraph_tests Clear, C00000000_Depth_n_ZB on hardware: depth
     * cleared with depth and stencil writes off, the colour with every
     * channel masked), as both renderers' clears do, so it leaves the
     * surface ahead of RAM all the same. */
    if (!pg->clearing) {
        color = color && pgraph_color_write_enabled(pg);
        zeta = zeta && pgraph_zeta_write_enabled(pg);
    }
    pg->surface_color.draw_dirty |= color;
    pg->surface_zeta.draw_dirty |= zeta;

    /* RAM is behind the bindings now, before the method completes
     * (PGRAPHCpuAccess) */
    if (r->color_binding) {
        if (color) {
            pgraph_cpu_ahead_mark(pg, r->color_binding->vram_addr,
                                  r->color_binding->size);
            pgraph_vk_invalidate_bound_textures(pg, r->color_binding->vram_addr,
                                                r->color_binding->size);
        }
        r->color_binding->draw_dirty |= color;
        r->color_binding->frame_time = pg->frame_time;
        r->color_binding->cleared = false;
    }

    if (r->zeta_binding) {
        if (zeta) {
            pgraph_cpu_ahead_mark(pg, r->zeta_binding->vram_addr,
                                  r->zeta_binding->size);
            pgraph_vk_invalidate_bound_textures(pg, r->zeta_binding->vram_addr,
                                                r->zeta_binding->size);
        }
        r->zeta_binding->draw_dirty |= zeta;
        r->zeta_binding->frame_time = pg->frame_time;
        r->zeta_binding->cleared = false;
    }
}

static bool ensure_buffer_space(PGRAPHState *pg, int index, VkDeviceSize size)
{
    if (!pgraph_vk_buffer_has_space_for(pg, index, size, 1)) {
        pgraph_vk_finish(pg, VK_FINISH_REASON_NEED_BUFFER_SPACE);
        /* The ring is empty now; a request it cannot hold grows it. */
        pgraph_vk_buffer_ensure_capacity(pg, index, size);
        return true;
    }

    return false;
}

static void get_size_and_count_for_format(VkFormat fmt, size_t *size, size_t *count)
{
    static const struct {
        size_t size;
        size_t count;
    } table[] = {
        [VK_FORMAT_R8_UNORM] =              { 1, 1 },
        [VK_FORMAT_R8G8_UNORM] =            { 1, 2 },
        [VK_FORMAT_R8G8B8_UNORM] =          { 1, 3 },
        [VK_FORMAT_R8G8B8A8_UNORM] =        { 1, 4 },
        [VK_FORMAT_B8G8R8A8_UNORM] =        { 1, 4 },
        [VK_FORMAT_R16_SNORM] =             { 2, 1 },
        [VK_FORMAT_R16G16_SNORM] =          { 2, 2 },
        [VK_FORMAT_R16G16B16_SNORM] =       { 2, 3 },
        [VK_FORMAT_R16G16B16A16_SNORM] =    { 2, 4 },
        [VK_FORMAT_R16_SSCALED] =           { 2, 1 },
        [VK_FORMAT_R16G16_SSCALED] =        { 2, 2 },
        [VK_FORMAT_R16G16B16_SSCALED] =     { 2, 3 },
        [VK_FORMAT_R16G16B16A16_SSCALED] =  { 2, 4 },
        [VK_FORMAT_R32_SFLOAT] =            { 4, 1 },
        [VK_FORMAT_R32G32_SFLOAT] =         { 4, 2 },
        [VK_FORMAT_R32G32B32_SFLOAT] =      { 4, 3 },
        [VK_FORMAT_R32G32B32A32_SFLOAT] =   { 4, 4 },
        [VK_FORMAT_R32_SINT] =              { 4, 1 },
    };

    assert(fmt < ARRAY_SIZE(table));
    assert(table[fmt].size);

    *size = table[fmt].size;
    *count = table[fmt].count;
}

typedef struct VertexBufferRemap {
    uint16_t attributes;
    size_t buffer_space_required;
    struct {
        VkDeviceAddress offset;
        VkDeviceSize old_stride;
        /* All reachable vertex formats have at most 16 bytes. Pack the
         * stride and conversion fields into the former 64-bit stride slot. */
        uint8_t new_stride;
        uint8_t read_size;
        uint8_t convert;
    } map[NV2A_VERTEXSHADER_ATTRIBUTES];
} VertexBufferRemap;

static VertexBufferRemap remap_unaligned_attributes(PGRAPHState *pg,
                                                    uint32_t num_vertices)
{
    PGRAPHVkState *r = pg->vk_renderer_state;

    VertexBufferRemap remap = {0};

    VkDeviceAddress output_offset = 0;

    for (int attr_id = 0; attr_id < NV2A_VERTEXSHADER_ATTRIBUTES; attr_id++) {
        int desc_loc = r->vertex_attribute_to_description_location[attr_id];
        if (desc_loc < 0) {
            continue;
        }

        VkVertexInputBindingDescription *desc =
            &r->vertex_binding_descriptions[desc_loc];
        VkVertexInputAttributeDescription *attr =
            &r->vertex_attribute_descriptions[desc_loc];

        size_t element_size, element_count;
        get_size_and_count_for_format(attr->format, &element_size, &element_count);

        bool offset_valid =
            (r->vertex_attribute_offsets[attr_id] % element_size == 0);
        bool stride_valid = (desc->stride % element_size == 0);

        enum VertexConvert convert = VTX_CONVERT_NONE;
        VkFormat substitute = VK_FORMAT_UNDEFINED;
        if (r->vertex_conversion_required && r->vertex_format_conversion[attr->format]) {
            substitute = pgraph_vk_vertex_substitute_format(attr->format, &convert);
        }
        if (offset_valid && stride_valid && substitute == VK_FORMAT_UNDEFINED) {
            continue;
        }

        size_t read_size = element_size * element_count;
        if (substitute != VK_FORMAT_UNDEFINED) {
            get_size_and_count_for_format(substitute, &element_size, &element_count);
            attr->format = substitute;
        }

        remap.attributes |= 1 << attr_id;
        remap.map[attr_id].offset = ROUND_UP(output_offset, element_size);
        remap.map[attr_id].old_stride = desc->stride;
        remap.map[attr_id].new_stride = element_size * element_count;
        remap.map[attr_id].read_size = read_size;
        remap.map[attr_id].convert = convert;

        // fprintf(stderr,
        //         "attr %02d remapped: "
        //         "%08" HWADDR_PRIx "->%08" HWADDR_PRIx " "
        //         "stride=%d->%zd\n",
        //         attr_id, r->vertex_attribute_offsets[attr_id],
        //         remap.map[attr_id].offset,
        //         remap.map[attr_id].old_stride,
        //         (size_t)remap.map[attr_id].new_stride);

        output_offset =
            remap.map[attr_id].offset +
            (VkDeviceSize)remap.map[attr_id].new_stride * num_vertices;
        desc->stride = remap.map[attr_id].new_stride;
    }

    remap.buffer_space_required = output_offset;

    // reserve space
    if (remap.attributes) {
        StorageBuffer *buffer = &r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING];
        VkDeviceSize starting_offset = ROUND_UP(buffer->buffer_offset, 16);
        size_t total_space_required =
            (starting_offset - buffer->buffer_offset) + remap.buffer_space_required;
        ensure_buffer_space(pg, BUFFER_VERTEX_INLINE_STAGING, total_space_required);
        buffer->buffer_offset = ROUND_UP(buffer->buffer_offset, 16);
    }

    return remap;
}

static void copy_remapped_attributes_to_inline_buffer(PGRAPHState *pg,
                                                      const VertexBufferRemap *remap,
                                                      uint32_t start_vertex,
                                                      uint32_t end_vertex)
{
    NV2AState *d = container_of(pg, NV2AState, pgraph);
    PGRAPHVkState *r = pg->vk_renderer_state;
    StorageBuffer *buffer = &r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING];

    if (!remap->attributes) {
        return;
    }

    assert(pgraph_vk_buffer_has_space_for(pg, BUFFER_VERTEX_INLINE_STAGING,
                                          remap->buffer_space_required, 256));

    // FIXME: Caching
    /* [start_vertex, end_vertex), with the original indices preserved:
     * reserve the prefix, but do not read or fill vertices the draw skips. */
    assert(start_vertex <= end_vertex);
    assert(buffer->mapped);
    const uint32_t copy_count = end_vertex - start_vertex;
    /* Do not form a source pointer beyond the last element for an empty
     * range: its trailing stride need not lie within guest RAM. */
    const uint32_t first_vertex = copy_count ? start_vertex : 0;

    /* Dispatch once per attribute, so each small gather copy has a constant
     * width and the compiler can emit unaligned loads/stores directly. */
#define COPY_REMAPPED_WIDTH(n)                                         \
    do {                                                              \
        for (uint32_t vertex_id = 0; vertex_id < copy_count; vertex_id++) { \
            memcpy(out_ptr, in_ptr, (n));                              \
            out_ptr += (n);                                          \
            in_ptr += stride;                                        \
        }                                                             \
    } while (0)

    // Copy vertex data
    for (int attr_id = 0; attr_id < NV2A_VERTEXSHADER_ATTRIBUTES; attr_id++) {
        if (!(remap->attributes & (1 << attr_id))) {
            continue;
        }

        VkDeviceSize attr_buffer_offset =
            buffer->buffer_offset + remap->map[attr_id].offset;

        size_t width = remap->map[attr_id].new_stride;
        size_t stride = remap->map[attr_id].old_stride;
        uint8_t *out_ptr = buffer->mapped + attr_buffer_offset +
                           (size_t)first_vertex * width;
        uint8_t *in_ptr = d->vram_ptr + r->vertex_attribute_offsets[attr_id] +
                          (size_t)first_vertex * stride;

        if (remap->map[attr_id].convert != VTX_CONVERT_NONE) {
            pgraph_vk_convert_vertex_attribute(
                out_ptr, in_ptr, copy_count, stride, width,
                remap->map[attr_id].read_size, remap->map[attr_id].convert);
        } else if (stride == width) {
            /* An unaligned base can require remapping even when the vertex
             * elements are packed. Copy that span once, without gathering. */
            if (copy_count) {
                memcpy(out_ptr, in_ptr, width * copy_count);
            }
        } else {
            switch (width) {
            case 1: COPY_REMAPPED_WIDTH(1); break;
            case 2: COPY_REMAPPED_WIDTH(2); break;
            case 3: COPY_REMAPPED_WIDTH(3); break;
            case 4: COPY_REMAPPED_WIDTH(4); break;
            case 6: COPY_REMAPPED_WIDTH(6); break;
            case 8: COPY_REMAPPED_WIDTH(8); break;
            case 12: COPY_REMAPPED_WIDTH(12); break;
            case 16: COPY_REMAPPED_WIDTH(16); break;
            default:
                COPY_REMAPPED_WIDTH(width);
                break;
            }
        }

        r->vertex_attribute_offsets[attr_id] = attr_buffer_offset;
    }


    buffer->buffer_offset += remap->buffer_space_required;
#undef COPY_REMAPPED_WIDTH
}

/* Only devices requiring conversion enter this path. Retain guest dword
 * padding and the source offsets used to capture provoking-vertex values;
 * emit the new layout directly into the existing mapped upload ring.
 * Keep its plan off the native-format draw path's stack. */
static __attribute__((noinline)) VkDeviceSize upload_converted_inline_array(PGRAPHState *pg,
                                                  VkDeviceSize source_size,
                                                  unsigned source_stride,
                                                  unsigned count)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    struct {
        uint16_t source_offset, destination_offset;
        uint8_t source_width, width, convert, format;
    } plan[NV2A_VERTEXSHADER_ATTRIBUTES];
    unsigned destination_stride = 0;
    bool any_convert = false;
    for (int j = 0; j < r->num_active_vertex_attribute_descriptions; j++) {
        VkVertexInputAttributeDescription *attr = &r->vertex_attribute_descriptions[j];
        enum VertexConvert convert = r->vertex_format_conversion[attr->format];
        VkFormat format = attr->format;
        size_t element, components;
        get_size_and_count_for_format(format, &element, &components);
        plan[j].source_width = element * components;
        if (convert != VTX_CONVERT_NONE) {
            format = pgraph_vk_vertex_substitute_format(format, &convert);
            get_size_and_count_for_format(format, &element, &components);
            any_convert = true;
        }
        assert(r->vertex_attribute_offsets[attr->location] <= UINT16_MAX);
        plan[j].source_offset = r->vertex_attribute_offsets[attr->location];
        plan[j].destination_offset = destination_stride;
        plan[j].width = element * components;
        plan[j].convert = convert;
        assert(format <= UINT8_MAX);
        plan[j].format = format;
        destination_stride += ROUND_UP(plan[j].width, 4);
    }
    VkDeviceSize size = any_convert ? (VkDeviceSize)destination_stride * count : source_size;
    ensure_buffer_space(pg, BUFFER_VERTEX_INLINE_STAGING, size);
    if (any_convert) {
        for (int j = 0; j < r->num_active_vertex_attribute_descriptions; j++) {
            VkVertexInputAttributeDescription *attr = &r->vertex_attribute_descriptions[j];
            r->vertex_attribute_offsets[attr->location] = plan[j].destination_offset;
            r->vertex_binding_descriptions[attr->binding].stride = destination_stride;
            attr->format = plan[j].format;
        }
    }
    begin_pre_draw(pg);
    if (!any_convert) {
        void *source = pgraph_inline_array_data(pg);
        return pgraph_vk_update_vertex_inline_buffer(pg, &source, &size, 1);
    }
    StorageBuffer *buffer = &r->storage_buffers[BUFFER_VERTEX_INLINE_STAGING];
    assert(buffer->mapped && pgraph_vk_buffer_has_space_for(
        pg, BUFFER_VERTEX_INLINE_STAGING, size, 1));
    nv2a_profile_inc_counter(NV2A_PROF_GEOM_BUFFER_UPDATE_3);
    VkDeviceSize offset = buffer->buffer_offset;
    buffer->buffer_offset += size;
    for (int j = 0; j < r->num_active_vertex_attribute_descriptions; j++) {
        pgraph_vk_convert_vertex_attribute(
            buffer->mapped + offset + plan[j].destination_offset,
            (const uint8_t *)pgraph_inline_array_data(pg) + plan[j].source_offset,
            count, source_stride, destination_stride, plan[j].source_width,
            plan[j].convert);
    }
    return offset;
}

/* Keep emulated topologies on the original index path. Exclude 0xffff:
 * Metal treats that value as a u16 strip restart even when disabled. */
static bool inline_indices_use_u16(uint32_t primitive, uint32_t max_index)
{
    if (max_index >= 0xffff) {
        return false;
    }
    switch (primitive) {
    case NV097_SET_BEGIN_END_OP_POINTS:
    case NV097_SET_BEGIN_END_OP_LINES:
    case NV097_SET_BEGIN_END_OP_LINE_STRIP:
    case NV097_SET_BEGIN_END_OP_TRIANGLES:
    case NV097_SET_BEGIN_END_OP_TRIANGLE_STRIP:
        return true;
    default:
        return false;
    }
}

void pgraph_vk_flush_draw(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHVkState *r = pg->vk_renderer_state;

    if (!(r->color_binding || r->zeta_binding)) {
        NV2A_VK_DPRINTF("No binding present!!!\n");
        return;
    }

    r->num_vertex_ram_buffer_syncs = 0;

    /* per-vertex cost of the current config (constant for the flush) */
    unsigned long long vcost_milli = 0, vbytes_milli = 0;
    if (vk_cost_probe_active(pg)) {
        vcost_milli = xemu_cost_vert_cost_milli(pg);
        vbytes_milli = xemu_cost_attr_bytes(pg) * 1000ull;
    }

    if (pg->draw_arrays_length) {
        NV2A_VK_DGROUP_BEGIN("Draw Arrays");
        nv2a_profile_inc_counter(NV2A_PROF_DRAW_ARRAYS);

        assert(pg->inline_elements_length == 0);
        assert(pg->inline_buffer_length == 0);
        assert(pg->inline_array_length == 0);

        pgraph_vk_bind_vertex_attributes(d, pg->draw_arrays_min_start,
                                         pg->draw_arrays_max_count - 1, false,
                                         0, pg->draw_arrays_max_count - 1);
        /* DRAW_ARRAYS maintains this bound as batches are appended/connected. */
        uint32_t max_element = pg->draw_arrays_max_count;
        sync_vertex_ram_buffer(pg);
        VertexBufferRemap remap = remap_unaligned_attributes(pg, max_element);

        begin_pre_draw(pg);
        copy_remapped_attributes_to_inline_buffer(
            pg, &remap, pg->draw_arrays_min_start, max_element);
        pgraph_vk_begin_debug_marker(r, r->command_buffer, RGBA_BLUE,
                                     "Draw Arrays");
        begin_draw(pg);
        bind_vertex_buffer(pg, remap.attributes, 0);
        for (int i = 0; i < pg->draw_arrays_length; i++) {
            uint32_t start = pg->draw_arrays_start[i],
                     count = pg->draw_arrays_count[i];
            NV2A_VK_DPRINTF("- [%d] Start:%d Count:%d", i, start, count);
            vkCmdDraw(r->command_buffer, count, 1, start, 0);
            xemu_cost_add_verts(count, vcost_milli, vbytes_milli);
        }
        end_draw(pg);
        pgraph_vk_end_debug_marker(r, r->command_buffer);

        NV2A_VK_DGROUP_END();
    } else if (pg->inline_elements_length) {
        NV2A_VK_DGROUP_BEGIN("Inline Elements");
        assert(pg->inline_buffer_length == 0);
        assert(pg->inline_array_length == 0);

        nv2a_profile_inc_counter(NV2A_PROF_INLINE_ELEMENTS);

        bool use_u16 = inline_indices_use_u16(pg->primitive_mode,
                                               pg->inline_elements_max);
        size_t index_data_size = use_u16 ?
            ROUND_UP(pg->inline_elements_length, 2) * sizeof(uint16_t) :
            pg->inline_elements_length * sizeof(pg->inline_elements[0]);

        ensure_buffer_space(pg, BUFFER_INDEX_STAGING, index_data_size);

        uint32_t min_element = pg->inline_elements_min;
        uint32_t max_element = pg->inline_elements_max;
        pgraph_vk_bind_vertex_attributes(
            d, min_element, max_element, false, 0,
            pg->inline_elements[pg->inline_elements_length - 1]);
        sync_vertex_ram_buffer(pg);
        VertexBufferRemap remap = remap_unaligned_attributes(pg, max_element + 1);

        begin_pre_draw(pg);
        copy_remapped_attributes_to_inline_buffer(pg, &remap, min_element,
                                                  max_element + 1);
        VkDeviceSize buffer_offset = use_u16 ?
            pgraph_vk_update_index_buffer_u16(pg, pg->inline_elements,
                                             pg->inline_elements_length) :
            pgraph_vk_update_index_buffer(pg, pg->inline_elements,
                                          index_data_size);
        pgraph_vk_begin_debug_marker(r, r->command_buffer, RGBA_BLUE,
                                     "Inline Elements");
        begin_draw(pg);
        bind_vertex_buffer(pg, remap.attributes, 0);
        vkCmdBindIndexBuffer(r->command_buffer,
                             r->storage_buffers[BUFFER_INDEX].buffer,
                             buffer_offset, use_u16 ? VK_INDEX_TYPE_UINT16 :
                                                       VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(r->command_buffer, pg->inline_elements_length, 1, 0, 0,
                         0);
        xemu_cost_add_verts(pg->inline_elements_length, vcost_milli,
                            vbytes_milli);
        end_draw(pg);
        pgraph_vk_end_debug_marker(r, r->command_buffer);

        NV2A_VK_DGROUP_END();
    } else if (pg->inline_buffer_length) {
        NV2A_VK_DGROUP_BEGIN("Inline Buffer");
        nv2a_profile_inc_counter(NV2A_PROF_INLINE_BUFFERS);
        assert(pg->inline_array_length == 0);

        size_t vertex_data_size = pg->inline_buffer_length * sizeof(float) * 4;
        void *data[NV2A_VERTEXSHADER_ATTRIBUTES];
        size_t sizes[NV2A_VERTEXSHADER_ATTRIBUTES];
        size_t offset = 0;

        pgraph_vk_bind_vertex_attributes_inline(d);
        for (int i = 0; i < r->num_active_vertex_attribute_descriptions; i++) {
            int attr_index = r->vertex_attribute_descriptions[i].location;

            VertexAttribute *attr = &pg->vertex_attributes[attr_index];
            r->vertex_attribute_offsets[attr_index] = offset;

            data[i] = attr->inline_buffer;
            sizes[i] = vertex_data_size;

            attr->inline_buffer_populated = false;
            offset += vertex_data_size;
        }
        ensure_buffer_space(pg, BUFFER_VERTEX_INLINE_STAGING, offset);

        begin_pre_draw(pg);
        VkDeviceSize buffer_offset = pgraph_vk_update_vertex_inline_buffer(
            pg, data, sizes, r->num_active_vertex_attribute_descriptions);
        pgraph_vk_begin_debug_marker(r, r->command_buffer, RGBA_BLUE,
                                     "Inline Buffer");
        begin_draw(pg);
        bind_inline_vertex_buffer(pg, buffer_offset);
        vkCmdDraw(r->command_buffer, pg->inline_buffer_length, 1, 0, 0);
        xemu_cost_add_verts(pg->inline_buffer_length, vcost_milli,
                            vbytes_milli);
        end_draw(pg);
        pgraph_vk_end_debug_marker(r, r->command_buffer);

        NV2A_VK_DGROUP_END();
    } else if (pg->inline_array_length) {
        NV2A_VK_DGROUP_BEGIN("Inline Array");
        nv2a_profile_inc_counter(NV2A_PROF_INLINE_ARRAYS);

        VkDeviceSize inline_array_data_size = pg->inline_array_length * 4;

        unsigned int offset = 0;
        for (int i = 0; i < NV2A_VERTEXSHADER_ATTRIBUTES; i++) {
            VertexAttribute *attr = &pg->vertex_attributes[i];
            if (attr->count == 0) {
                continue;
            }

            attr->inline_array_offset = offset;
            NV2A_DPRINTF("bind inline attribute %d size=%d, count=%d\n", i,
                         attr->size, attr->count);
            offset += pgraph_inline_attr_size(attr);
        }

        unsigned int vertex_size = offset;
        if (!vertex_size || inline_array_data_size < vertex_size) {
            NV2A_VK_DGROUP_END();
            return;
        }
        unsigned int index_count = pg->inline_array_length * 4 / vertex_size;

        NV2A_DPRINTF("draw inline array %d, %d\n", vertex_size, index_count);
        pgraph_vk_bind_vertex_attributes(d, 0, index_count - 1, true,
                                         vertex_size, index_count - 1);

        VkDeviceSize buffer_offset;
        if (r->vertex_conversion_required) {
            pgraph_materialize_inline_array(pg);
            buffer_offset = upload_converted_inline_array(
                pg, inline_array_data_size, vertex_size, index_count);
        } else {
            ensure_buffer_space(pg, BUFFER_VERTEX_INLINE_STAGING,
                                inline_array_data_size);
            begin_pre_draw(pg);
            if (!pgraph_vk_commit_inline_array(pg, inline_array_data_size, &buffer_offset)) {
                void *inline_array_data = pgraph_inline_array_data(pg);
                buffer_offset = pgraph_vk_update_vertex_inline_buffer(
                    pg, &inline_array_data, &inline_array_data_size, 1);
            }
        }
        pgraph_vk_begin_debug_marker(r, r->command_buffer, RGBA_BLUE,
                                     "Inline Array");
        begin_draw(pg);
        bind_inline_vertex_buffer(pg, buffer_offset);
        vkCmdDraw(r->command_buffer, index_count, 1, 0, 0);
        xemu_cost_add_verts(index_count, vcost_milli, vbytes_milli);
        end_draw(pg);
        pgraph_vk_end_debug_marker(r, r->command_buffer);
        NV2A_VK_DGROUP_END();
    } else {
        NV2A_VK_DPRINTF("EMPTY NV097_SET_BEGIN_END");
        NV2A_UNCONFIRMED("EMPTY NV097_SET_BEGIN_END");
    }
}
