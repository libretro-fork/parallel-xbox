/* SPDX-License-Identifier: GPL-2.0-or-later */
#define VK_NO_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <vulkan/vulkan.h>
#include <GL/gl.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/blend.h"
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define ctz32(v) __builtin_ctz(v)
#define QEMU_BUILD_BUG_ON(x) _Static_assert(!(x), "build assertion")
#define nv2a_profile_inc_counter(...) ((void)0)
typedef struct {unsigned value;} ShaderState;
typedef struct {unsigned format;} RenderPassState;
typedef struct {bool clear;RenderPassState render_pass_state;ShaderState shader_state;VkVertexInputBindingDescription binding_descriptions[16];VkVertexInputAttributeDescription attribute_descriptions[16];uint32_t regs[9];} PipelineKey;
typedef struct {PipelineKey key;} PipelineBinding;
typedef struct {ShaderState state;} ShaderBinding;
typedef struct {
    PipelineBinding *pipeline_binding;ShaderBinding *shader_binding;bool shader_bindings_changed,texture_bindings_changed;
    bool dyn_blend,dyn_cull_front,dyn_vertex_input;VkCommandBuffer command_buffer;
    unsigned num_active_vertex_binding_descriptions,num_active_vertex_attribute_descriptions;
    VkVertexInputBindingDescription vertex_binding_descriptions[16];VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
    struct {bool valid;uint32_t blend,blendcolor,control_0,control_1,control_2,topology,setupraster;} dyn_last;
    struct {bool valid;uint32_t k[13];} draw_state_key;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;bool clearing;unsigned host_format,surface_scale_factor;
    struct {unsigned width,height;} surface_binding_dim;
    struct {unsigned color_format,clip_x,clip_y,clip_width,clip_height;} surface_shape;
    uint32_t regs[0x2000];} PGRAPHState;
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned reg) {assert(reg<ARRAY_SIZE(pg->regs));return pg->regs[reg];}
static bool pgraph_is_reg_dirty(PGRAPHState *pg,unsigned reg) {return false;}
static void init_render_pass_state(PGRAPHState *pg,RenderPassState *state) {state->format=pg->host_format;}
static uint32_t get_primitive_topology(PGRAPHState *pg) {return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;}
static void pgraph_argb_pack32_to_rgba_float(uint32_t packed,float *c) {for(unsigned i=0;i<4;i++) c[i]=0;}
static const unsigned pgraph_cull_face_vk_map[4]={0};
static const unsigned pgraph_depth_func_vk_map[16]={0};
static const unsigned pgraph_stencil_func_vk_map[16]={0};
static const unsigned pgraph_stencil_op_vk_map[16]={0};
/* MAPS */
static unsigned emissions;
static VkColorBlendEquationEXT emitted;
static void vkCmdSetColorBlendEquationEXT(VkCommandBuffer cmd,uint32_t first,uint32_t count,const VkColorBlendEquationEXT *eq) {emitted=*eq;emissions++;}
#define vkCmdSetBlendConstants(...) ((void)0)
#define vkCmdSetCullModeEXT(cmd,cull) ((void)(cull))
#define vkCmdSetFrontFaceEXT(...) ((void)0)
#define vkCmdSetColorWriteMaskEXT(...) ((void)0)
#define vkCmdSetColorBlendEnableEXT(...) ((void)0)
#define vkCmdSetDepthTestEnableEXT(...) ((void)0)
#define vkCmdSetDepthWriteEnableEXT(...) ((void)0)
#define vkCmdSetDepthCompareOpEXT(...) ((void)0)
#define vkCmdSetDepthBoundsTestEnableEXT(...) ((void)0)
#define vkCmdSetStencilTestEnableEXT(...) ((void)0)
#define vkCmdSetStencilOpEXT(...) ((void)0)
#define vkCmdSetStencilCompareMask(...) ((void)0)
#define vkCmdSetStencilWriteMask(...) ((void)0)
#define vkCmdSetStencilReference(...) ((void)0)
#define vkCmdSetPrimitiveTopologyEXT(...) ((void)0)
/* PRODUCTION */
static unsigned gl_emissions;static GLenum gl_src,gl_dst;
#ifndef REAL_GL
static void test_gl_blend_func(GLenum sf,GLenum df) {gl_src=sf;gl_dst=df;gl_emissions++;}
#define glBlendFunc test_gl_blend_func
#define glColorMask(...) ((void)0)
#define glDepthMask(...) ((void)0)
#define glStencilMask(...) ((void)0)
#define glEnable(...) ((void)0)
#define glDisable(...) ((void)0)
#define glBlendEquation(...) ((void)0)
#define glBlendColor(...) ((void)0)
#endif
/* GL PRODUCTION */
static unsigned oracle(unsigned factor,unsigned format,bool enabled) {
    /* Guest formats 1,2,4,5,6,7 have forced destination alpha. */
    if(enabled && (format==1 || format==2 || (format>=4 && format<=7))) {
        if(factor==6) return 1;
        if(factor==7) return 0;
    }
    return factor;
}
#ifndef REAL_GL
int main(void) {
    static PGRAPHState pg;PGRAPHVkState r={0};PipelineBinding binding={0};ShaderBinding shader={0};
    pg.vk_renderer_state=&r;r.pipeline_binding=&binding;r.shader_binding=&shader;r.dyn_vertex_input=true;
    unsigned cases=0;
    for(unsigned fmt=0;fmt<32;fmt++) for(unsigned enabled=0;enabled<2;enabled++)
    for(unsigned sf=0;sf<16;sf++) for(unsigned df=0;df<16;df++) {
        uint32_t raw=0xa5a50002u|(sf<<4)|(df<<8)|(enabled?8:0);
        uint32_t expected=(raw&~0xff0u)|(oracle(sf,fmt,enabled)<<4)|(oracle(df,fmt,enabled)<<8);
        assert(pgraph_surface_blend(raw,fmt)==expected);
        assert(pgraph_surface_blend(expected,fmt)==expected);
        pg.surface_shape.color_format=fmt;pg.regs[NV_PGRAPH_BLEND]=raw;r.dyn_blend=false;
        init_pipeline_key(&pg,&binding.key);assert(binding.key.regs[0]==(expected&0xfff));
        assert(!check_pipeline_dirty(&pg));
        /* A format-only change can retain the same host VkFormat. */
        pg.surface_shape.color_format=8;
        assert(check_pipeline_dirty(&pg)==((raw&0xfff)!=(expected&0xfff)));
        pg.surface_shape.color_format=fmt;r.dyn_blend=true;
        init_pipeline_key(&pg,&binding.key);assert(!binding.key.regs[0]);
        assert(!check_pipeline_dirty(&pg));
        /* Factor 11 is reserved; mapping tests cover it, host emission does not. */
        if(sf!=11 && df!=11) {
            emissions=0;emit_dynamic_state(&pg,true);assert(emissions==1);
            unsigned es=oracle(sf,fmt,enabled),ed=oracle(df,fmt,enabled);
            assert(emitted.srcColorBlendFactor==pgraph_blend_factor_vk_map[es]);
            assert(emitted.dstColorBlendFactor==pgraph_blend_factor_vk_map[ed]);
            assert(emitted.srcAlphaBlendFactor==emitted.srcColorBlendFactor && emitted.dstAlphaBlendFactor==emitted.dstColorBlendFactor);
            emit_dynamic_state(&pg,false);assert(emissions==1);
            pg.surface_shape.color_format=8;emit_dynamic_state(&pg,false);
            assert(emissions==1+(unsigned)(expected!=raw));
            pg.surface_shape.color_format=fmt;r.draw_state_key.valid=false;gl_emissions=0;
            apply_gl_draw_state(&pg);assert(gl_emissions==enabled);
            if(enabled) {
                assert(gl_src==pgraph_blend_factor_gl_map[es] && gl_dst==pgraph_blend_factor_gl_map[ed]);
            }
            apply_gl_draw_state(&pg);assert(gl_emissions==enabled);
        }
        cases++;
    }
    /* Equivalent factors reuse one static key; dynamic blend leaves both
     * guest factors and target alpha semantics out of the pipeline key. */
    pg.surface_shape.color_format=4;r.dyn_blend=false;pg.regs[NV_PGRAPH_BLEND]=0x168;
    PipelineKey first,second;init_pipeline_key(&pg,&first);
    pg.regs[NV_PGRAPH_BLEND]=0x118;init_pipeline_key(&pg,&second);assert(!memcmp(&first,&second,sizeof(first)));
    puts("surface blend: 32768 mapping/key/format-transition cases, canonical PSO reuse and dynamic-state caching passed");
}
#else
#include "surface-blend-gl.h"
#endif
