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
#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include <encodings/xxh3.h>
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define VSH_UBO_BINDING 0
/* Scalar/default ISA identity oracle; backend equivalence has its own suite. */
uint64_t cpu_features_get(void){return 0;}
/* TYPES */
typedef struct ShaderModuleInfo {unsigned refs;} ShaderModuleInfo;
typedef struct ShaderModuleCacheEntry {LruNode node;ShaderModuleCacheKey key;ShaderModuleInfo *module_info;} ShaderModuleCacheEntry;
typedef struct PGRAPHVkState {Lru shader_module_cache;bool use_push_constants_for_uniform_attrs;} PGRAPHVkState;
static bool geometry;
bool pgraph_glsl_need_geom(const GeomState *g){(void)g;return geometry;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m){assert(m&&m->refs);m->refs++;}
static void evict(Lru *l,LruNode *n){(void)l;ShaderModuleInfo *m=container_of(n,ShaderModuleCacheEntry,node)->module_info;assert(m&&m->refs);m->refs--;}
/* PRODUCTION */
/* Frozen pre-refactor lighting projection, independent of current helper. */
static bool reference(VshState *vsh){
 if(!vsh->is_fixed_function)return false;
 FixedFunctionVshState *f=&vsh->fixed_function;
 f->csv0c_dynamic=true;f->lighting=false;f->local_eye=false;f->normalization=false;
 f->emission_src=0;f->ambient_src=0;f->diffuse_src=0;f->specular_src=0;
 memset(f->light,0,sizeof(f->light));f->light_dynamic=false;f->two_sided=false;
 vsh->specular_enable=false;vsh->separate_specular=false;vsh->ignore_specular_alpha=false;return true;
}
static uint32_t random_state=0x73611234;
static uint32_t next(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
int main(void){
 for(unsigned trial=0;trial<100000;trial++){
  VshState a,b;
  for(size_t i=0;i<sizeof(a);i++)((uint8_t*)&a)[i]=(uint8_t)next();
  a.is_fixed_function=trial&1;b=a;
  bool expected=reference(&a),got=pgraph_glsl_vsh_canonicalize_lighting(&b);
  assert(expected==got&&!memcmp(&a,&b,sizeof(a)));
  assert(pgraph_glsl_vsh_canonicalize_lighting(&b)==got&&!memcmp(&a,&b,sizeof(a)));
 }
 PGRAPHVkState *r=calloc(1,sizeof(*r));assert(r);
 for(unsigned trial=0;trial<4096;trial++){
  ShaderModuleCacheEntry entries[2];memset(entries,0xa5,sizeof(entries));
  ShaderModuleInfo owner={1},duplicate={1};ShaderState state;
  for(size_t i=0;i<sizeof(state);i++)((uint8_t*)&state)[i]=(uint8_t)next();
  geometry=trial&1;r->use_push_constants_for_uniform_attrs=trial&2;
  lru_init(&r->shader_module_cache);r->shader_module_cache.post_node_evict=evict;
  for(unsigned i=0;i<2;i++)lru_add_free(&r->shader_module_cache,&entries[i].node);
  ShaderModuleCacheKey keys[2];int n=vk_pre_state_module_keys(&state,r->use_push_constants_for_uniform_attrs,keys);assert(n==1+geometry);
  VertexModuleView view;uint64_t hash=vertex_module_view(r,&state,&view);
  assert(view.state==&state.vsh&&hash==fast_hash((const uint8_t*)&keys[n-1],sizeof(keys[0])));
  assert(!pgraph_vk_ready_vertex_module(r,&state));
  pgraph_vk_adopt_vertex_module(r,&state,&owner);
  assert(owner.refs==2&&pgraph_vk_ready_vertex_module(r,&state)==&owner);
  ShaderModuleCacheEntry *entry=container_of(QTAILQ_FIRST(&r->shader_module_cache.global),ShaderModuleCacheEntry,node);
  assert(!memcmp(&entry->key,&keys[n-1],sizeof(entry->key)));
  pgraph_vk_adopt_vertex_module(r,&state,&duplicate);
  assert(duplicate.refs==1&&pgraph_vk_ready_vertex_module(r,&state)==&owner&&owner.refs==2);
  /* A different full key deliberately occupies this hash bin. */
  entry->key.vsh.state.programmable.program_data[0][0]^=1;
  assert(!pgraph_vk_ready_vertex_module(r,&state));
  pgraph_vk_adopt_vertex_module(r,&state,&duplicate);
  assert(duplicate.refs==2&&pgraph_vk_ready_vertex_module(r,&state)==&duplicate);
  owner.refs--;duplicate.refs--;lru_flush(&r->shader_module_cache);
  assert(!owner.refs&&!duplicate.refs);
 }
 free(r);puts("Vertex module view: 100000 frozen lighting projections and 4096 exact full-key/hash, poisoned final-node, collision, duplicate-owner and eviction lifetimes PASS");
}
