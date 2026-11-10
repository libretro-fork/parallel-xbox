/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <retro_atomic.h>
#include "qemu/fast-hash.h"
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
/* Use production XXH3's scalar path without a platform CPU-probe dependency. */
uint64_t cpu_features_get(void) {return 0;}
typedef struct {retro_atomic_int_t refs;} ShaderLayouts;
typedef struct {ShaderLayouts *layouts;uint32_t *words;unsigned num_words,refs;} ShaderSource;
typedef struct ShaderModuleInfo {int refcnt;uint64_t identity;VkShaderModule module;ShaderLayouts *layouts;} ShaderModuleInfo;
typedef struct {bool dyn_cull_front,dyn_vertex_input,dyn_blend;uint64_t next_shader_module_identity;VkDevice device;VkPipelineCache vk_pipeline_cache;} PGRAPHVkState;
/* TYPES */
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
typedef struct {RenderPassState render_pass_state;uint32_t shader_state;} PipelineKey;
static unsigned allocations,driver_calls,source_drops;
static uint32_t words[8];
static ShaderModuleInfo *alloc_shader_module_info(const ShaderLayouts *layouts) {assert(!layouts);allocations++;return calloc(1,sizeof(ShaderModuleInfo));}
static VkShaderModule pgraph_vk_create_shader_module_from_words(PGRAPHVkState *r,const uint32_t *data,size_t count) {assert(r && data==words && count==8);driver_calls++;return (VkShaderModule)(uintptr_t)0x5151;}
static void pgraph_vk_shader_source_unref(ShaderSource *source) {assert(source->refs==1);source->refs--;source_drops++;}
static struct {bool frag;uint64_t hash;VkPipeline pipeline;} libraries[16];
static unsigned library_count,library_calls;
static VkPipeline gpl_lookup(PGRAPHVkState *r,bool frag,uint64_t hash,const void *key,bool *found) {
    assert(r && key);for(unsigned i=0;i<library_count;i++)if(libraries[i].frag==frag && libraries[i].hash==hash){*found=true;return libraries[i].pipeline;}
    *found=false;return VK_NULL_HANDLE;
}
static void gpl_store(PGRAPHVkState *r,bool frag,uint64_t hash,const void *key,VkPipeline pipeline) {
    assert(r && key && library_count<16);libraries[library_count].frag=frag;libraries[library_count].hash=hash;libraries[library_count++].pipeline=pipeline;
}
static uint32_t push_constant_size(PGRAPHVkState *r,const uint32_t *state) {assert(r);return *state;}
static VkResult vkCreateGraphicsPipelines(VkDevice device,VkPipelineCache cache,uint32_t count,const VkGraphicsPipelineCreateInfo *info,const VkAllocationCallbacks *allocator,VkPipeline *out) {
    (void)device;(void)cache;assert(count==1 && !allocator && info->flags==VK_PIPELINE_CREATE_LIBRARY_BIT_KHR);
    for(unsigned i=0;i<info->stageCount;i++)assert(info->pStages[i].module==(VkShaderModule)(uintptr_t)0x5151);
    *out=(VkPipeline)(uintptr_t)(++library_calls+100);return VK_SUCCESS;
}
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
/* These identity/constructor fixtures model an empty completed queue.
 * Transfer ownership and publication boundaries have their own native-key test. */
static VkPipeline pump_take_library(PGRAPHVkState *r,bool frag,uint64_t hash,const void *key) {
    (void)r;(void)frag;(void)hash;assert(key);return VK_NULL_HANDLE;
}
/* FUNCTIONS */
/* REFERENCE */
/* Keep the identity matrix's direct-hash calls concise while production
 * callers retain the canonical output for exact cache comparisons. */
static uint64_t identity_pre_hash(PGRAPHVkState *r,uint64_t v,uint64_t g,const RenderPassState *rp,const VkPipelineRasterizationStateCreateInfo *rs,const VkPipelineInputAssemblyStateCreateInfo *ia,const VkPipelineVertexInputStateCreateInfo *vi) {
    GplPreKey key;uint64_t hash=gpl_pre_hash(r,v,g,rp,rs,ia,vi,&key);
    if(r->dyn_vertex_input)for(size_t i=offsetof(GplPreKey,bind);i<sizeof(key);i++)assert(((const unsigned char *)&key)[i]==0);
    return hash;
}
static uint64_t identity_frag_hash(PGRAPHVkState *r,uint64_t p,const VkPipelineDepthStencilStateCreateInfo *ds,const VkPipelineColorBlendStateCreateInfo *cb,const RenderPassState *rp,uint32_t push) {
    GplFragKey key;return gpl_frag_hash(r,p,ds,cb,rp,push,&key);
}
#define gpl_pre_hash identity_pre_hash
#define gpl_frag_hash identity_frag_hash
static ShaderModuleInfo *create(PGRAPHVkState *r,unsigned code) {
    words[0]=code;ShaderSource source={.words=words,.num_words=8,.refs=1};
    unsigned before=allocations;ShaderModuleInfo *m=pgraph_vk_create_shader_module_from_spirv(r,&source);
    assert(m && m->identity && !m->refcnt && m->module==(VkShaderModule)(uintptr_t)0x5151);
    assert(allocations==before+1 && !source.refs);return m;
}
int main(void) {
    unsigned cases=0;
    for(unsigned generation=0;generation<256;generation++)for(unsigned bits=0;bits<32;bits++) {
        PGRAPHVkState r={.dyn_cull_front=bits&1,.dyn_vertex_input=bits&2,.dyn_blend=bits&4};
        ShaderModuleInfo *v=create(&r,1),*g=create(&r,2),*p=create(&r,3);
        assert(v->identity==1 && g->identity==2 && p->identity==3);
        RenderPassState rp={.color_format=VK_FORMAT_R8G8B8A8_UNORM,.zeta_format=bits&8?VK_FORMAT_D24_UNORM_S8_UINT:VK_FORMAT_UNDEFINED};
        VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.lineWidth=1,.cullMode=generation%4,.polygonMode=generation%3,.frontFace=generation%2};
        VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        VkVertexInputBindingDescription binding={.binding=0,.stride=generation+4};
        VkVertexInputAttributeDescription attribute={.binding=0,.location=0,.format=VK_FORMAT_R32G32B32A32_SFLOAT,.offset=generation%16};
        VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&binding,.vertexAttributeDescriptionCount=1,.pVertexAttributeDescriptions=&attribute};
        VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=VK_TRUE,.depthCompareOp=generation%8};
        VkPipelineColorBlendAttachmentState blend={.colorWriteMask=generation%16,.blendEnable=generation%2};
        VkPipelineColorBlendStateCreateInfo cb={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};
        uint64_t geom=bits&16?g->identity:0;uint32_t push=(generation%17)*16;
        library_count=0;PGRAPHState pg={&r};PipelineKey key={.render_pass_state=rp,.shader_state=push};
        unsigned pre_count=geom?2:1;
        VkPipelineShaderStageCreateInfo stages[3]={{.module=v->module},{.module=geom?g->module:p->module},{.module=p->module}};
        VkGraphicsPipelineCreateInfo full={.pStages=stages,.stageCount=pre_count+1,.pRasterizationState=&rs,.pInputAssemblyState=&ia,.pVertexInputState=&vi,.pDepthStencilState=&ds,.pColorBlendState=&cb};
        VkPipeline old_pre_lib=gpl_get_pre(&pg,&full,&rp,VK_NULL_HANDLE,pre_count,v,geom?g:NULL);
        VkPipeline old_frag_lib=gpl_get_frag(&pg,&full,&key,VK_NULL_HANDLE,pre_count+1,p);
        unsigned calls=library_calls;
        assert(old_pre_lib==gpl_get_pre(&pg,&full,&rp,VK_NULL_HANDLE,pre_count,v,geom?g:NULL));
        assert(old_frag_lib==gpl_get_frag(&pg,&full,&key,VK_NULL_HANDLE,pre_count+1,p) && library_calls==calls);
        uint64_t pre=gpl_pre_hash(&r,v->identity,geom,&rp,&rs,&ia,&vi);
        uint64_t frag=gpl_frag_hash(&r,p->identity,&ds,&cb,&rp,push);
        assert(pre==reference_pre_hash(&r,(VkShaderModule)(uintptr_t)v->identity,(VkShaderModule)(uintptr_t)geom,&rp,&rs,&ia,&vi));
        assert(frag==reference_frag_hash(&r,(VkShaderModule)(uintptr_t)p->identity,&ds,&cb,&rp,push));
        uint64_t old_pre=reference_pre_hash(&r,v->module,bits&16?g->module:VK_NULL_HANDLE,&rp,&rs,&ia,&vi);
        uint64_t old_frag=reference_frag_hash(&r,p->module,&ds,&cb,&rp,push);
        VkShaderModule handle=v->module;free(v);v=create(&r,4);assert(v->module==handle && v->identity==4);
        assert(old_pre==reference_pre_hash(&r,v->module,bits&16?g->module:VK_NULL_HANDLE,&rp,&rs,&ia,&vi));
        assert(pre!=gpl_pre_hash(&r,v->identity,geom,&rp,&rs,&ia,&vi));
        VkPipeline next_pre_lib=gpl_get_pre(&pg,&full,&rp,VK_NULL_HANDLE,pre_count,v,geom?g:NULL);assert(next_pre_lib!=old_pre_lib);
        if(geom) {free(g);g=create(&r,5);assert(pre!=gpl_pre_hash(&r,1,g->identity,&rp,&rs,&ia,&vi));assert(next_pre_lib!=gpl_get_pre(&pg,&full,&rp,VK_NULL_HANDLE,pre_count,v,g));}
        free(p);p=create(&r,6);assert(old_frag==reference_frag_hash(&r,p->module,&ds,&cb,&rp,push));
        assert(frag!=gpl_frag_hash(&r,p->identity,&ds,&cb,&rp,push));
        assert(old_frag_lib!=gpl_get_frag(&pg,&full,&key,VK_NULL_HANDLE,pre_count+1,p));
        uint64_t stable=gpl_pre_hash(&r,v->identity,bits&16?g->identity:0,&rp,&rs,&ia,&vi);
        v->refcnt=19;assert(stable==gpl_pre_hash(&r,v->identity,bits&16?g->identity:0,&rp,&rs,&ia,&vi));
        free(v);free(g);free(p);cases++;
    }
    PGRAPHVkState r={.next_shader_module_identity=UINT64_MAX-2};
    ShaderModuleInfo *a=create(&r,7),*b=create(&r,8);assert(a->identity==UINT64_MAX-1 && b->identity==UINT64_MAX);free(a);free(b);
    RenderPassState rp={0};VkPipelineRasterizationStateCreateInfo rs={0};
    VkPipelineInputAssemblyStateCreateInfo ia={0};VkPipelineVertexInputStateCreateInfo vi={0};
    VkPipelineColorBlendStateCreateInfo cb={0};uint64_t high=(UINT64_C(1)<<40)|1;
    assert(gpl_pre_hash(&r,high,0,&rp,&rs,&ia,&vi)!=gpl_pre_hash(&r,1,0,&rp,&rs,&ia,&vi));
    assert(gpl_pre_hash(&r,1,high,&rp,&rs,&ia,&vi)!=gpl_pre_hash(&r,1,1,&rp,&rs,&ia,&vi));
    assert(gpl_frag_hash(&r,high,NULL,&cb,&rp,0)!=gpl_frag_hash(&r,1,NULL,&cb,&rp,0));
    assert(allocations==driver_calls && source_drops==driver_calls);
    printf("GPL module identity: %u forced-handle-reuse/static/dynamic cases, native constructor IDs, fresh renderer generations, high64-bit IDs and one-allocation/source-owner checks PASS; frozen handle keys alias as expected\n",cases);
}
