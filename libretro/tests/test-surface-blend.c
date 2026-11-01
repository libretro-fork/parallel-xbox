/* SPDX-License-Identifier: GPL-2.0-or-later */
#define VK_NO_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <vulkan/vulkan.h>
#include <GL/gl.h>
#include <GL/glext.h>
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
typedef struct {unsigned unused;} Lru, LruNode;
typedef struct {LruNode node;PipelineKey key;VkPipelineLayout layout;VkPipeline pipeline;unsigned draw_time;bool has_dynamic_line_width;} PipelineBinding;
typedef struct {ShaderState state;} ShaderBinding;
typedef struct {
    PipelineBinding *pipeline_binding;ShaderBinding *shader_binding;bool shader_bindings_changed,texture_bindings_changed,ahead_valid;
    bool dyn_blend,dyn_cull_front,dyn_vertex_input;VkCommandBuffer command_buffer;
    unsigned num_active_vertex_binding_descriptions,num_active_vertex_attribute_descriptions;
    VkVertexInputBindingDescription vertex_binding_descriptions[16];VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
    struct {bool valid;uint32_t blend,blendcolor,control_0,control_1,control_2,topology,setupraster;} dyn_last;
    struct {bool valid;uint32_t k[13];} draw_state_key;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;bool clearing;unsigned host_format,surface_scale_factor;
    struct {unsigned width,height;} surface_binding_dim;
    struct {unsigned color_format,clip_x,clip_y,clip_width,clip_height;} surface_shape;
    uint32_t regs[0x2000];bool dirty[0x2000];} PGRAPHState;
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned reg) {assert(reg<ARRAY_SIZE(pg->regs));return pg->regs[reg];}
static bool pgraph_is_reg_dirty(PGRAPHState *pg,unsigned reg) {assert(reg<ARRAY_SIZE(pg->dirty));return pg->dirty[reg];}
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
typedef struct {PGRAPHState pgraph;} NV2AState;
#define container_of(ptr,type,member) ((type*)(void*)(ptr))
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
#define NV2A_VK_DPRINTF(...) ((void)0)
static unsigned key_rebuilds;
static void pgraph_vk_bind_textures(NV2AState*d){(void)d;}
static void pgraph_vk_bind_shaders(PGRAPHState*pg){(void)pg;}
static void pgraph_clear_dirty_reg_map(PGRAPHState*pg){memset(pg->dirty,0,sizeof(pg->dirty));}
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
/* The previous key projection is the compatibility reference. */
static void reference_pipeline_regs(PGRAPHState *pg, uint32_t *out)
{
    PGRAPHVkState *r = pg->vk_renderer_state;
    const int regs[] = {
        NV_PGRAPH_BLEND,       NV_PGRAPH_BLENDCOLOR,  NV_PGRAPH_CONTROL_0,
        NV_PGRAPH_CONTROL_1,   NV_PGRAPH_CONTROL_2,   NV_PGRAPH_CONTROL_3,
        NV_PGRAPH_SETUPRASTER, NV_PGRAPH_ZOFFSETBIAS, NV_PGRAPH_ZOFFSETFACTOR,
    };
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
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(regs) != 9);
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(reg_masks) != ARRAY_SIZE(regs));
    for (int i = 0; i < ARRAY_SIZE(regs); i++) {
        out[i] = pgraph_reg_r(pg, regs[i]) & reg_masks[i];
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
                          NV_PGRAPH_CONTROL_0_ZWRITEENABLE);
        out[3] = 0;
        out[4] = 0;
    }
}

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
    /* Fresh allocations are not zeroed, and recycled line entries may carry
     * true metadata into a clear, which has no dynamic line-width state. */
    PipelineBinding recycled;memset(&recycled,0xa5,sizeof(recycled));recycled.has_dynamic_line_width=true;
    PipelineKey untouched;memcpy(&untouched,&recycled.key,sizeof(untouched));
    pipeline_cache_entry_init(NULL,&recycled.node,NULL);
    assert(recycled.layout==VK_NULL_HANDLE&&recycled.pipeline==VK_NULL_HANDLE&&!recycled.draw_time&&!recycled.has_dynamic_line_width);
    assert(!memcmp(&untouched,&recycled.key,sizeof(untouched)));
    puts("pipeline entry metadata: poisoned/recycled line-width flag reset without clearing key bytes");

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
    const unsigned pipeline_regs[]={NV_PGRAPH_BLEND,NV_PGRAPH_BLENDCOLOR,NV_PGRAPH_CONTROL_0,NV_PGRAPH_CONTROL_1,NV_PGRAPH_CONTROL_2,NV_PGRAPH_CONTROL_3,NV_PGRAPH_SETUPRASTER,NV_PGRAPH_ZOFFSETBIAS,NV_PGRAPH_ZOFFSETFACTOR};
    unsigned reuse_cases=0;uint32_t random=0x529abc31;
    for(unsigned fmt=0;fmt<32;fmt++)for(unsigned features=0;features<8;features++){
        pg.surface_shape.color_format=fmt;r.dyn_blend=features&1;r.dyn_cull_front=features&2;r.dyn_vertex_input=features&4;
        for(unsigned i=0;i<ARRAY_SIZE(pipeline_regs);i++){random^=random<<13;random^=random>>17;random^=random<<5;pg.regs[pipeline_regs[i]]=random;}
        init_pipeline_key(&pg,&binding.key);uint32_t expected_regs[9];reference_pipeline_regs(&pg,expected_regs);assert(!memcmp(expected_regs,binding.key.regs,sizeof(expected_regs)));
        for(unsigned i=0;i<ARRAY_SIZE(pipeline_regs);i++)for(unsigned bit=0;bit<32;bit++){
            unsigned reg=pipeline_regs[i];pg.regs[reg]^=1u<<bit;pg.dirty[reg]=true;
            PipelineKey candidate;init_pipeline_key(&pg,&candidate);reference_pipeline_regs(&pg,expected_regs);assert(!memcmp(expected_regs,candidate.regs,sizeof(expected_regs)));
            bool expected=memcmp(binding.key.regs,expected_regs,sizeof(expected_regs))!=0;
            assert(check_pipeline_dirty(&pg)==expected);
            pg.regs[reg]^=1u<<bit;pg.dirty[reg]=false;reuse_cases++;
        }
    }
    /* Reusing the pipeline must still emit changed dynamic blend state,
     * even after create_pipeline clears the register-dirty map. */
    r.dyn_blend=r.dyn_cull_front=r.dyn_vertex_input=true;pg.surface_shape.color_format=8;
    memset(pg.regs,0,sizeof(pg.regs));pg.regs[NV_PGRAPH_BLEND]=0x118;
    init_pipeline_key(&pg,&binding.key);emissions=0;emit_dynamic_state(&pg,true);assert(emissions==1);
    pg.regs[NV_PGRAPH_BLEND]=0x128;pg.dirty[NV_PGRAPH_BLEND]=true;assert(!check_pipeline_dirty(&pg));
    memset(pg.dirty,0,sizeof(pg.dirty));emit_dynamic_state(&pg,false);assert(emissions==2&&emitted.srcColorBlendFactor==pgraph_blend_factor_vk_map[2]);
    r.shader_bindings_changed=true;assert(check_pipeline_dirty(&pg));r.shader_bindings_changed=false;
    r.texture_bindings_changed=true;assert(check_pipeline_dirty(&pg));r.texture_bindings_changed=false;
    binding.key.clear=true;assert(check_pipeline_dirty(&pg));binding.key.clear=false;
    pg.host_format=1;assert(check_pipeline_dirty(&pg));pg.host_format=0;
    r.dyn_vertex_input=false;r.num_active_vertex_binding_descriptions=1;r.num_active_vertex_attribute_descriptions=1;
    r.vertex_binding_descriptions[0]=(VkVertexInputBindingDescription){.binding=0,.stride=16,.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
    r.vertex_attribute_descriptions[0]=(VkVertexInputAttributeDescription){.location=0,.binding=0,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.offset=0};
    init_pipeline_key(&pg,&binding.key);pg.dirty[NV_PGRAPH_BLENDCOLOR]=true;
    r.vertex_binding_descriptions[0].stride=32;assert(check_pipeline_dirty(&pg));r.vertex_binding_descriptions[0].stride=16;
    r.vertex_attribute_descriptions[0].offset=4;assert(check_pipeline_dirty(&pg));r.vertex_attribute_descriptions[0].offset=0;
    assert(!check_pipeline_dirty(&pg));r.pipeline_binding=NULL;assert(check_pipeline_dirty(&pg));r.pipeline_binding=&binding;
    /* All static list lengths, including empty/full and matching prefixes.
     * Dynamic VI ignores these key arrays and updates its command state. */
    for(unsigned i=0;i<16;i++){
        r.vertex_binding_descriptions[i]=(VkVertexInputBindingDescription){.binding=i,.stride=16,.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
        r.vertex_attribute_descriptions[i]=(VkVertexInputAttributeDescription){.location=i,.binding=i,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.offset=0};
    }
    unsigned length_cases=0;
    for(unsigned dynamic=0;dynamic<2;dynamic++)for(unsigned old=0;old<=16;old++){
        r.dyn_vertex_input=dynamic;r.num_active_vertex_binding_descriptions=r.num_active_vertex_attribute_descriptions=old;
        init_pipeline_key(&pg,&binding.key);
        for(unsigned count=0;count<=16;count++){
            r.num_active_vertex_binding_descriptions=r.num_active_vertex_attribute_descriptions=count;
            assert(check_pipeline_dirty(&pg)==(!dynamic&&old!=count));length_cases++;
        }
    }
    printf("static vertex input: %u old/new length and feature schedules; shrinking prefixes cannot reuse stale attributes\n",length_cases);
    /* Execute the production create_pipeline prefix, observing whether it
     * reaches key construction, including the speculative-ahead exception. */
    r.dyn_vertex_input=true;r.num_active_vertex_binding_descriptions=r.num_active_vertex_attribute_descriptions=0;
    r.dyn_blend=r.dyn_cull_front=true;init_pipeline_key(&pg,&binding.key);key_rebuilds=0;
    pg.regs[NV_PGRAPH_BLENDCOLOR]^=1;pg.dirty[NV_PGRAPH_BLENDCOLOR]=true;pipeline_prefix(&pg);assert(!key_rebuilds&&!pg.dirty[NV_PGRAPH_BLENDCOLOR]);
    r.ahead_valid=true;pipeline_prefix(&pg);assert(key_rebuilds==1);r.ahead_valid=false;
    r.shader_bindings_changed=true;pipeline_prefix(&pg);assert(key_rebuilds==2);r.shader_bindings_changed=false;
    r.dyn_blend=false;init_pipeline_key(&pg,&binding.key);
    pg.regs[NV_PGRAPH_BLEND]^=NV_PGRAPH_BLEND_EN;pg.dirty[NV_PGRAPH_BLEND]=true;pipeline_prefix(&pg);assert(key_rebuilds==3);
    printf("pipeline register reuse: %u bit/format/feature schedules, identical key projection and independent dynamic emission passed\n",reuse_cases);
    printf("surface blend: %u mapping/key/format-transition cases, canonical PSO reuse and dynamic-state caching passed\n",cases);
}
#else
#include "surface-blend-gl.h"
#endif
