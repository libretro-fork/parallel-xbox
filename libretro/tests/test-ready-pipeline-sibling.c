/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define QTAILQ_FOREACH(n,h,f) for((n)=*(h);(n);(n)=(n)->next)
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
typedef struct LruNode {uint64_t hash;struct LruNode *next;} LruNode;
typedef struct {LruNode *bins[8];} Lru;
typedef struct {unsigned refs;} ShaderModuleInfo;
typedef struct ShaderBinding {LruNode node;ShaderState state;struct{ShaderModuleInfo *module_info;}vsh,geom,psh;} ShaderBinding;
/* TYPES */
typedef struct {PipelineKey key;uint64_t hash,state_hash;ShaderModuleInfo *mods[3];unsigned nmods;} LibJob;
typedef struct {Lru shader_cache,pipeline_cache;ShaderBinding *shader_binding;PipelineBinding *pipeline_binding;bool shader_bindings_changed,pipeline_binding_changed,ahead_valid,sibling_retry;unsigned sibling_ticket,sibling_generation;} PGRAPHVkState;
struct PGRAPHState {PGRAPHVkState *vk_renderer_state;bool program_data_dirty;};
typedef struct {PGRAPHState pgraph;} NV2AState;
static bool feature,geometry,collision,full_queue,inflight,force_dirty;
static unsigned pump_w_local;
unsigned pgraph_glsl_dynamic_gen=7;
static unsigned sync_calls,fills,publishes,uniforms,touches,pipe_count;
static PipelineKey active_key,sync_key;
static ShaderBinding *full_binding,*selected;
static PipelineBinding pipes[8];static LibJob slot;
static bool pgraph_gpu_boost_gl(void){return feature;}
bool pgraph_glsl_need_geom(const GeomState *s){(void)s;return geometry;}
static unsigned lru_hash_to_bin(Lru *l,uint64_t h){(void)l;return h%8;}
static uint64_t fast_hash(const uint8_t *p,size_t n){if(collision)return 1;uint64_t h=1469598103934665603ull;while(n--)h=(h^*p++)*1099511628211ull;return h;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m){assert(m&&m->refs);m->refs++;}
static void unref(ShaderModuleInfo *m){assert(m&&m->refs);m->refs--;}
static ShaderBinding *get_shader_binding_for_state(PGRAPHVkState *r,const ShaderState *key,uint64_t h){
 LruNode *n;QTAILQ_FOREACH(n,&r->shader_cache.bins[h%8],next_bin){ShaderBinding *b=container_of(n,ShaderBinding,node);if(n->hash==h&&!memcmp(&b->state,key,sizeof(*key))){touches++;return b;}}assert(false);return NULL;
}
static void update_shader_uniforms(PGRAPHState *pg){assert(pg->vk_renderer_state->shader_binding==selected);uniforms++;}
static PipelineBinding *add_pipe(PGRAPHVkState *r,const PipelineKey *key,bool ready){assert(pipe_count<8);PipelineBinding *b=&pipes[pipe_count++];memset(b,0,sizeof(*b));b->key=*key;b->node.hash=fast_hash((const uint8_t *)key,sizeof(*key));b->pipeline=ready?(VkPipeline)(uintptr_t)(pipe_count+100):VK_NULL_HANDLE;unsigned bin=b->node.hash%8;b->node.next=r->pipeline_cache.bins[bin];r->pipeline_cache.bins[bin]=&b->node;return b;}
static LruNode *lru_lookup(Lru *l,uint64_t h,const void *key){LruNode *n;QTAILQ_FOREACH(n,&l->bins[h%8],next_bin){PipelineBinding *b=container_of(n,PipelineBinding,node);if(n->hash==h&&!memcmp(&b->key,key,sizeof(b->key)))return n;}PGRAPHVkState *r=container_of(l,PGRAPHVkState,pipeline_cache);PipelineBinding *b=add_pipe(r,key,false);memset(&b->key,0xcc,sizeof(b->key));return &b->node;}
static bool pump_full_in_flight(uint64_t h,const PipelineKey *k){assert(h==fast_hash((const uint8_t *)k,sizeof(*k)));return inflight;}
static LibJob *pump_slot(void){if(full_queue)return NULL;memset(&slot,0,sizeof(slot));return &slot;}
static void pipeline_job_fill(PGRAPHState *pg,const ShaderState *s,const ShaderModuleInfo *v,const ShaderModuleInfo *g,const ShaderModuleInfo *p,const PipelineKey *key,LibJob *job){assert(pg&&s==&job->key.shader_state&&key==&job->key);assert(v==full_binding->vsh.module_info&&g==full_binding->geom.module_info&&p==full_binding->psh.module_info);assert(v->refs==2&&p->refs==2&&(!g||g->refs==2));fills++;}
static void pump_submit(LibJob *job){assert(job==&slot&&job->nmods==2u+geometry);publishes++;pump_w_local++;}
static void pgraph_vk_spec_pipeline_queued(uint64_t h){assert(h==full_binding->node.hash);}
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
#define NV2A_VK_DPRINTF(...) ((void)0)
#define NV2A_PROF_PIPELINE_GEN 0
static void nv2a_profile_inc_counter(unsigned x){(void)x;}
static void pgraph_vk_bind_textures(NV2AState *d){assert(d);}
static void pgraph_vk_bind_shaders(PGRAPHState *pg){PGRAPHVkState *r=pg->vk_renderer_state;r->shader_bindings_changed=false;if(pg->program_data_dirty||r->sibling_retry){r->shader_binding=full_binding;pg->program_data_dirty=false;r->shader_bindings_changed=true;}}
static bool check_pipeline_dirty(PGRAPHState *pg){return force_dirty||pg->vk_renderer_state->shader_bindings_changed;}
static void pgraph_clear_dirty_reg_map(PGRAPHState *pg){(void)pg;}
static void init_pipeline_key(PGRAPHState *pg,PipelineKey *key){*key=active_key;key->shader_state=pg->vk_renderer_state->shader_binding->state;}
static void pipeline_ahead(PGRAPHState *pg,const PipelineKey *key){(void)pg;(void)key;}
/* PRODUCTION */
int main(void){
 (void)pipeline_ready;(void)pump_full_in_flight;
 unsigned cases=0;uint64_t snapshots=0;
 for(unsigned gen=0;gen<128;gen++)for(unsigned bits=0;bits<256;bits++){
  PGRAPHVkState r={0};NV2AState d={.pgraph={.vk_renderer_state=&r}};PGRAPHState *pg=&d.pgraph;
  ShaderModuleInfo owners[3]={{1},{1},{1}},fbowners[3]={{1},{1},{1}};ShaderBinding full={0},fb={0};full_binding=&full;
  feature=bits&1;geometry=bits&32;collision=bits&64;full_queue=bits&8;inflight=bits&16;force_dirty=true;
  pump_w_local=19;sync_calls=fills=publishes=uniforms=touches=pipe_count=0;memset(&slot,0,sizeof(slot));memset(&active_key,0,sizeof(active_key));
  full.state.vsh.programmable.program_data[0][0]=gen;full.state.psh.combiner_control=gen+1;full.state.psh.alpha_test=true;full.state.psh.final_inputs_0=gen*91;full.state.psh.combiner_dynamic=!!(gen&16);
  full.vsh.module_info=&owners[0];full.geom.module_info=geometry?&owners[1]:NULL;full.psh.module_info=&owners[2];full.node.hash=fast_hash((const uint8_t *)&full.state,sizeof(full.state));r.shader_binding=&full;
  active_key.shader_state=full.state;active_key.render_pass_state.color_format=VK_FORMAT_R8G8B8A8_UNORM;active_key.render_pass_state.zeta_format=gen&1?VK_FORMAT_D16_UNORM:VK_FORMAT_UNDEFINED;active_key.regs[gen%9]=gen;active_key.binding_descriptions[0].stride=gen+16;active_key.attribute_descriptions[0].offset=gen%16;
  PipelineKey original=active_key,projected=original;bool opt=feature;feature=true;pgraph_glsl_psh_canonicalize_dynamic(&projected.shader_state.psh);feature=opt;
  fb.state=projected.shader_state;fb.vsh.module_info=gen&2?NULL:&fbowners[0];fb.geom.module_info=geometry&&!(gen&4)?&fbowners[1]:NULL;fb.psh.module_info=gen&32?NULL:&fbowners[2];if(gen&64)fb.state.psh.g8b8_mip_flags^=1;
  fb.node.hash=collision?1:fast_hash((const uint8_t *)&projected.shader_state,sizeof(projected.shader_state));if(bits&2)r.shader_cache.bins[fb.node.hash%8]=&fb.node;selected=&fb;
  PipelineKey cached=projected;if(gen&8){switch(gen%6){case 0:cached.render_pass_state.color_format=VK_FORMAT_R5G6B5_UNORM_PACK16;break;case 1:cached.render_pass_state.zeta_format=VK_FORMAT_D24_UNORM_S8_UINT;break;case 2:cached.regs[0]^=0x100;break;case 3:cached.binding_descriptions[0].stride++;break;case 4:cached.attribute_descriptions[0].format=VK_FORMAT_R8G8B8A8_UNORM;break;case 5:cached.attribute_descriptions[0].offset++;break;}}
  if((bits&4)&&!(gen&16))add_pipe(&r,&cached,true);
  bool full_ready=bits&128;if(full_ready)add_pipe(&r,&original,true);
  bool use=!full_ready&&feature&&(bits&2)&&(bits&4)&&!(gen&(2|8|16|32|64))&&(!geometry||!(gen&4));
  create_pipeline(pg);
  assert(sync_calls==(!full_ready&&!use));assert((pg->program_data_dirty||r.sibling_retry)==use);
  assert(uniforms==use&&touches==use);
  if(use){assert(r.shader_binding==&fb&&r.pipeline_binding&&r.pipeline_binding->pipeline);assert(!memcmp(&r.pipeline_binding->key,&projected,sizeof(projected)));assert(fills==(!full_queue&&!inflight)&&publishes==fills);
   if(!fills){full_queue=inflight=false;force_dirty=false;create_pipeline(pg);assert(fills==1&&publishes==1&&r.sibling_retry&&!pg->program_data_dirty&&r.shader_binding==&fb);}
   if(fills){assert(r.sibling_retry&&r.sibling_ticket==19&&r.sibling_generation==7);snapshots+=sizeof(slot.key);assert(!memcmp(&slot.key,&original,sizeof(original)));
    if(gen&1){memset(&full.state,0xcd,sizeof(full.state));for(unsigned i=0;i<3;i++)if(i!=1||geometry)unref(&owners[i]);assert(!memcmp(&slot.key,&original,sizeof(original)));for(unsigned i=0;i<slot.nmods;i++)unref(slot.mods[i]);for(unsigned i=0;i<3;i++)assert(owners[i].refs==(i==1&&!geometry));}
    else {PipelineBinding *b=container_of(lru_lookup(&r.pipeline_cache,slot.hash,&slot.key),PipelineBinding,node);b->pipeline=(VkPipeline)(uintptr_t)999;for(unsigned i=0;i<slot.nmods;i++)unref(slot.mods[i]);force_dirty=false;create_pipeline(pg);assert(r.shader_binding==&full&&!pg->program_data_dirty&&r.pipeline_binding==b&&sync_calls==0&&publishes==1);}
   } else {assert(!fills&&!publishes);}
  }else{assert(r.shader_binding==&full&&!fills&&!publishes);if(!full_ready)assert(!memcmp(&sync_key,&original,sizeof(original)));}
  cases++;
 }
 printf("Ready pipeline sibling: %u exact pass/register/vertex key, cache collision, partial binding, queue/inflight, owner eviction, retry and completed-adoption cases PASS; PSH projection=%zu bytes, queued key snapshot=%zu bytes, copied only on publication (%llu total fixture bytes)\n",cases,2*sizeof(PshState),sizeof(PipelineKey),(unsigned long long)snapshots);
}
