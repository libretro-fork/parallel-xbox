/*
 * Geforce NV2A PGRAPH OpenGL Renderer
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2015 Jannik Vogel
 * Copyright (c) 2018-2025 Matt Borgerson
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
#include "hw/xbox/nv2a/pgraph/pgraph.h"
#include "debug.h"
#include "renderer.h"
#include "qemu/fast-hash.h"
#include "ui/xemu-settings.h"

GloContext *g_nv2a_context_render;

static void early_context_init(void)
{
    g_nv2a_context_render = glo_context_create_4_1();
}

static void pgraph_gl_init(NV2AState *d, Error **errp)
{
    PGRAPHState *pg = &d->pgraph;

    pg->gl_renderer_state = g_malloc0(sizeof(*pg->gl_renderer_state));
    PGRAPHGLState *r = pg->gl_renderer_state;

    /* fire up opengl (in the core, the frontend's context is there: the
     * machine starts after context_reset) */
    glo_set_current(g_nv2a_context_render);
    if (pgraph_gpu_boost_gl() &&
        glo_check_extension("GL_ARB_parallel_shader_compile")) {
        glMaxShaderCompilerThreadsARB(16);
    }

#if DEBUG_NV2A_GL
    gl_debug_initialize();
#endif

    /* DXT textures */
    if (!glo_check_extension("GL_EXT_texture_compression_s3tc")) {
        fprintf(stderr, "Warning: GL_EXT_texture_compression_s3tc not available\n");
    }
    /*  Internal RGB565 texture format */
    if (!glo_check_extension("GL_ARB_ES2_compatibility")) {
        fprintf(stderr, "Warning: GL_ARB_ES2_compatibility not available\n");
    }

    glGetFloatv(GL_SMOOTH_LINE_WIDTH_RANGE, r->supported_smooth_line_width_range);
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, r->supported_aliased_line_width_range);

    pgraph_gl_init_surfaces(pg);
    pgraph_gl_init_reports(d);
    pgraph_gl_init_textures(d);
    pgraph_gl_init_buffers(d);
    pgraph_gl_init_shaders(pg);
    pgraph_gl_init_display(d);

    pgraph_gl_invalidate_memory_buffer(d);

    pg->uniform_attrs = 0;
    pg->swizzle_attrs = 0;

    r->supported_extensions.texture_filter_anisotropic =
        glo_check_extension("GL_EXT_texture_filter_anisotropic");
    if (r->supported_extensions.texture_filter_anisotropic) {
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,
                    &r->supported_extensions.max_texture_anisotropy);
    }
}

static void pgraph_gl_discard_flip(PGRAPHState *pg)
{
    if (pg->host_flip_pending) {
        /* Closed private cost samples cannot follow a renderer switch. Guest
         * reports retain their own normal drain/finalization path. */
        xemu_cost_segn = 0;
        xemu_cost_q_open = xemu_cost_q2_open = false;
    }
    pg->host_flip_pending = false;
    pg->gl_renderer_state->flip_failed = false;
    pg->gl_renderer_state->flip_display_queued = false;
    glDeleteSync(pg->gl_renderer_state->flip_wait_fence);
    pg->gl_renderer_state->flip_wait_fence = NULL;
    glDeleteSync(pg->gl_renderer_state->flip_fence);
    pg->gl_renderer_state->flip_fence = NULL;
}

static void pgraph_gl_finalize(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;

    glo_set_current(g_nv2a_context_render);

    pgraph_gl_discard_flip(pg);
    pgraph_gl_finalize_surfaces(pg);
    pgraph_gl_finalize_shaders(pg);
    pgraph_gl_finalize_textures(pg);
    pgraph_gl_finalize_reports(pg);
    pgraph_gl_finalize_buffers(pg);
    pgraph_gl_finalize_display(pg);

    glo_set_current(NULL);

    g_free(pg->gl_renderer_state);
    pg->gl_renderer_state = NULL;
}

/* Retire once, after the selected GPU fence is ready. Other surface/report
 * paths retain their existing policies; none runs during an unsuccessful poll. */
static void pgraph_gl_flip_complete(NV2AState *d)
{
    PGRAPHGLState *r = d->pgraph.gl_renderer_state;
    /* Surfaces the guest CPU read this frame (a blocking read that drains
     * the pipeline -- free on the real hardware's unified VRAM) are read
     * back here, at the flip, without waiting where the pack allows: the
     * next guest read finds RAM up to date and no longer blocks. Nothing
     * for games that never read back. */
    SurfaceBinding *s;
    QTAILQ_FOREACH (s, &r->surfaces, entry) {
        if (s->cpu_reads != s->cpu_reads_last_flip) {
            s->cpu_reads_last_flip = s->cpu_reads;
            pgraph_gl_surface_predownload(d, s);
        }
    }
    pgraph_gl_surfaces_refresh_any_dirty(d);
    xemu_cost_resolve_frame(d, &pgraph_gl_cost_ops,
                            r->flip_scale);
}

static bool pgraph_gl_flip_stall_ready(NV2AState *d)
{
    PGRAPHGLState *r = d->pgraph.gl_renderer_state;
    if (r->flip_failed) {
        return false;
    }
    if (r->flip_wait_fence) {
        GLint status = GL_UNSIGNALED;
        glGetSynciv(r->flip_wait_fence, GL_SYNC_STATUS, 1, NULL, &status);
        if (status != GL_SIGNALED) {
            return false;
        }
        sampled_texture_complete(&r->sampled_texture_pool,
                                 r->sampled_flip_wait_serial);
        glDeleteSync(r->flip_wait_fence);
        r->flip_wait_fence = NULL;
    }
    pgraph_gl_flip_complete(d);
    d->pgraph.host_flip_pending = false;
    return true;
}

static void pgraph_gl_flip_stall(NV2AState *d)
{
    PGRAPHGLState *r = d->pgraph.gl_renderer_state;
    assert(!d->pgraph.host_flip_pending && !r->flip_wait_fence);
    r->flip_display_queued = false;

    NV2A_GL_DFRAME_TERMINATOR();
    pgraph_gl_shaders_flip_service(d);
    xemu_cost_frame_close(d, &pgraph_gl_cost_ops);
    r->flip_scale = d->pgraph.surface_scale_factor;

    GLsync current = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!current) {
        /* Without a completion marker, neither query reuse nor further FIFO
         * work is safe. Keep backpressure until renderer reinitialization. */
        r->flip_failed = true;
        d->pgraph.host_flip_pending = true;
        glFlush();
        fprintf(stderr, "nv2a: failed to create a GL flip completion fence\n");
        return;
    }
    if (sampled_texture_rollover(&r->sampled_texture_pool)) {
        r->sampled_flip_serial = r->sampled_flip_wait_serial = 0;
    }
    uint64_t sampled_serial = sampled_texture_seal(&r->sampled_texture_pool);
    if (xemu_cost_segn) {
        /* Query slots cannot be reused until this frame completes. Its
         * fence also covers all preceding work, so the old one is redundant. */
        glDeleteSync(r->flip_fence);
        r->flip_fence = NULL;
        r->flip_wait_fence = current;
        r->sampled_flip_wait_serial = sampled_serial;
    } else {
        /* Preserve the previous-frame bound without waiting on this thread. */
        r->flip_wait_fence = r->flip_fence;
        r->sampled_flip_wait_serial = r->sampled_flip_serial;
        r->sampled_flip_serial = sampled_serial;
        r->flip_fence = current;
    }
    glFlush();
    d->pgraph.host_flip_pending = true;
    /* The readiness callback clears pending on success. Keep this call last
     * so eager retirement can tail-call instead of nesting both stack frames. */
    pgraph_gl_flip_stall_ready(d);
}

static void pgraph_gl_flush(NV2AState *d)
{
    PGRAPHGLState *r = d->pgraph.gl_renderer_state;

    pgraph_gl_surface_flush(d);
    /* The surface list is rebuilt: the display resolve compares surface
     * pointers, so forget the one it published from. */
    r->disp_last_surface = NULL;
    pgraph_gl_mark_textures_possibly_dirty(d, 0, memory_region_size(d->vram));
    pgraph_gl_invalidate_memory_buffer(d);
    pgraph_gl_uniform_sources_invalidate(r);
    /* FIXME: Flush more? */

    pgraph_clear_and_notify(&d->pgraph.flush_complete, &d->pgraph.flush_pending);
}

static void pgraph_gl_process_pending(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    /* Permit one GPU-ordered early display attempt, without waiting for the
     * flip to complete. Coalesce further requests until retirement so repeated
     * frontend callbacks cannot grow the GPU queue. Maintenance still runs. */
    bool display_ready = !pg->host_flip_pending ||
        (!r->flip_failed && !r->flip_display_queued);
    bool scanout = display_ready &&
        (retro_atomic_load_relaxed_int(&pg->scanout_changed) ||
         retro_atomic_load_relaxed_int(&r->disp_check_pending));
    if (retro_atomic_load_relaxed_int(&r->download_dirty_surfaces_pending) ||
        (display_ready && retro_atomic_load_relaxed_int(&pg->sync_pending)) ||
        scanout ||
        retro_atomic_load_relaxed_int(&d->pgraph.flush_pending) ||
        retro_atomic_load_relaxed_int(&r->shader_cache_writeback_pending)) {
        qemu_mutex_unlock(&d->pfifo.lock);
        if (retro_atomic_load_relaxed_int(&r->download_dirty_surfaces_pending)) {
            /* pre-savevm: close the open visibility-test interval and serve
             * every queued report, so each GET_REPORT result is in guest
             * RAM before it is serialized. */
            if (r->gl_zpass_query_open) {
                glEndQuery(GL_SAMPLES_PASSED);
                r->gl_zpass_query_open = false;
            }
            pgraph_gl_drain_pending_reports(d);
            pgraph_gl_download_dirty_surfaces(d);
        }
        if (display_ready && retro_atomic_load_relaxed_int(&pg->sync_pending)) {
            retro_atomic_store_relaxed_int(&d->pgraph.scanout_changed, 0);
            retro_atomic_store_relaxed_int(&r->disp_check_pending, 0);
            pgraph_gl_sync(d);
            if (pg->host_flip_pending) {
                r->flip_display_queued = true;
            }
        } else if (scanout) {
            retro_atomic_store_relaxed_int(&d->pgraph.scanout_changed, 0);
            retro_atomic_store_relaxed_int(&r->disp_check_pending, 0);
            pgraph_gl_present_scanout(d);
            if (pg->host_flip_pending) {
                r->flip_display_queued = true;
            }
        }
        if (retro_atomic_load_relaxed_int(&d->pgraph.flush_pending)) {
            pgraph_gl_flush(d);
        }
        if (retro_atomic_load_relaxed_int(&r->shader_cache_writeback_pending)) {
            pgraph_gl_shader_write_cache_reload_list(&d->pgraph);
        }
        qemu_mutex_lock(&d->pfifo.lock);
    }
}

static void pgraph_gl_pre_savevm_trigger(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    retro_atomic_store_relaxed_int(&r->download_dirty_surfaces_pending, 1);
}

static void pgraph_gl_pre_savevm_wait(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    pgraph_wait_until_clear(&r->dirty_surfaces_download_complete,
                            &r->download_dirty_surfaces_pending);
}

static void pgraph_gl_pre_shutdown_trigger(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    retro_atomic_store_relaxed_int(&r->shader_cache_writeback_pending, 1);
}

static void pgraph_gl_pre_shutdown_wait(NV2AState *d)
{
    PGRAPHState *pg = &d->pgraph;
    PGRAPHGLState *r = pg->gl_renderer_state;

    pgraph_wait_until_clear(&r->shader_cache_writeback_complete,
                            &r->shader_cache_writeback_pending);
}

static PGRAPHRenderer pgraph_gl_renderer = {
    .type = CONFIG_DISPLAY_RENDERER_OPENGL,
    .name = "OpenGL",
    .ops = {
        .init = pgraph_gl_init,
        .early_context_init = early_context_init,
        .finalize = pgraph_gl_finalize,
        .clear_report_value = pgraph_gl_clear_report_value,
        .clear_surface = pgraph_gl_clear_surface,
        .draw_begin = pgraph_gl_draw_begin,
        .draw_end = pgraph_gl_draw_end,
        .flip_stall = pgraph_gl_flip_stall,
        .flip_stall_ready = pgraph_gl_flip_stall_ready,
        .flush_draw = pgraph_gl_flush_draw,
        .get_report = pgraph_gl_get_report,
        .image_blit = pgraph_gl_image_blit,
        .pre_savevm_trigger = pgraph_gl_pre_savevm_trigger,
        .pre_savevm_wait = pgraph_gl_pre_savevm_wait,
        .pre_shutdown_trigger = pgraph_gl_pre_shutdown_trigger,
        .pre_shutdown_wait = pgraph_gl_pre_shutdown_wait,
        .process_pending = pgraph_gl_process_pending,
        .process_pending_reports = pgraph_gl_process_pending_reports,
        .poll_pending_report = pgraph_gl_poll_pending_report,
        .surface_update = pgraph_gl_surface_update,
        .set_surface_scale_factor = pgraph_gl_set_surface_scale_factor,
        .get_surface_scale_factor = pgraph_gl_get_surface_scale_factor,
        .get_framebuffer_surface = pgraph_gl_get_framebuffer_surface,
        .cpu_access_service = pgraph_gl_cpu_access_service,
        .get_gpu_properties = pgraph_gl_get_gpu_properties,
    }
};

static void __attribute__((constructor)) register_renderer(void)
{
    pgraph_renderer_register(&pgraph_gl_renderer);
}
