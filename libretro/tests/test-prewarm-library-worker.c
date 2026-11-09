/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);_Exit(1); } } while(0)
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define PUMP_RING 64
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct { VkShaderModule module;uint64_t identity;unsigned refs; } ShaderModuleInfo;
typedef struct { VkVertexInputBindingDescription binding_descriptions[16];VkVertexInputAttributeDescription attribute_descriptions[16]; } PipelineKey;
typedef struct { unsigned tag;bool geometry; } ShaderState;
typedef struct { unsigned tag; } RenderPassState;
typedef struct { VkDevice device;VkPipelineCache vk_pipeline_cache; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; } PGRAPHState;
/* PREWARM */
/* JOB */
static LibJob pump_jobs[PUMP_RING];
static unsigned pump_w_local,pump_d,acquires,releases,drains,starts,wakes,layouts,destroyed,layout_drops,stores;
static int pump_w,pump_r,pipe_ec_work;
static bool pipe_started=true;
static bool cached;
static ShaderModuleInfo vertex,geometry;
static uint64_t expected_hash;
static int retro_atomic_load_acquire_int(int *p) {assert(p==&pump_r);drains++;return *p;}
static void pipeline_land(PGRAPHState *pg,LibJob *job) {(void)pg;(void)job;assert(false);}
static void pipe_start(void) { starts++; }
static void retro_atomic_store_release_int(int *p,int value) {
    assert(p==&pump_w && value==(int)pump_w_local);
    LibJob *j=&pump_jobs[(pump_w_local-1)%PUMP_RING];
    assert(j->nmods==(geometry.refs?2:1) && j->mods[0]==&vertex && vertex.refs==1);
    if(geometry.refs) assert(j->mods[1]==&geometry && geometry.refs==1);
    assert(j->ci.pStages==j->stages && j->ci.pNext==&j->lib_info);
    assert(j->ci.pVertexInputState==&j->vi && !j->vi.vertexBindingDescriptionCount && !j->vi.vertexAttributeDescriptionCount);
    assert(j->ci.pInputAssemblyState==&j->ia && j->ci.pRasterizationState==&j->raster);
    assert(j->ci.pViewportState==&j->vp && j->ci.pDynamicState==&j->dyn && j->dyn.pDynamicStates==j->dyn_states);
    assert(!j->ci.pMultisampleState && !j->ci.pColorBlendState && !j->ci.pDepthStencilState);
    assert(j->ci.flags==VK_PIPELINE_CREATE_LIBRARY_BIT_KHR && !j->full && !j->frag);
    assert(j->lib_info.flags==(VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT|VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT));
    *p=value;
}
static void retro_eventcount_notify(int *p) {assert(p==&pipe_ec_work);wakes++;}
static void pgraph_vk_ref_pre_state_modules(PGRAPHVkState *r,const ShaderState *s,ShaderModuleInfo **g,ShaderModuleInfo **v) {
    assert(r);acquires++;vertex.module=(VkShaderModule)(uintptr_t)(s->tag+100);vertex.identity=s->tag+50000;vertex.refs++;*v=&vertex;
    *g=NULL;if(s->geometry){geometry.module=(VkShaderModule)(uintptr_t)(s->tag+1000);geometry.identity=s->tag+60000;geometry.refs++;*g=&geometry;}
}
static void pgraph_vk_unref_shader_module(PGRAPHVkState *r,ShaderModuleInfo *m) { assert(r && m->refs);m->refs--;releases++; }
static int pre_stages(VkPipelineShaderStageCreateInfo *out,ShaderModuleInfo *v,ShaderModuleInfo *g) {
    out[0]=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.module=v->module,.stage=VK_SHADER_STAGE_VERTEX_BIT,.pName="main"};
    if(g) out[1]=(VkPipelineShaderStageCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.module=g->module,.stage=VK_SHADER_STAGE_GEOMETRY_BIT,.pName="main"};
    return g?2:1;
}
static void ahead_pre_state(const ShaderState *s,VkPipelineRasterizationStateCreateInfo *rs,VkPipelineInputAssemblyStateCreateInfo *ia) {
    *rs=(VkPipelineRasterizationStateCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.lineWidth=1,.cullMode=s->tag%4};
    *ia=(VkPipelineInputAssemblyStateCreateInfo){.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=s->geometry?VK_PRIMITIVE_TOPOLOGY_LINE_LIST:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
}
static uint64_t gpl_pre_hash(PGRAPHVkState *r,uint64_t v,uint64_t g,const RenderPassState *rp,const VkPipelineRasterizationStateCreateInfo *rs,const VkPipelineInputAssemblyStateCreateInfo *ia,const VkPipelineVertexInputStateCreateInfo *vi) {
    assert(r && v==vertex.identity && g==geometry.identity && rp->tag==71 && rs->lineWidth==1 && ia->sType && vi->sType);
    return expected_hash;
}
static VkPipeline gpl_lookup(PGRAPHVkState *r,bool frag,uint64_t hash,bool *found) {assert(r && !frag && hash==expected_hash);*found=cached;return cached?(VkPipeline)(uintptr_t)11:VK_NULL_HANDLE;}
static void gpl_store(PGRAPHVkState *r,bool frag,uint64_t hash,VkPipeline pipeline) {assert(r && !frag && hash==expected_hash && pipeline==(VkPipeline)(uintptr_t)21);stores++;}
static VkPipelineLayout create_pipeline_layout_size(PGRAPHVkState *r,uint32_t size) {assert(r && size==16);layouts++;return (VkPipelineLayout)(uintptr_t)31;}
static int ahead_dynamic_states(PGRAPHVkState *r,const ShaderState *s,VkDynamicState *out) {assert(r && s);out[0]=VK_DYNAMIC_STATE_VIEWPORT;out[1]=VK_DYNAMIC_STATE_SCISSOR;return 2;}
static VkRenderPass get_render_pass(PGRAPHVkState *r,RenderPassState *rp) {assert(r && rp->tag==71);return (VkRenderPass)(uintptr_t)41;}
static void vkDestroyPipeline(VkDevice d,VkPipeline p,const VkAllocationCallbacks *a) {assert(d && p==(VkPipeline)(uintptr_t)21 && !a);destroyed++;}
static void vkDestroyPipelineLayout(VkDevice d,VkPipelineLayout p,const VkAllocationCallbacks *a) {assert(d && p==(VkPipelineLayout)(uintptr_t)31 && !a);layout_drops++;}
/* PRODUCTION */
int main(void) {
    unsigned cases=0;
    for(unsigned generation=0;generation<128;generation++)for(unsigned bits=0;bits<64;bits++) {
        memset(pump_jobs,0xa5,sizeof(pump_jobs));memset(&vertex,0,sizeof(vertex));memset(&geometry,0,sizeof(geometry));
        acquires=releases=drains=starts=wakes=layouts=destroyed=layout_drops=stores=0;
        PGRAPHVkState r={(VkDevice)(uintptr_t)1,(VkPipelineCache)(uintptr_t)2};PGRAPHState pg={&r};
        ShaderState state={generation+1,!!(bits&1)};RenderPassState rp={71};cached=bits&2;
        bool full=bits&4,duplicate=bits&8,other=bits&16;
        expected_hash=700+generation;pump_d=generation;pump_r=(int)pump_d;pump_w_local=pump_d+(full?64:duplicate?1:0);
        if(duplicate && !full) {LibJob *old=&pump_jobs[pump_d%64];pipeline_job_reset(old);old->hash=expected_hash;old->full=other;}
        VkPrewarmState pipeline={.render_pass=rp,.push_size=16};
        ahead_pre_state(&state,&pipeline.raster,&pipeline.ia);
        pipeline.num_dynamic=ahead_dynamic_states(&r,&state,pipeline.dynamic_states);
        ShaderModuleInfo *vsh,*geom;pgraph_vk_ref_pre_state_modules(&r,&state,&geom,&vsh);
        unsigned before=pump_w_local;bool accepted=pgraph_vk_pre_library_ready(&pg,&pipeline,vsh,geom);
        bool skip=full || cached || (duplicate && !other);
        assert(accepted==!full && drains==1 && acquires==1 && layouts==!skip);
        assert(pump_w_local==before+!skip && starts==!skip && wakes==!skip);
        assert(releases==(full?0u:skip?1u+state.geometry:0u));
        assert(vertex.refs==(full||!skip) && geometry.refs==((full||!skip) && state.geometry));
        if(full) {pgraph_vk_unref_shader_module(&r,vsh);if(geom)pgraph_vk_unref_shader_module(&r,geom);}
        if(!skip) {
            LibJob *j=&pump_jobs[before%64];unsigned tag=state.tag;bool geo=state.geometry;
            memset(&state,0xcc,sizeof(state));memset(&rp,0xcc,sizeof(rp));
            memset(&pipeline,0xdd,sizeof(pipeline));
            assert(j->ci.stageCount==(geo?2u:1u) && j->stages[0].module==(VkShaderModule)(uintptr_t)(tag+100));
            assert(j->ci.renderPass==(VkRenderPass)(uintptr_t)41 && j->layout==(VkPipelineLayout)(uintptr_t)31);
            assert(j->ci.pStages==j->stages && j->dyn.pDynamicStates==j->dyn_states);
            assert(j->dyn_states[0]==VK_DYNAMIC_STATE_VIEWPORT && j->dyn_states[1]==VK_DYNAMIC_STATE_SCISSOR);
            assert(j->raster.lineWidth==1 && j->ia.topology==(geo?VK_PRIMITIVE_TOPOLOGY_LINE_LIST:VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST));
            bool cancelled=bits&32;j->result=cancelled?VK_NULL_HANDLE:(VkPipeline)(uintptr_t)21;cached=bits&16;
            /* Older pending jobs were tested above; consume this completion
             * alone through the real cancellation/adoption/release path. */
            pump_d=before;pump_r=(int)pump_w_local;pump_drain(&pg);
            assert(pump_d==pump_w_local && destroyed==(!cancelled && cached) && stores==(!cancelled && !cached) && layout_drops==1);
            assert(!vertex.refs && !geometry.refs);
        }
        cases++;
    }
    printf("Upload prewarm worker: %u stable-slot/backpressure/cache/inflight/owner/adoption cases PASS\n",cases);
}
