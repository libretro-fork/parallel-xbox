/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define VK_NULL_HANDLE 0
#define NV097_CLEAR_SURFACE_COLOR 15u
typedef uintptr_t VkRenderPass;
typedef struct { bool in_shader_read, initialized; unsigned view; } Surface;
typedef struct { VkRenderPass render_pass; } Pipeline;
typedef struct { VkRenderPass pass; } RenderPassState;
typedef struct {
    Surface *color_binding, *zeta_binding;
    Pipeline *pipeline_binding;
    struct { bool draw, rendering; } shader_objects;
    bool in_render_pass, framebuffer_dirty;
    uintptr_t framebuffer;
    VkRenderPass render_pass;
    unsigned clear_parameter;
} PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; bool clearing; } PGRAPHState;
static bool next_dynamic, rotate_descriptors;
static unsigned rp_calls, fb_calls, ends, queries_closed, descriptors, clear_pipelines;
static Pipeline ordinary = { 17 }, clear_pipeline = { 23 };
static void pgraph_vk_reclaim_command_buffer(PGRAPHState *pg, bool reuse)
{ (void)pg; assert(reuse); }
static void pgraph_vk_surface_ensure_attachment_layout(PGRAPHState *pg, Surface *s)
{ (void)pg; s->in_shader_read = false; }
static void init_render_pass_state(PGRAPHState *pg, RenderPassState *s)
{ (void)pg; s->pass = 29; }
static VkRenderPass get_render_pass(PGRAPHVkState *r, RenderPassState *s)
{ assert(!r->shader_objects.draw); rp_calls++; return s->pass; }
static void create_clear_pipeline(PGRAPHState *pg)
{ clear_pipelines++; pg->vk_renderer_state->pipeline_binding = &clear_pipeline; }
static void create_pipeline(PGRAPHState *pg)
{
    pg->vk_renderer_state->shader_objects.draw = next_dynamic;
    pg->vk_renderer_state->pipeline_binding = next_dynamic ? NULL : &ordinary;
}
static void pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    if (r->in_render_pass) { ends++; queries_closed++; }
    r->in_render_pass = r->shader_objects.rendering = false;
}
static void pgraph_vk_ensure_command_buffer(PGRAPHState *pg) { (void)pg; }
static void create_frame_buffer(PGRAPHState *pg)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    assert(!r->shader_objects.draw && r->render_pass != VK_NULL_HANDLE);
    fb_calls++; r->framebuffer = 100 + r->render_pass;
}
static void pgraph_vk_update_descriptor_sets(PGRAPHState *pg)
{
    descriptors++;
    if (rotate_descriptors) {
        pgraph_vk_ensure_not_in_render_pass(pg);
        pg->vk_renderer_state->framebuffer = VK_NULL_HANDLE;
        rotate_descriptors = false;
    }
}
/* PRODUCTION CLEAR CLASSIFIER */
/* PRODUCTION PRE-DRAW */
static void open_scope(PGRAPHVkState *r)
{
    r->in_render_pass = true;
    r->shader_objects.rendering = r->shader_objects.draw;
}
int main(void)
{
    Surface color = { .initialized = true, .view = 1 };
    Surface depth = { .initialized = true, .view = 2 };
    PGRAPHVkState r = { .color_binding = &color, .zeta_binding = &depth,
                        .framebuffer_dirty = true };
    PGRAPHState pg = { .vk_renderer_state = &r };
    next_dynamic = true;
    begin_pre_draw(&pg);
    assert(!rp_calls && !fb_calls && !r.framebuffer_dirty && !r.framebuffer);
    open_scope(&r);
    for (unsigned i = 0; i < 10000; i++) begin_pre_draw(&pg);
    assert(!ends && !fb_calls && !rp_calls && r.in_render_pass);
    /* Color/depth replacement, dimension changes and sample-in-place each
     * invalidate an attachment scope, without creating a framebuffer. */
    for (unsigned i = 0; i < 4; i++) {
        unsigned prior = ends;
        r.framebuffer_dirty = true;
        color.in_shader_read = depth.in_shader_read = true;
        begin_pre_draw(&pg);
        assert(ends == prior + 1 && !r.in_render_pass && !r.framebuffer_dirty);
        assert(!color.in_shader_read && !depth.in_shader_read && !fb_calls && !rp_calls);
        open_scope(&r);
    }
    /* Retain cached classic owners; descriptor rotation and a new command
     * buffer may also leave no classic framebuffer at all. */
    r.framebuffer = 777;
    begin_pre_draw(&pg); assert(r.framebuffer == 777);
    rotate_descriptors = true; begin_pre_draw(&pg);
    assert(!r.framebuffer && !r.in_render_pass && !fb_calls);
    begin_pre_draw(&pg); assert(!fb_calls);
    open_scope(&r);
    pg.clearing = true; r.clear_parameter = NV097_CLEAR_SURFACE_COLOR;
    begin_pre_draw(&pg);
    assert(!r.shader_objects.draw && r.render_pass == 29 && r.framebuffer == 129);
    assert(fb_calls == 1 && rp_calls == 1 && !clear_pipelines);
    open_scope(&r);
    pg.clearing = false; next_dynamic = true;
    begin_pre_draw(&pg); assert(!r.render_pass && r.framebuffer == 129 && fb_calls == 1);
    open_scope(&r);
    pg.clearing = true; r.clear_parameter = 1;
    begin_pre_draw(&pg);
    assert(r.render_pass == 23 && r.framebuffer == 123 && clear_pipelines == 1);
    assert(fb_calls == 2);
    /* Pressure fallback has a non-null compatible pass. Rotation after the
     * first lookup must be repaired by the classic-only final lookup. */
    pg.clearing = false; next_dynamic = true;
    begin_pre_draw(&pg); open_scope(&r);
    next_dynamic = false; rotate_descriptors = true;
    begin_pre_draw(&pg);
    assert(r.render_pass == 17 && r.framebuffer == 117 && fb_calls == 4);
    for (unsigned i = 0; i < 10000; i++) {
        next_dynamic = !(i & 1);
        rotate_descriptors = i % 3 == 0;
        r.color_binding = i % 5 ? &color : NULL;
        r.zeta_binding = i % 7 ? &depth : NULL;
        if (!r.color_binding && !r.zeta_binding) r.color_binding = &color;
        r.framebuffer_dirty = i % 11 == 0;
        begin_pre_draw(&pg);
        assert(!r.framebuffer_dirty);
        if (next_dynamic) assert(r.render_pass == VK_NULL_HANDLE);
        else assert(r.render_pass == 17 && r.framebuffer == 117);
        open_scope(&r);
    }
    assert(queries_closed == ends && descriptors == 20011);
    puts("Dynamic rendering: 20013 pre-draws, attachment/clear/fallback/rotation ownership and zero dynamic framebuffer creations PASS");
    return 0;
}
