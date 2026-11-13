/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define PUMP_RING 64
#define VSH_UBO_BINDING 0
#define PSH_UBO_BINDING 1
#define PSH_TEX_BINDING 2
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define QTAILQ_FOREACH(n,h,f) for((n)=*(h);(n);(n)=(n)->next)
typedef struct LruNode {uint64_t hash;struct LruNode *next;} LruNode;
typedef struct {LruNode *bins[8];} Lru;
typedef struct ShaderModuleInfo {unsigned refs;} ShaderModuleInfo;
/* TYPES */
typedef struct {bool compute,full,prepare_vertex,prepare_fragment;uint64_t hash,state_hash,vertex_identity;
    PipelineKey key;ShaderModuleInfo *mods[3];unsigned nmods;SpvVshOptions vertex_options;uint64_t fragment_identity;SpvPshOptions fragment_options;
    VkGraphicsPipelineCreateInfo ci;} LibJob;
typedef struct ShaderBinding {ShaderState state;} ShaderBinding;
typedef struct {Lru pipeline_cache;uint64_t next_shader_module_identity;
    bool use_push_constants_for_uniform_attrs,sibling_retry,sibling_vertex_family;
    unsigned sibling_ticket,sibling_generation;} PGRAPHVkState;
struct PGRAPHState {PGRAPHVkState *vk_renderer_state;bool program_data_dirty;};
static LibJob pump_jobs[PUMP_RING];
static unsigned pump_d,pump_w_local,filled,published;
unsigned pgraph_glsl_dynamic_gen=17;
static bool enabled,geometry,collision,pressure,queue_full;
static bool pgraph_vk_spirv_1_6,pgraph_vk_preserve_float32;
static PipelineKey native_key;
static ShaderBinding materialized;
static ShaderModuleInfo *ready_vertex,*ready_fragment;
static bool pgraph_gpu_boost_gl(void){return enabled;}
bool pgraph_glsl_need_geom(const GeomState *state){(void)state;return geometry;}
static uint64_t fast_hash(const uint8_t *p,size_t n){if(collision)return 1;uint64_t h=1469598103934665603ull;while(n--)h=(h^*p++)*1099511628211ull;return h;}
static unsigned lru_hash_to_bin(Lru *l,uint64_t h){(void)l;return h%8;}
static void init_pipeline_key_for_shader(PGRAPHState *pg,PipelineKey *key,const ShaderState *state){(void)pg;*key=native_key;key->shader_state=*state;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m){assert(m&&m->refs);m->refs++;}
static ShaderModuleInfo *pgraph_vk_ready_vertex_module(PGRAPHVkState *r,const ShaderState *s){(void)r;(void)s;return ready_vertex;}
static ShaderModuleInfo *pgraph_vk_ready_fragment_module(PGRAPHVkState *r,const ShaderState *s){(void)r;(void)s;return ready_fragment;}
static LibJob *pump_slot(void){if(queue_full)return NULL;LibJob *j=&pump_jobs[pump_w_local%PUMP_RING];memset(j,0,offsetof(LibJob,key));j->nmods=0;return j;}
static void pipeline_job_fill(PGRAPHState *pg,const ShaderState *s,const ShaderModuleInfo *v,
    const ShaderModuleInfo *g,const ShaderModuleInfo *f,const PipelineKey *key,LibJob *job)
{
    (void)pg;assert(s==&job->key.shader_state&&key==&job->key);
    assert((f!=NULL)==!job->prepare_fragment);assert(!f||f->refs>=2);
    assert((g!=NULL)==geometry);assert(!g||g->refs>=2);
    assert((v!=NULL)==!job->prepare_vertex);assert(!v||v->refs>=2);
    job->full=true;job->ci.stageCount=geometry?3:2;filled++;
}
static void pump_submit(LibJob *j){assert(j==&pump_jobs[pump_w_local%PUMP_RING]);published++;pump_w_local++;}
static PipelineBinding *ready_vertex_pipeline(PGRAPHVkState *,const PipelineKey *,uint64_t);
static ShaderBinding *pgraph_vk_materialize_ready_shader_binding(PGRAPHVkState *r,const PipelineKey *key,uint64_t hash)
{
    if(pressure||!ready_vertex_pipeline(r,key,hash))return NULL;
    materialized.state=key->shader_state;return &materialized;
}
/* PRODUCTION */
static void add_pipeline(PGRAPHVkState *r,PipelineBinding *b,const PipelineKey *key,ShaderModuleInfo *mods)
{
    memset(b,0,sizeof(*b));b->key=*key;b->node.hash=fast_hash((const uint8_t *)key,sizeof(*key));
    b->pipeline=(VkPipeline)(uintptr_t)1;b->shader_modules[0]=mods;
    b->shader_modules[1]=geometry?mods+1:NULL;b->shader_modules[2]=mods+2;
    unsigned bin=b->node.hash%8;b->node.next=r->pipeline_cache.bins[bin];r->pipeline_cache.bins[bin]=&b->node;
}
static void reset(void)
{
    memset(pump_jobs,0,sizeof(pump_jobs));pump_d=pump_w_local=7;filled=published=0;
    memset(&native_key,0,sizeof(native_key));
    native_key.render_pass_state.color_format=VK_FORMAT_R8G8B8A8_UNORM;
    native_key.render_pass_state.zeta_format=VK_FORMAT_D16_UNORM;
    native_key.regs[0]=123;native_key.binding_descriptions[0].stride=16;
    native_key.attribute_descriptions[0].offset=4;
}
static void release_jobs(void)
{
    for(unsigned i=pump_d;i<pump_w_local;i++)for(unsigned k=0;k<pump_jobs[i%PUMP_RING].nmods;k++) {
        ShaderModuleInfo *m=pump_jobs[i%PUMP_RING].mods[k];assert(m->refs>=2);m->refs--;
    }
}
int main(void)
{
    unsigned cases=0;
    for(unsigned gen=0;gen<64;gen++)for(unsigned bits=0;bits<512;bits++) {
        reset();enabled=bits&1;geometry=bits&8;pressure=bits&16;queue_full=bits&32;collision=bits&128;
        ShaderModuleInfo mods[3]={{1},{1},{1}},v={1},f={1};ready_vertex=bits&64?&v:NULL;ready_fragment=bits&256?&f:NULL;
        PGRAPHVkState r={.next_shader_module_identity=33};PGRAPHState pg={.vk_renderer_state=&r};
        ShaderState raw={0};raw.vsh.is_fixed_function=true;
        raw.vsh.fixed_function.lighting=true;raw.vsh.fixed_function.normalization=gen&1;
        raw.vsh.fixed_function.light[0]=gen%4;raw.vsh.specular_enable=gen&2;
        raw.psh.final_inputs_0=gen;raw.vsh.uniform_attrs=gen;
        PipelineKey key;init_pipeline_key_for_shader(&pg,&key,&raw);
        pgraph_glsl_vsh_canonicalize_lighting(&key.shader_state.vsh);
        bool option=enabled;enabled=true;pgraph_glsl_psh_canonicalize_dynamic(&key.shader_state.psh);enabled=option;
        PipelineKey canonical=key;
        if(bits&4) switch(gen%8) {
            case 0:key.render_pass_state.color_format=VK_FORMAT_R5G6B5_UNORM_PACK16;break;
            case 1:key.render_pass_state.zeta_format=VK_FORMAT_D24_UNORM_S8_UINT;break;
            case 2:key.regs[0]++;break;
            case 3:key.binding_descriptions[0].stride++;break;
            case 4:key.attribute_descriptions[0].offset++;break;
            case 5:key.shader_state.psh.final_inputs_0++;break;
            case 6:key.shader_state.vsh.uniform_attrs++;break;
            case 7:key.shader_state.geom.primitive_mode++;break;
        }
        PipelineBinding source;
        if(bits&2)add_pipeline(&r,&source,&key,mods);
        bool use=enabled&&(bits&2)&&!(bits&4)&&!pressure;
        ShaderBinding *selected=pgraph_vk_try_vertex_family(&pg,&raw);
        assert((selected!=NULL)==use);
        if(use)assert(!memcmp(&selected->state,&canonical.shader_state,sizeof(raw)));
        assert(published==(unsigned)(use&&!queue_full));assert(filled==published);
        assert(r.sibling_retry==(use&&!queue_full));assert(r.sibling_vertex_family==r.sibling_retry);
        assert(pg.program_data_dirty==(use&&queue_full));
        if(published) {
            LibJob *job=&pump_jobs[7];PipelineKey exact;init_pipeline_key_for_shader(&pg,&exact,&raw);
            assert(!memcmp(&job->key,&exact,sizeof(exact)));
            assert(job->hash==fast_hash((const uint8_t *)&exact,sizeof(exact)));
            assert(job->prepare_vertex==!ready_vertex);
            assert(job->prepare_fragment==!ready_fragment);
            assert(job->nmods==(unsigned)!!ready_vertex+(unsigned)!!ready_fragment+(unsigned)geometry);
            assert(r.next_shader_module_identity==33+(unsigned)!ready_vertex+(unsigned)!ready_fragment);
            assert(r.sibling_ticket==7&&r.sibling_generation==17);
        }
        release_jobs();assert(mods[0].refs==1&&mods[1].refs==1&&mods[2].refs==1&&v.refs==1&&f.refs==1);cases++;
    }
    /* Exact in-flight jobs are reused even while their missing stage is being
     * prepared; another PSO with that stage waits for its final stage owner. */
    for(unsigned geom=0;geom<2;geom++)for(unsigned hit=0;hit<2;hit++) {
        reset();enabled=true;geometry=geom;pressure=queue_full=collision=false;ready_vertex=NULL;
        ShaderModuleInfo mods[3]={{1},{1},{1}};PGRAPHVkState r={0};PGRAPHState pg={.vk_renderer_state=&r};
        PipelineKey key=native_key;PipelineBinding source;add_pipeline(&r,&source,&key,mods);
        key.shader_state.vsh.specular_enable=true;
        uint64_t hash=fast_hash((const uint8_t *)&key,sizeof(key));unsigned ticket=99;
        assert(queue_vertex_pipeline(&pg,&key,hash,&source,&ticket)&&ticket==7&&published==1);
        if(!hit){key.regs[0]++;hash=fast_hash((const uint8_t *)&key,sizeof(key));}
        assert(queue_vertex_pipeline(&pg,&key,hash,&source,&ticket)==(bool)hit);
        assert(published==1&&r.next_shader_module_identity==1);
        release_jobs();assert(mods[1].refs==1&&mods[2].refs==1);cases++;
    }

    for(unsigned geom=0;geom<2;geom++) {
        reset();geometry=geom;queue_full=collision=false;ready_vertex=ready_fragment=NULL;
        ShaderModuleInfo mods[3]={{1},{1},{1}};PGRAPHVkState r={0};PGRAPHState pg={.vk_renderer_state=&r};
        PipelineKey key=native_key;PipelineBinding source;add_pipeline(&r,&source,&key,mods);
        /* A live source retains exact stage owners even after stage-cache eviction. */
        uint64_t hash=fast_hash((const uint8_t *)&key,sizeof(key));
        assert(queue_vertex_pipeline(&pg,&key,hash,&source,NULL));
        assert(!pump_jobs[7].prepare_vertex&&!pump_jobs[7].prepare_fragment&&!r.next_shader_module_identity);
        release_jobs();assert(mods[0].refs==1&&mods[1].refs==1&&mods[2].refs==1);
        reset();key=native_key;r.next_shader_module_identity=0;
        key.shader_state.vsh.specular_enable=true;key.shader_state.psh.final_inputs_0=77;
        hash=fast_hash((const uint8_t *)&key,sizeof(key));
        assert(queue_vertex_pipeline(&pg,&key,hash,&source,NULL));
        assert(pump_jobs[7].prepare_vertex&&pump_jobs[7].prepare_fragment&&r.next_shader_module_identity==2);
        /* A different VS cannot bypass the pending fragment owner and emit it again. */
        key.shader_state.vsh.uniform_attrs++;
        hash=fast_hash((const uint8_t *)&key,sizeof(key));
        assert(!queue_vertex_pipeline(&pg,&key,hash,&source,NULL));
        assert(published==1&&r.next_shader_module_identity==2);
        release_jobs();assert(mods[0].refs==1&&mods[1].refs==1&&mods[2].refs==1);cases+=2;
    }
    puts("Vertex lighting family: exact full-PSO readiness, forced collisions, eight key mismatches, cache/queue pressure, final-slot ownership and pending-stage deduplication PASS");
    printf("%u family cases; no compiler or native creation entry points in foreground fixture\n",cases);
}
