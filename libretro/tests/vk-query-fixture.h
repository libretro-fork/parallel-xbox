/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define XEMU_COST_SEGS 1024
enum { NV2A_PROF_QUERY, NV2A_PROF_PIPELINE_RENDERPASSES, NV2A_PROF_PIPELINE_BIND };
typedef struct { VkPipeline pipeline;unsigned draw_time;bool has_dynamic_line_width; } PipelineBinding;
typedef struct {
    VkDevice device;VkCommandBuffer command_buffer;VkRenderPass render_pass;VkFramebuffer framebuffer;
    VkQueryPool query_pool,cost_stats_pool,cost_occl_pool;
    bool host_query_reset,in_command_buffer,in_render_pass,in_draw;
    bool query_in_flight,query_in_render_pass,new_query_needed;
    int num_queries_in_flight,max_queries_in_flight;
    PipelineBinding *pipeline_binding;bool pipeline_binding_changed,dyn_vertex_input;
    struct {bool valid;float line_width;} dyn_last;
    bool cost_recording,cost_recording_samples,cost_pools_reset,cost_pending_samples;
    int cost_recording_slot,cost_pending_slot;
    bool cost_seg_recorded[XEMU_COST_SEGS],cost_seg_recorded_samples[XEMU_COST_SEGS];
} PGRAPHVkState;
typedef struct {
    PGRAPHVkState *vk_renderer_state;bool clearing,zpass_pixel_count_enable;
    unsigned draw_time,line_width,surface_scale_factor;
    struct {unsigned width,height;} surface_binding_dim;
    struct {unsigned clip_x,clip_y,clip_width,clip_height;} surface_shape;
} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
static unsigned counters[3],cost_slots,interruptions;
static bool cost_model,xemu_cost_q_open;
static int pgraph_vk_cost_ops;
static bool xemu_cost_model_active(void) {return cost_model;}
static void nv2a_profile_inc_counter(unsigned n) {assert(n<3);counters[n]++;}
static void xemu_cost_backend_interrupt(void) {xemu_cost_q_open=false;interruptions++;}
static void xemu_cost_samples_interrupted(void) {interruptions++;}
static void xemu_cost_draw_begin(NV2AState *d,int *ops) {
    PGRAPHVkState *r=d->pgraph.vk_renderer_state;assert(cost_slots<XEMU_COST_SEGS);
    r->cost_pending_slot=cost_slots++;r->cost_pending_samples=!r->query_in_flight;xemu_cost_q_open=true;
}
static void pgraph_apply_scaling_factor(PGRAPHState *pg,unsigned *w,unsigned *h) {*w*=pg->surface_scale_factor;*h*=pg->surface_scale_factor;}
static void pgraph_apply_anti_aliasing_factor(PGRAPHState *pg,unsigned *w,unsigned *h) {}
static float clamp_line_width_to_device_limits(PGRAPHState *pg,float width) {return width;}
static void emit_dynamic_state(PGRAPHState *pg,bool force) {}
static void emit_dynamic_vertex_input(PGRAPHState *pg) {}
static void bind_descriptor_sets(PGRAPHState *pg) {}
static void push_vertex_attr_values(PGRAPHState *pg) {}
