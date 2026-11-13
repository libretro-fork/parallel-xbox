/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define PUMP_RING 64
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
typedef struct { VkShaderModule module; unsigned refs; uint64_t identity; } ShaderModuleInfo;
/* TYPES */
static LibJob pump_jobs[PUMP_RING];
static int pump_w,pump_r,pump_cancel,pipe_ec_work,pipe_ec_done;
static unsigned prepared,fragment_prepared,created,computed,notified,release_count,expected;
static bool cancel_in_prepare;
static ShaderModuleInfo vertex,fragment;
static jmp_buf stopped;
static int retro_atomic_load_acquire_int(int *p) { return *p; }
static void retro_atomic_store_release_int(int *p,int value)
{
    assert(p==&pump_r && value==1 && !release_count);
    LibJob *job=&pump_jobs[0];
    if (prepared) {
        assert(job->mods[0]==&vertex && vertex.refs==1);
        assert(job->stages[0].module==vertex.module);
        assert(job->nmods==(int)job->ci.stageCount);
    }
    assert(created+computed==(expected?0:1));
    *p=value;release_count++;
}
static void retro_eventcount_notify(int *p)
{
    assert(p==&pipe_ec_done && pump_r==1 && release_count==1);
    notified++;longjmp(stopped,1);
}
static int retro_eventcount_prepare_wait(int *p) { (void)p;assert(false);return 0; }
static void retro_eventcount_cancel_wait(int *p) { (void)p;assert(false); }
static void retro_eventcount_commit_wait(int *p,int key) { (void)p;(void)key;assert(false); }
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m) { assert(m);m->refs++; }
static ShaderModuleInfo *pgraph_vk_prepare_vertex_module(VkDevice device,uint64_t id,
    const VshState *state,SpvVshOptions options,bool geometry)
{
    LibJob *job=&pump_jobs[0];
    assert(!pump_r && !release_count && !created && !computed);
    assert(device==job->device && id==job->vertex_identity);
    assert(state==&job->key.shader_state.vsh);
    assert(!memcmp(&options,&job->vertex_options,sizeof(options)));
    assert(geometry==(job->ci.stageCount==3));
    assert(job->stages[0].module==VK_NULL_HANDLE);
    prepared++;vertex=(ShaderModuleInfo){.module=(VkShaderModule)(uintptr_t)0x1234,.identity=id};
    if(cancel_in_prepare) pump_cancel=1;
    return &vertex;
}
static ShaderModuleInfo *pgraph_vk_prepare_fragment_module(VkDevice device,uint64_t id,
    const PshState *state,SpvPshOptions options)
{
    LibJob *job=&pump_jobs[0];
    assert(!pump_r && !release_count && !created && !computed);
    assert(device==job->device && id==job->fragment_identity);
    assert(state==&job->key.shader_state.psh);
    assert(!memcmp(&options,&job->fragment_options,sizeof(options)));
    assert(job->stages[job->ci.stageCount-1].module==VK_NULL_HANDLE);
    fragment_prepared++;fragment=(ShaderModuleInfo){.module=(VkShaderModule)(uintptr_t)0x4321,.identity=id};
    return &fragment;
}
static VkResult vkCreateGraphicsPipelines(VkDevice device,VkPipelineCache cache,
    uint32_t count,const VkGraphicsPipelineCreateInfo *ci,const VkAllocationCallbacks *alloc,VkPipeline *result)
{
    LibJob *job=&pump_jobs[0];
    assert(device==job->device && cache==job->cache && count==1 && !alloc);
    assert(ci==&job->ci && !pump_r && !release_count);
    assert(ci->pStages==job->stages && job->nmods==(int)ci->stageCount);
    for(unsigned i=0;i<ci->stageCount;i++) {
        assert(job->mods[i] && job->mods[i]->refs==1);
        assert(ci->pStages[i].module==job->mods[i]->module);
    }
    assert(ci->pStages[0].stage==VK_SHADER_STAGE_VERTEX_BIT);
    assert(ci->pStages[ci->stageCount-1].stage==VK_SHADER_STAGE_FRAGMENT_BIT);
    if(ci->stageCount==3) assert(ci->pStages[1].stage==VK_SHADER_STAGE_GEOMETRY_BIT);
    created++;*result=(VkPipeline)(uintptr_t)0x5678;return VK_SUCCESS;
}
static void s3tc_pipeline_job_create(VkDevice d,VkPipelineCache c,VkPipelineLayout *l,
    S3tcPipelineJob *s,VkPipeline *result)
{
    (void)d;(void)c;(void)l;(void)s;assert(!pump_r && !prepared && !created);
    computed++;*result=(VkPipeline)(uintptr_t)0x789a;
}
/* PRODUCTION */
int main(void)
{
    unsigned cases=0;
    for(unsigned serial=1;serial<=256;serial++)
    for(unsigned geometry=0;geometry<2;geometry++)
    for(unsigned mode=0;mode<5;mode++)
    for(unsigned missing_fragment=0;missing_fragment<2;missing_fragment++) {
        memset(pump_jobs,0,sizeof(pump_jobs));
        prepared=fragment_prepared=created=computed=notified=release_count=0;
        pump_w=1;pump_r=0;pump_cancel=mode==1;expected=mode==1;
        cancel_in_prepare=mode==2;
        ShaderModuleInfo gs={.module=(VkShaderModule)(uintptr_t)2,.refs=1};
        ShaderModuleInfo fs={.module=(VkShaderModule)(uintptr_t)3,.refs=1};
        ShaderModuleInfo vs={.module=(VkShaderModule)(uintptr_t)4,.refs=1};
        LibJob *job=&pump_jobs[0];job->full=true;job->prepare_vertex=mode<3;
        job->prepare_fragment=missing_fragment&&mode!=4;
        job->fragment_identity=serial+1024;job->fragment_options.tex_binding=serial%4;
        job->compute=mode==4;job->vertex_identity=serial;job->device=(VkDevice)(uintptr_t)5;
        job->vertex_options.ubo_binding=serial%8;
        job->ci.stageCount=geometry?3:2;job->ci.pStages=job->stages;
        job->stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;
        if(!job->prepare_vertex) {job->mods[job->nmods++]=&vs;job->stages[0].module=vs.module;}
        if(geometry) {job->mods[job->nmods++]=&gs;job->stages[1].module=gs.module;job->stages[1].stage=VK_SHADER_STAGE_GEOMETRY_BIT;}
        if(!job->prepare_fragment){job->mods[job->nmods++]=&fs;job->stages[geometry?2:1].module=fs.module;}
        job->stages[geometry?2:1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;
        if(!setjmp(stopped)) pipe_worker_fn(NULL);
        assert(notified==1 && release_count==1 && pump_r==1);
        assert(prepared==(mode==0||mode==2));assert(created==(mode==0||mode==2||mode==3));assert(computed==(mode==4));
        assert((job->result==VK_NULL_HANDLE)==(mode==1));
        assert(fragment_prepared==(unsigned)(missing_fragment&&mode!=1&&mode!=4));
        if(mode==1) assert(job->nmods==(int)job->ci.stageCount-1-job->prepare_fragment);
        /* Main-thread ordered drain releases exactly the owners published. */
        for(int i=0;i<job->nmods;i++) {assert(job->mods[i]->refs==1);job->mods[i]->refs--;}
        assert(!gs.refs || !geometry);assert(fs.refs==(unsigned)job->prepare_fragment);
        if(fragment_prepared)assert(!fragment.refs);
        if(prepared) assert(!vertex.refs);
        cases++;
    }
    printf("Vertex worker: %u VS/GS/FS ordering, pre-cancel, in-progress cancel, completion publication and transferred-owner cases PASS\n",cases);
    return 0;
}
