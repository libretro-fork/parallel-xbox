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
typedef struct ShaderModuleInfo { unsigned refs; } ShaderModuleInfo;
typedef struct ShaderBinding {
 LruNode node; ShaderState state;
 struct { ShaderModuleInfo *module_info; } vsh,geom,psh;
} ShaderBinding;
/* TYPES */
typedef struct PGRAPHVkState { Lru shader_cache,pipeline_cache; ShaderBinding *shader_binding; } PGRAPHVkState;
static bool geometry,collisions;
static unsigned uniforms,ordinary,evictions;
static ShaderModuleInfo *initial[3];
static uint64_t fast_hash(const uint8_t *p,size_t n) {
 if(collisions)return 1;
 uint64_t h=1469598103934665603ull;while(n--)h=(h^*p++)*1099511628211ull;return h;
}
bool pgraph_glsl_need_geom(const GeomState *s){(void)s;return geometry;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m){assert(m&&m->refs);m->refs++;}
static void unref(ShaderModuleInfo *m){if(m){assert(m->refs);m->refs--;}}
static void update_shader_uniform_locs(ShaderBinding *b){assert(b->vsh.module_info&&b->psh.module_info&&(!geometry||b->geom.module_info));uniforms++;}
static void ordinary_init(Lru *l,LruNode *node,const void *key){
 ShaderBinding *b=container_of(node,ShaderBinding,node);memcpy(&b->state,key,sizeof(b->state));
 b->vsh.module_info=initial[0];b->geom.module_info=initial[1];b->psh.module_info=initial[2];ordinary++;
}
static bool ordinary_compare(Lru *l,LruNode *node,const void *key){return memcmp(&container_of(node,ShaderBinding,node)->state,key,sizeof(ShaderState));}
static void binding_evict(Lru *l,LruNode *node){
 ShaderBinding *b=container_of(node,ShaderBinding,node);unref(b->vsh.module_info);unref(b->geom.module_info);unref(b->psh.module_info);evictions++;
}
static void pipeline_init(Lru *l,LruNode *node,const void *key){PipelineBinding *p=container_of(node,PipelineBinding,node);p->key=*(const PipelineKey*)key;}
static bool pipeline_compare(Lru *l,LruNode *node,const void *key){return memcmp(&container_of(node,PipelineBinding,node)->key,key,sizeof(PipelineKey));}
static void pipeline_evict(Lru *l,LruNode *node){
 PipelineBinding *p=container_of(node,PipelineBinding,node);
 for(unsigned i=0;i<3;i++){unref(p->shader_modules[i]);p->shader_modules[i]=NULL;}
}
/* PRODUCTION */
int main(void){
 PGRAPHVkState *r=calloc(1,sizeof(*r));assert(r);unsigned cases=0;
 for(unsigned gen=1;gen<=8;gen++)for(unsigned bits=0;bits<512;bits++){
  ShaderBinding entries[3]={0};PipelineBinding pipelines[2]={0};
  ShaderModuleInfo owners[6]={{1},{1},{1},{1},{1},{1}};
  ShaderState full={0},state,expected;PipelineKey key={0};
  geometry=!!(bits&32);collisions=!!(gen&1);uniforms=ordinary=evictions=0;
  full.vsh.programmable.program_data[0][0]=gen;full.psh.final_inputs_0=gen;full.psh.shader_stage_program=PS_TEXTUREMODES_PROJECT2D;
  state=full;state.psh.combiner_dynamic=true;state.psh.alpha_dynamic=true;state.psh.final_inputs_0=0;
  if(bits&8)state.vsh.programmable.program_data[0][0]^=0x8000;
  if(bits&16)state.psh.depth_format=DEPTH_FORMAT_UI_CLAMP;
  expected=state;key.shader_state=state;key.regs[0]=gen;
  lru_init(&r->shader_cache);lru_init(&r->pipeline_cache);
  r->shader_cache.init_node=ordinary_init;r->shader_cache.compare_nodes=ordinary_compare;r->shader_cache.post_node_evict=binding_evict;
  r->pipeline_cache.init_node=pipeline_init;r->pipeline_cache.compare_nodes=pipeline_compare;r->pipeline_cache.post_node_evict=pipeline_evict;
  initial[0]=&owners[0];initial[1]=geometry?&owners[1]:NULL;initial[2]=&owners[2];
  lru_add_free(&r->shader_cache,&entries[0].node);
  if(bits&1)lru_add_free(&r->shader_cache,&entries[1].node);
  if((bits&257)==257)lru_add_free(&r->shader_cache,&entries[2].node);
  uint64_t fh=fast_hash((const uint8_t*)&full,sizeof(full));
  r->shader_binding=container_of(lru_lookup(&r->shader_cache,fh,&full),ShaderBinding,node);
  key.clear=!!(gen&4);uint64_t ph=fast_hash((const uint8_t*)&key,sizeof(key));
  PipelineKey stored=key;if(bits&4)stored.shader_state.psh.shader_stage_program^=0x8000;
  lru_add_free(&r->pipeline_cache,&pipelines[0].node);
  PipelineBinding *p=container_of(lru_lookup(&r->pipeline_cache,ph,&stored),PipelineBinding,node);
  p->pipeline=(gen&2)?VK_NULL_HANDLE:(VkPipeline)(uintptr_t)17;
  p->shader_modules[0]=bits&64?NULL:&owners[3];p->shader_modules[1]=geometry&&!(bits&128)?&owners[5]:NULL;p->shader_modules[2]=bits&2?NULL:&owners[4];
  /* A full-key collision must not provide readiness or borrowed metadata. */
  PipelineKey other=key;other.regs[0]^=0x5555;lru_add_free(&r->pipeline_cache,&pipelines[1].node);
  PipelineBinding *collision=container_of(lru_lookup(&r->pipeline_cache,ph,&other),PipelineBinding,node);collision->pipeline=(VkPipeline)(uintptr_t)18;
  uint64_t sh=fast_hash((const uint8_t*)&state,sizeof(state));
  if((bits&256)&&(bits&1)){initial[0]=initial[1]=initial[2]=NULL;lru_lookup(&r->shader_cache,sh,&state);ordinary--;}
  bool ok=(bits&1)&&!(bits&(2|4|64|256))&&(!geometry||!(bits&128))&&!(gen&(2|4));
  unsigned before=ordinary;ShaderBinding *b=pgraph_vk_materialize_ready_shader_binding(r,&key,ph);
  assert((b!=NULL)==ok);assert(ordinary==before&&evictions==0&&uniforms==ok);
  if(ok){
   assert(b!=r->shader_binding&&!memcmp(&b->state,&state,sizeof(state)));
   assert(b->vsh.module_info==&owners[3]&&b->psh.module_info==&owners[4]&&b->geom.module_info==(geometry?&owners[5]:NULL));
   assert(owners[3].refs==2&&owners[4].refs==2&&(!geometry||owners[5].refs==2));
   assert(pgraph_vk_materialize_ready_shader_binding(r,&key,ph)==b&&uniforms==1);
   assert(owners[3].refs==2&&owners[4].refs==2);
   memset(&key,0xcd,sizeof(key));lru_flush(&r->pipeline_cache);
   assert(owners[3].refs==1&&owners[4].refs==1&&(!geometry||owners[5].refs==1));
   lru_evict_node(&r->shader_cache,&r->shader_binding->node);
   assert(owners[0].refs==0&&owners[2].refs==0);
   assert(!memcmp(&b->state,&expected,sizeof(expected))&&b->vsh.module_info->refs==1&&b->psh.module_info->refs==1);
   lru_flush(&r->shader_cache);assert(owners[3].refs==0&&owners[4].refs==0&&(!geometry||owners[5].refs==0));
  }else{
   for(unsigned i=0;i<6;i++)assert(owners[i].refs==1);
   lru_flush(&r->shader_cache);lru_flush(&r->pipeline_cache);
  }
  cases++;
 }
 printf("Ready PSO binding: %u exact full-key/collision, absent PSO/module, vertex+geometry variants, pressure, partial-entry and owner-eviction cases PASS; final state=%zu bytes, scoped pointers=%zu bytes\n",cases,sizeof(ShaderState),sizeof(ReadyBindingInput));free(r);
}
