/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL clear-pipeline line %d: %s\n",__LINE__,#x); _Exit(1); } } while (0)
#define nv2a_profile_inc_counter(...) ((void)0)
typedef struct { VkFormat color_format,zeta_format; } RenderPassState;
typedef struct { bool initialized,in_shader_read; } SurfaceBinding;
typedef struct { VkRenderPass render_pass; VkPipeline pipeline; unsigned draw_time; bool has_dynamic_line_width; } PipelineBinding;
typedef struct {
    SurfaceBinding *color_binding,*zeta_binding;
    PipelineBinding *pipeline_binding;
    VkRenderPass render_pass;
    VkFramebuffer framebuffer;
    VkCommandBuffer command_buffer;
    unsigned clear_parameter;
    bool framebuffer_dirty,pipeline_binding_changed,in_command_buffer,in_render_pass,in_draw;
    bool host_query_reset,query_in_flight,new_query_needed,cost_recording,dyn_vertex_input;
    struct { bool valid; float line_width; } dyn_last;
    struct { bool valid; } vi_last;
} PGRAPHVkState;
typedef struct {
    PGRAPHVkState *vk_renderer_state;
    bool clearing,zpass_pixel_count_enable;
    unsigned draw_time,line_width;
    float surface_scale_factor;
    struct { unsigned width,height; } surface_binding_dim;
    struct { unsigned clip_x,clip_y,clip_width,clip_height; } surface_shape;
} PGRAPHState;
static unsigned creations,binds,query_ends,descriptors,pushes;
static bool rotate_slot;
static PipelineBinding owned_pipeline;
static PGRAPHVkState *active;
static void end_query(PGRAPHVkState *r){ assert(r->query_in_flight);r->query_in_flight=false;query_ends++; }
static void end_render_pass(PGRAPHVkState *r){
    if(r->in_render_pass){if(r->query_in_flight&&r->host_query_reset)end_query(r);r->cost_recording=false;r->in_render_pass=false;}
}
static void pgraph_vk_reclaim_command_buffer(PGRAPHState *pg,bool wait){
    assert(wait);PGRAPHVkState *r=pg->vk_renderer_state;
    if(rotate_slot){end_render_pass(r);r->in_command_buffer=false;r->framebuffer=VK_NULL_HANDLE;}
}
static void pgraph_vk_surface_ensure_attachment_layout(PGRAPHState *pg,SurfaceBinding *s){(void)pg;s->in_shader_read=false;}
static void init_render_pass_state(PGRAPHState *pg,RenderPassState *s){s->color_format=pg->vk_renderer_state->color_binding?1:0;s->zeta_format=pg->vk_renderer_state->zeta_binding?2:0;}
static VkRenderPass get_render_pass(PGRAPHVkState *r,RenderPassState *s){(void)r;return (VkRenderPass)(uintptr_t)(1+s->color_format+s->zeta_format);}
static VkRenderPass expected_pass(PGRAPHState *pg){RenderPassState s;init_render_pass_state(pg,&s);return get_render_pass(pg->vk_renderer_state,&s);}
static void create_pipeline(PGRAPHState *pg){
    PGRAPHVkState *r=pg->vk_renderer_state;creations++;
    owned_pipeline=(PipelineBinding){.render_pass=expected_pass(pg),.pipeline=(VkPipeline)(uintptr_t)8,.has_dynamic_line_width=!pg->clearing};
    r->pipeline_binding=&owned_pipeline;r->pipeline_binding_changed=true;
}
static void create_clear_pipeline(PGRAPHState *pg){assert(pg->clearing);create_pipeline(pg);}
static void pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg){end_render_pass(pg->vk_renderer_state);}
static void pgraph_vk_ensure_command_buffer(PGRAPHState *pg){pg->vk_renderer_state->in_command_buffer=true;}
static void create_frame_buffer(PGRAPHState *pg){assert(pg->vk_renderer_state->render_pass==expected_pass(pg));assert(pg->vk_renderer_state->in_command_buffer);pg->vk_renderer_state->framebuffer=(VkFramebuffer)(uintptr_t)4;}
static void pgraph_vk_update_descriptor_sets(PGRAPHState *pg){assert(!pg->clearing);}
static void begin_render_pass(PGRAPHState *pg){PGRAPHVkState *r=pg->vk_renderer_state;assert(r->framebuffer&&r->in_command_buffer&&!r->in_render_pass);assert(r->render_pass==expected_pass(pg));r->in_render_pass=true;}
static void vk_cost_close_recorded_queries(PGRAPHVkState *r){r->cost_recording=false;}
static void xemu_cost_backend_interrupt(void){}
static void begin_query(PGRAPHVkState *r){assert(!r->query_in_flight);r->query_in_flight=true;}
static void vk_cost_materialize(PGRAPHState *pg){(void)pg;}
static void vkCmdBindPipeline(VkCommandBuffer cmd,VkPipelineBindPoint point,VkPipeline pipeline){(void)cmd;assert(point==VK_PIPELINE_BIND_POINT_GRAPHICS&&pipeline==active->pipeline_binding->pipeline);binds++;}
static void vkCmdSetViewport(VkCommandBuffer cmd,uint32_t first,uint32_t n,const VkViewport *v){(void)cmd;(void)first;assert(n==1&&v->width&&v->height);}
static void vkCmdSetScissor(VkCommandBuffer cmd,uint32_t first,uint32_t n,const VkRect2D *v){(void)cmd;(void)first;assert(n==1&&v->extent.width&&v->extent.height);}
static void vkCmdSetLineWidth(VkCommandBuffer cmd,float width){(void)cmd;assert(width>0);}
static void pgraph_apply_scaling_factor(PGRAPHState *pg,unsigned *a,unsigned *b){(void)pg;(void)a;(void)b;}
static void pgraph_apply_anti_aliasing_factor(PGRAPHState *pg,unsigned *a,unsigned *b){(void)pg;(void)a;(void)b;}
static float clamp_line_width_to_device_limits(PGRAPHState *pg,float width){(void)pg;return width;}
static void emit_dynamic_state(PGRAPHState *pg,bool force){(void)force;pg->vk_renderer_state->dyn_last.valid=!pg->clearing;}
static void emit_dynamic_vertex_input(PGRAPHState *pg){pg->vk_renderer_state->vi_last.valid=!pg->clearing;}
static void bind_descriptor_sets(PGRAPHState *pg){assert(!pg->clearing);descriptors++;}
static void push_vertex_attr_values(PGRAPHState *pg){assert(!pg->clearing);pushes++;}
/* PRODUCTION */
int main(void){
    unsigned cases=0;assert(NV097_CLEAR_SURFACE_COLOR==0xf0);
    for(unsigned mask=0;mask<256;mask++)for(unsigned surfaces=1;surfaces<4;surfaces++)
    for(unsigned bits=0;bits<1024;bits++)for(unsigned clearing=0;clearing<2;clearing++){
        SurfaceBinding color={.initialized=true,.in_shader_read=true},depth=color;
        PipelineBinding old={.pipeline=(VkPipeline)(uintptr_t)9};
        PGRAPHVkState r={.color_binding=(surfaces&1)?&color:NULL,.zeta_binding=(surfaces&2)?&depth:NULL,
            .pipeline_binding=(bits&1)?&old:NULL,.render_pass=(VkRenderPass)(uintptr_t)1,
            .framebuffer=(VkFramebuffer)(uintptr_t)4,.command_buffer=(VkCommandBuffer)(uintptr_t)3,
            .clear_parameter=mask,.framebuffer_dirty=!!(bits&2),.in_command_buffer=true,
            .in_render_pass=!!(bits&4),.query_in_flight=!!(bits&8),.host_query_reset=!!(bits&16),
            .cost_recording=!!(bits&32),.dyn_vertex_input=true,.dyn_last={.valid=true},.vi_last={.valid=true}};
        PGRAPHState pg={.vk_renderer_state=&r,.clearing=clearing,.zpass_pixel_count_enable=!!(bits&256),.draw_time=55,.line_width=8,.surface_scale_factor=1,
            .surface_binding_dim={8,8},.surface_shape={.clip_width=8,.clip_height=8}};
        if(bits&128)r.render_pass=expected_pass(&pg);
        r.new_query_needed=!!(bits&512);
        active=&r;rotate_slot=!!(bits&64);creations=binds=query_ends=descriptors=pushes=0;
        /* The oracle uses the guest color-nibble, independent of the helper. */
        bool partial=(surfaces&1)&&((mask>>4)&15)&&((mask>>4)&15)!=15;
        bool needs_pipeline=!clearing||partial;
        begin_pre_draw(&pg);begin_draw(&pg);
        assert(creations==(unsigned)needs_pipeline&&binds==(unsigned)needs_pipeline);
        assert(r.in_command_buffer&&r.in_render_pass&&r.in_draw);
        assert(r.query_in_flight==(!clearing&&pg.zpass_pixel_count_enable));
        assert(!color.in_shader_read||!r.color_binding);assert(!depth.in_shader_read||!r.zeta_binding);
        assert(descriptors==(unsigned)!clearing&&pushes==(unsigned)!clearing);
        if(clearing){assert(!r.dyn_last.valid&&!r.vi_last.valid);}
        if(!needs_pipeline)assert(r.pipeline_binding==((bits&1)?&old:NULL));
        end_draw(&pg);assert(!r.in_draw&&r.in_render_pass==!clearing);
        if(clearing&&!needs_pipeline){
            pg.clearing=false;creations=binds=descriptors=pushes=0;
            begin_pre_draw(&pg);begin_draw(&pg);
            assert(creations==1&&binds==1&&descriptors==1&&pushes==1);
            assert(r.in_draw&&r.in_render_pass&&r.dyn_last.valid&&r.vi_last.valid);
            end_draw(&pg);
        }
        cases++;
    }
    printf("Pipeline-free attachment clears: %u masks/attachments/first-clear/query/slot/draw-transition cases PASS\n",cases);
    return 0;
}
