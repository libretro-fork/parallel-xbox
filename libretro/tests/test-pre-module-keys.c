/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define VSH_UBO_BINDING 0
#define PSH_UBO_BINDING 1
#define PSH_TEX_BINDING 2
/* TYPES */
typedef struct {bool use_push_constants_for_uniform_attrs;} PGRAPHVkState;
typedef struct {unsigned id;} ShaderModuleInfo;
static bool need_geometry;
bool pgraph_glsl_need_geom(const GeomState *s){(void)s;return need_geometry;}
static ShaderModuleInfo modules[3];
static ShaderModuleCacheKey captured[3];static unsigned lookups;
static ShaderModuleInfo *get_and_ref_shader_module_for_key(PGRAPHVkState *r,const ShaderModuleCacheKey *key){(void)r;assert(lookups<3);memcpy(&captured[lookups],key,sizeof(*key));return &modules[lookups++];}
#ifdef CHECKED
static size_t cleared;
static void *count_clear(void *dst,int v,size_t n){cleared+=n;return memset(dst,v,n);}
#define memset count_clear
#endif
/* FUNCTIONS */
#ifdef CHECKED
#undef memset
#endif
#if defined(REFERENCE_VERTEX_CLEAR) || defined(REFERENCE_FRAGMENT_CLEAR)
#define REFERENCE_PRE(r,s,g,v) reference_pre_refs(r,s,g,v)
#else
#define REFERENCE_PRE(r,s,g,v) reference_refs(r,s,g,v,NULL)
#endif
static uint32_t seed=0x793ac624;
static uint32_t random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void correctness(void){
 ShaderState state;for(size_t i=0;i<sizeof(state);i++)((uint8_t*)&state)[i]=(uint8_t)random_value();
 for(unsigned trial=0;trial<100000;trial++){
  ((uint8_t*)&state)[random_value()%sizeof(state)]=(uint8_t)random_value();
  state.psh.depth_format=trial&4?DEPTH_FORMAT_UI_CLAMP:DEPTH_FORMAT_D24;state.psh.shader_stage_program=(uint32_t)PS_TEXTUREMODES_PROJECT2D<<((trial%4)*5);
  need_geometry=(trial&1)!=0;PGRAPHVkState r={.use_push_constants_for_uniform_attrs=(trial&2)!=0};
  ShaderModuleCacheKey a[3],b[3];memset(a,0x97,sizeof(a));memset(b,0x38,sizeof(b));
#ifdef CHECKED
  cleared=0;
#endif
  int na=reference_keys(&state,r.use_push_constants_for_uniform_attrs,a);
#ifdef CHECKED
  size_t old_full_clear=cleared;cleared=0;
#endif
  int nb=vk_state_module_keys(&state,r.use_push_constants_for_uniform_attrs,b);
#ifdef CHECKED
  assert(cleared==(size_t)nb*sizeof(ShaderModuleCacheKey)-sizeof(state.vsh)-sizeof(state.psh));
#ifdef REFERENCE_FRAGMENT_CLEAR
  assert(old_full_clear-cleared==sizeof(state.psh));
#else
  assert(old_full_clear-cleared==sizeof(state.vsh)+sizeof(state.psh));
#endif
#endif
  assert(na==nb&&!memcmp(a,b,na*sizeof(a[0])));
  ShaderModuleInfo *ga,*va,*gb,*vb,*psh;
#ifdef CHECKED
  cleared=0;
#endif
  lookups=0;REFERENCE_PRE(&r,&state,&ga,&va);unsigned expected=lookups;
#ifdef CHECKED
  size_t old_cleared=cleared;
#endif
  ShaderModuleCacheKey old[3];memcpy(old,captured,sizeof(old));
#ifdef CHECKED
  cleared=0;
#endif
  lookups=0;pgraph_vk_ref_pre_state_modules(&r,&state,&gb,&vb);
  assert(lookups==expected&&lookups==1+(unsigned)need_geometry&&ga==gb&&va==vb&&!memcmp(old,captured,lookups*sizeof(old[0])));
#ifdef CHECKED
  assert(cleared==lookups*sizeof(ShaderModuleCacheKey)-sizeof(state.vsh));
#ifdef REFERENCE_FRAGMENT_CLEAR
  assert(old_cleared==cleared);
#elif defined(REFERENCE_VERTEX_CLEAR)
  assert(old_cleared-cleared==sizeof(state.vsh));
#else
  assert(old_cleared-cleared==sizeof(ShaderModuleCacheKey)+sizeof(state.vsh));
#endif
#endif
  lookups=0;pgraph_vk_ref_state_modules(&r,&state,&gb,&vb,&psh);
  assert(lookups==(unsigned)na&&ga==gb&&va==vb&&psh==&modules[na-1]&&!memcmp(a,captured,na*sizeof(a[0])));
 }
 puts("100000 states: complete keys unchanged; prewarm prefix/ref order identical; push constants and geometry clamp preserved; counted build verifies selective clearing");
}
#ifndef CHECKED
static volatile unsigned sink;
__attribute__((noinline)) static int baseline_full(const ShaderState *s,bool p,ShaderModuleCacheKey *k){return reference_keys(s,p,k);}
__attribute__((noinline)) static int current_full(const ShaderState *s,bool p,ShaderModuleCacheKey *k){return vk_state_module_keys(s,p,k);}
static double bench(unsigned current,unsigned full,unsigned geom){
 ShaderState state;memset(&state,0,sizeof(state));ShaderModuleCacheKey keys[3];PGRAPHVkState r={true};need_geometry=geom;
 struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<1000000;i++){
  state.vsh.programmable.program_data[0][0]=i;
  if(full){int(*run)(const ShaderState*,bool,ShaderModuleCacheKey*)=current?current_full:baseline_full;sink=run(&state,true,keys);}
  else{ShaderModuleInfo *g,*v;lookups=0;if(current)pgraph_vk_ref_pre_state_modules(&r,&state,&g,&v);else REFERENCE_PRE(&r,&state,&g,&v);sink=lookups;}
 }
 timespec_get(&b,TIME_UTC);return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/1000000;
}
#endif
int main(void){correctness();printf("module-key=%zu vertex-state=%zu fragment-state=%zu bytes; omitted overwritten clears checked\n",sizeof(ShaderModuleCacheKey),sizeof(VshState),sizeof(PshState));
#ifndef CHECKED
 puts("Isolated key builders/modeled lookup capture; no shader compilation/GPU timing; ns/state");
 for(unsigned round=0;round<5;round++)for(unsigned full=0;full<2;full++)for(unsigned geom=0;geom<2;geom++){
  double t[2];for(unsigned k=0;k<2;k++){unsigned w=k^(round&1);t[w]=bench(w,full,geom);}
  printf("round=%u full=%u geometry=%u baseline=%.3f current=%.3f ns/state\n",round,full,geom,t[0],t[1]);
 }
#endif
}
