/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "qemu/lru.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef struct ShaderModuleInfo { unsigned refs, generation; } ShaderModuleInfo;
/* TYPES */
typedef struct PGRAPHVkState {
    Lru pipeline_cache;
    PipelineBinding *pipeline_binding;
    bool in_command_buffer, fence_pending;
    unsigned command_buffer_start_time, trash_n;
    VkPipeline trash[8];
    VkDevice device;
} PGRAPHVkState;
struct PGRAPHState { PGRAPHVkState *vk_renderer_state; unsigned draw_time; };
typedef struct {
    PipelineKey key;
    uint64_t hash,state_hash;
    VkPipeline result;
    VkPipelineLayout layout;
    VkGraphicsPipelineCreateInfo ci;
    bool has_dynamic_line_width;
    ShaderModuleInfo *mods[3];
    unsigned nmods;
} LibJob;
struct XemuVkTrashEntry { VkPipeline pipeline; };
static unsigned destroyed, landed, drops;
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m) { assert(m&&m->refs);m->refs++; }
static void pgraph_vk_unref_shader_module(PGRAPHVkState *r,ShaderModuleInfo *m)
{ assert(r&&m&&m->refs);m->refs--;if(!m->refs)drops++; }
static void pgraph_vk_trash_push(PGRAPHVkState *r,struct XemuVkTrashEntry *e)
{ assert(r->fence_pending&&e->pipeline&&r->trash_n<8);r->trash[r->trash_n++]=e->pipeline; }
static void vkDestroyPipeline(VkDevice d,VkPipeline p,const void *a)
{ assert(d==(VkDevice)(uintptr_t)7&&p&&!a);destroyed++; }
static void pgraph_vk_spec_pipeline_landed(uint64_t hash) { assert(hash==19);landed++; }
/* PRODUCTION */
static void drop_job(PGRAPHVkState *r,LibJob *job)
{ for(unsigned i=0;i<job->nmods;i++)pgraph_vk_unref_shader_module(r,job->mods[i]); }
int main(void)
{
    unsigned cases=0;
    for(unsigned generation=1;generation<=8;generation++)
    for(unsigned geometry=0;geometry<2;geometry++)
    for(unsigned external=0;external<2;external++) {
        PGRAPHVkState r={.device=(VkDevice)(uintptr_t)7,.command_buffer_start_time=11,
                        .in_command_buffer=true,.fence_pending=true};
        PGRAPHState pg={&r,10};PipelineBinding entries[2]={0};
        ShaderModuleInfo owners[3]={{1,generation},{1,generation},{1,generation}};
        ShaderModuleInfo duplicate[3]={{1,generation},{1,generation},{1,generation}};
        LibJob job={.hash=1,.state_hash=19,.result=(VkPipeline)(uintptr_t)101,
                    .layout=(VkPipelineLayout)(uintptr_t)55,.has_dynamic_line_width=true};
        job.key.shader_state.vsh.programmable.program_data[0][0]=generation;
        job.key.regs[0]=geometry;job.ci.renderPass=(VkRenderPass)(uintptr_t)66;
        job.nmods=2+geometry;for(unsigned i=0;i<job.nmods;i++)job.mods[i]=&owners[i];
        lru_init(&r.pipeline_cache);r.pipeline_cache.init_node=pipeline_cache_entry_init;
        r.pipeline_cache.compare_nodes=pipeline_cache_entry_compare;
        r.pipeline_cache.post_node_evict=pipeline_cache_entry_post_evict;
        for(unsigned i=0;i<2;i++)lru_add_free(&r.pipeline_cache,&entries[i].node);
        destroyed=landed=drops=0;
        pipeline_land(&pg,&job);
        PipelineBinding *p=container_of(lru_lookup(&r.pipeline_cache,1,&job.key),PipelineBinding,node);
        assert(p->pipeline==job.result&&p->layout==job.layout&&p->render_pass==job.ci.renderPass&&p->has_dynamic_line_width);
        assert(p->shader_modules[0]==&owners[0]&&p->shader_modules[2]==&owners[job.nmods-1]);
        assert(p->shader_modules[1]==(geometry?&owners[1]:NULL));
        for(unsigned i=0;i<job.nmods;i++)assert(owners[i].refs==2);
        drop_job(&r,&job);for(unsigned i=0;i<job.nmods;i++)assert(owners[i].refs==1);
        /* Insert a colliding full key; it cannot replace the first owner. */
        PipelineKey other=job.key;other.regs[1]=0x135;
        PipelineBinding *collision=container_of(lru_lookup(&r.pipeline_cache,1,&other),PipelineBinding,node);
        collision->key=other;
        LibJob repeated=job;repeated.result=(VkPipeline)(uintptr_t)202;
        for(unsigned i=0;i<repeated.nmods;i++)repeated.mods[i]=&duplicate[i];
        pipeline_land(&pg,&repeated);
        assert(destroyed==1&&landed==2&&p->pipeline==job.result);
        for(unsigned i=0;i<job.nmods;i++)assert(owners[i].refs==1&&duplicate[i].refs==1);
        drop_job(&r,&repeated);
        if(external)for(unsigned i=0;i<job.nmods;i++)pgraph_vk_ref_shader_module(&owners[i]);
        r.pipeline_binding=p;lru_evict_node(&r.pipeline_cache,&p->node);
        assert(!r.pipeline_binding&&!p->pipeline&&!p->layout&&r.trash_n==1&&r.trash[0]==job.result);
        assert(destroyed==1); /* pending GPU reader still owns its PSO */
        for(unsigned i=0;i<3;i++)assert(!p->shader_modules[i]);
        for(unsigned i=0;i<job.nmods;i++) {
            assert(owners[i].refs==external);
            if(external){assert(owners[i].generation==generation);pgraph_vk_unref_shader_module(&r,&owners[i]);}
        }
        assert(drops==2*job.nmods);
        pipeline_cache_entry_post_evict(&r.pipeline_cache,&p->node);
        assert(r.trash_n==1&&drops==2*job.nmods);
        r.fence_pending=false;vkDestroyPipeline(r.device,r.trash[0],NULL);
        assert(destroyed==2);lru_flush(&r.pipeline_cache);
        cases++;
    }
    printf("Full PSO metadata: %u worker/duplicate/collision/geometry/external-consumer/eviction schedules; CPU owners end independently of GPU fence retirement PASS\n",cases);
}
