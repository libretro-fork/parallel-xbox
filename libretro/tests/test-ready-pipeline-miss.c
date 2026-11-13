/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "qemu/lru.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define QEMU_ALWAYS_INLINE __attribute__((always_inline))
#define NV2A_VK_DPRINTF(...) ((void)0)
#define nv2a_profile_inc_counter(...) ((void)0)
typedef struct ShaderModuleInfo { unsigned refs; } ShaderModuleInfo;
typedef struct ShaderBinding {
 LruNode node; ShaderState state;
 struct {ShaderModuleInfo *module_info;} vsh,geom,psh;
} ShaderBinding;
/* TYPES */
typedef struct PGRAPHVkState {
 Lru shader_cache,pipeline_cache; ShaderBinding *shader_binding;
 PGRAPHState *shader_cache_owner; bool dyn_vertex_input;
 VkVertexInputBindingDescription vertex_binding_descriptions[16];
 VkVertexInputAttributeDescription vertex_attribute_descriptions[16];
 unsigned num_active_vertex_binding_descriptions,num_active_vertex_attribute_descriptions;
} PGRAPHVkState;
struct PGRAPHState { PGRAPHVkState *vk_renderer_state; unsigned variant; };
static bool geometry;
static unsigned queries,fallbacks,uniforms,evictions;
static ShaderModuleInfo fallback[3];
static uint64_t fast_hash(const uint8_t *p,size_t n){(void)p;(void)n;return 1;}
bool pgraph_glsl_need_geom(const GeomState *g){(void)g;return geometry;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m){assert(m&&m->refs);m->refs++;}
static void unref(ShaderModuleInfo *m){if(m){assert(m->refs);m->refs--;}}
static void init_render_pass_state(PGRAPHState *pg,RenderPassState *s){s->color_format=VK_FORMAT_R8G8B8A8_UNORM;s->zeta_format=VK_FORMAT_D24_UNORM_S8_UINT;s->color_format+=pg->variant;}
static void pipeline_reg_values(PGRAPHState *pg,uint32_t *regs){for(unsigned i=0;i<9;i++)regs[i]=i*13+pg->variant;}
/* KEY AND QUERY */
static bool pgraph_vk_ref_ready_pipeline_modules(PGRAPHState *pg,ShaderBinding *b){queries++;return ready_query(pg,b);}
static void pgraph_vk_ref_state_modules(PGRAPHVkState *r,const ShaderState *s,ShaderModuleInfo **g,ShaderModuleInfo **v,ShaderModuleInfo **p){
 (void)r;(void)s;fallbacks++;*v=&fallback[0];*g=geometry?&fallback[1]:NULL;*p=&fallback[2];pgraph_vk_ref_shader_module(*v);if(*g)pgraph_vk_ref_shader_module(*g);pgraph_vk_ref_shader_module(*p);
}
static void update_shader_uniform_locs(ShaderBinding *b){assert(b->vsh.module_info&&b->psh.module_info&&(!geometry||b->geom.module_info));uniforms++;}
/* INITIALIZERS */
static bool compare(Lru *l,LruNode *n,const void *key){(void)l;return memcmp(&container_of(n,ShaderBinding,node)->state,key,sizeof(ShaderState));}
static void evict(Lru *l,LruNode *n){(void)l;ShaderBinding *b=container_of(n,ShaderBinding,node);unref(b->vsh.module_info);unref(b->geom.module_info);unref(b->psh.module_info);evictions++;}
int main(void){
 PGRAPHVkState *r=calloc(1,sizeof(*r));assert(r);unsigned cases=0;
 for(unsigned trial=0;trial<4096;trial++){
  ShaderBinding entries[2]={0},desired={0};PipelineBinding pipeline={0};
  ShaderModuleInfo owners[3]={{1},{1},{1}};
  for(unsigned i=0;i<3;i++)fallback[i].refs=1;
  geometry=trial&1;r->dyn_vertex_input=trial&2;
  PGRAPHState pg={r,trial%7};r->shader_cache_owner=&pg;
  r->num_active_vertex_binding_descriptions=r->num_active_vertex_attribute_descriptions=1;
  r->vertex_binding_descriptions[0]=(VkVertexInputBindingDescription){0,16,VK_VERTEX_INPUT_RATE_VERTEX};
  r->vertex_attribute_descriptions[0]=(VkVertexInputAttributeDescription){0,0,VK_FORMAT_R32G32B32A32_SFLOAT,0};
  desired.state.vsh.programmable.program_data[0][0]=trial+1;
  desired.state.psh.final_inputs_0=trial+19;
  init_pipeline_key_for_shader(&pg,&pipeline.key,&desired.state);
  pipeline.pipeline=(VkPipeline)(uintptr_t)1;
  for(unsigned i=0;i<3;i++)pipeline.shader_modules[i]=i==1&&!geometry?NULL:&owners[i];
  unsigned fail=(trial>>4)%10;
  if(fail==1)pipeline.pipeline=VK_NULL_HANDLE;
  if(fail==2)pipeline.key.clear=true;
  if(fail==3)pipeline.shader_modules[0]=NULL;
  if(fail==4)pipeline.shader_modules[2]=NULL;
  if(fail==5&&geometry)pipeline.shader_modules[1]=NULL;
  if(fail==6)pipeline.key.shader_state.psh.final_inputs_0^=1;
  if(fail==7)pipeline.key.regs[3]^=1;
  if(fail==8){if(r->dyn_vertex_input)pipeline.key.render_pass_state.color_format=VK_FORMAT_UNDEFINED;else pipeline.key.binding_descriptions[0].stride=32;}
  lru_init(&r->shader_cache);lru_init(&r->pipeline_cache);
  pipeline.node.hash=fail==9?1+LRU_NUM_BINS:1;
  QTAILQ_INSERT_HEAD(&r->pipeline_cache.bins[lru_hash_to_bin(&r->pipeline_cache,pipeline.node.hash)],&pipeline.node,next_bin);
  r->shader_cache.init_node=shader_cache_entry_init;r->shader_cache.compare_nodes=compare;r->shader_cache.post_node_evict=evict;
  lru_add_free(&r->shader_cache,&entries[0].node);
  if(!(trial&8))lru_add_free(&r->shader_cache,&entries[1].node);
  ShaderState dummy={0};r->shader_binding=container_of(lru_lookup(&r->shader_cache,1,&dummy),ShaderBinding,node);
  queries=fallbacks=uniforms=evictions=0;
  ShaderStateView view={.head=&desired.state};memcpy(&view.tail,(uint8_t*)&desired.state+VK_SHADER_HEAD_BYTES,sizeof(view.tail));
  static const LruKeyOps ops={.init_node=shader_cache_entry_init_view,.compare_nodes=shader_cache_entry_compare_view};
  LruNode *n=(trial&4)?lru_lookup_with_ops(&r->shader_cache,1,&view,&ops):lru_lookup(&r->shader_cache,1,&desired.state);
  ShaderBinding *b=container_of(n,ShaderBinding,node);
  bool ready=fail==0||(fail==5&&!geometry);
  assert(queries==1&&fallbacks==!ready&&uniforms==1&&evictions==!!(trial&8));
  assert(!memcmp(&b->state,&desired.state,sizeof(desired.state)));
  assert(b->vsh.module_info==(ready?&owners[0]:&fallback[0]));
  assert(b->psh.module_info==(ready?&owners[2]:&fallback[2]));
  assert(b->geom.module_info==(!geometry?NULL:ready?&owners[1]:&fallback[1]));
  assert(lru_lookup(&r->shader_cache,1,&desired.state)==n&&queries==1&&fallbacks==!ready&&uniforms==1);
  /* Native PSO owner may retire while the new shader binding remains live. */
  for(unsigned i=0;i<3;i++)unref(&owners[i]);
  if(ready)assert(owners[0].refs==1&&owners[2].refs==1);
  lru_flush(&r->shader_cache);
  for(unsigned i=0;i<3;i++)assert(owners[i].refs==0&&fallback[i].refs==1);
  cases++;
 }
 printf("Ready pipeline cache miss: %u native-LRU state/view, full-key collision, static/dynamic input, pressure, incomplete owner, eviction and hot-hit cases PASS\n",cases);free(r);
}

