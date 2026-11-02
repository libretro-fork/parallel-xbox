/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include <libretro.h>
#include "qemu/lru.h"
#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
typedef struct {unsigned id;} ShaderSource;
typedef struct {unsigned refs,source;} ShaderModuleInfo;
/* TYPES */
typedef struct {
 Lru shader_module_cache;ShaderModuleCacheEntry entry;ShaderModuleInfo info;
 unsigned creates,fallbacks;
} PGRAPHVkState;
static uint64_t fixture_simd;
uint64_t cpu_features_get(void){return fixture_simd;}
static bool force_collision;
static uint64_t expected_hash;
#ifdef CHECKED
static unsigned hash_calls;
#endif
static uint64_t hash_value(const uint8_t *p,size_t n){return force_collision?7:fast_hash(p,n);}
static uint64_t counted_hash(const uint8_t *p,size_t n){
#ifdef CHECKED
 hash_calls++;
#endif
 return hash_value(p,n);
}
static ShaderSource sources[3]={{1},{2},{99}};
static VkSpecDone vk_spec_ready[3];
static unsigned order_storage[3]={0,1,2},*vk_spec_ready_order=order_storage,vk_spec_ready_n;
static ShaderSource *vk_spec_ready_take_at(unsigned i){unsigned slot=vk_spec_ready_order[i];ShaderSource *s=vk_spec_ready[slot].spirv;vk_spec_ready[slot].spirv=NULL;memmove(&order_storage[i],&order_storage[i+1],(--vk_spec_ready_n-i)*sizeof(unsigned));return s;}
static ShaderSource *vk_module_spirv(const ShaderModuleCacheKey *key){(void)key;return &sources[2];}
static ShaderModuleInfo *pgraph_vk_create_shader_module_from_spirv(PGRAPHVkState *r,ShaderSource *s){assert(!r->info.refs&&r->entry.node.hash==expected_hash);r->creates++;r->fallbacks+=s==&sources[2];r->info.source=s->id;return &r->info;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m){m->refs++;}
static void pgraph_vk_unref_shader_module(PGRAPHVkState *r,ShaderModuleInfo *m){(void)r;assert(m->refs);m->refs--;}
#define fast_hash counted_hash
/* FUNCTIONS */
#undef fast_hash
static PGRAPHVkState *setup(bool current){
 PGRAPHVkState *r=calloc(1,sizeof(*r));assert(r);lru_init(&r->shader_module_cache);
 r->shader_module_cache.init_node=current?shader_module_cache_entry_init:reference_init;
 r->shader_module_cache.compare_nodes=shader_module_cache_entry_compare;r->shader_module_cache.post_node_evict=shader_module_cache_entry_post_evict;
 lru_add_free(&r->shader_module_cache,&r->entry.node);return r;
}
static void ready(unsigned mode,const ShaderModuleCacheKey *key,uint64_t hash){
 for(unsigned i=0;i<3;i++)order_storage[i]=i;
 memset(vk_spec_ready,0,sizeof(vk_spec_ready));vk_spec_ready_n=mode==0?0:mode==3?2:1;
 for(unsigned i=0;i<vk_spec_ready_n;i++){vk_spec_ready[i].key_hash=hash;memcpy(&vk_spec_ready[i].key,key,sizeof(*key));vk_spec_ready[i].spirv=&sources[0];}
 if(mode>=2){((uint8_t*)&vk_spec_ready[0].key)[sizeof(*key)-1]^=1;vk_spec_ready[0].spirv=&sources[1];}
}
static uint32_t seed=0x7ab32418;
static uint32_t random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void correctness(void){
 PGRAPHVkState *a=setup(false),*b=setup(true);ShaderModuleCacheKey key,previous;uint64_t previous_hash=0;bool valid=false;
 for(size_t i=0;i<sizeof(key);i++)((uint8_t*)&key)[i]=(uint8_t)random_value();
 for(unsigned trial=0;trial<100000;trial++){
  if(trial%5)((uint8_t*)&key)[random_value()%sizeof(key)]=(uint8_t)random_value();
  unsigned mode=trial%4;force_collision=mode>=2;expected_hash=hash_value((const uint8_t*)&key,sizeof(key));
  bool hit=valid&&previous_hash==expected_hash&&!memcmp(&previous,&key,sizeof(key));
  ready(mode,&key,expected_hash);
#ifdef CHECKED
  hash_calls=0;
#endif
  ShaderModuleInfo *old=get_and_ref_shader_module_for_key(a,&key);unsigned remaining=vk_spec_ready_n;unsigned saved_order[3];memcpy(saved_order,order_storage,sizeof(saved_order));
#ifdef CHECKED
  assert(hash_calls==(hit?1u:2u));hash_calls=0;
#endif
  ready(mode,&key,expected_hash);ShaderModuleInfo *now=get_and_ref_shader_module_for_key(b,&key);
#ifdef CHECKED
  assert(hash_calls==1);
#endif
  assert(old==&a->info&&now==&b->info&&old->refs==2&&now->refs==2&&old->source==now->source);
  assert(a->creates==b->creates&&a->fallbacks==b->fallbacks&&vk_spec_ready_n==remaining&&!memcmp(saved_order,order_storage,sizeof(saved_order)));
  assert(a->entry.node.hash==expected_hash&&b->entry.node.hash==expected_hash&&!memcmp(&a->entry.key,&b->entry.key,sizeof(key)));
  if(!hit)assert(now->source==((mode==1||mode==3)?1u:99u));
  pgraph_vk_unref_shader_module(a,old);pgraph_vk_unref_shader_module(b,now);
  memcpy(&previous,&key,sizeof(key));previous_hash=expected_hash;valid=true;
 }
 lru_evict_node(&a->shader_module_cache,&a->entry.node);lru_evict_node(&b->shader_module_cache,&b->entry.node);assert(!a->info.refs&&!b->info.refs);free(a);free(b);
 puts("100000 real-LRU states: hash before init, hit/miss/eviction, forced collisions, ready order, adoption/fallback and refs match; counted build verifies two-to-one hashes on misses and one on hits");
}
#ifndef CHECKED
static volatile unsigned sink;
static PGRAPHVkState *bench_state;
static double bench(bool current,bool hit){
 if(!bench_state)bench_state=setup(current);
 PGRAPHVkState *r=bench_state;
 r->shader_module_cache.init_node=current?shader_module_cache_entry_init:reference_init;
 ShaderModuleCacheKey key;memset(&key,0x37,sizeof(key));force_collision=false;expected_hash=hash_value((const uint8_t*)&key,sizeof(key));ready(0,&key,expected_hash);
 ShaderModuleInfo *m=get_and_ref_shader_module_for_key(r,&key);pgraph_vk_unref_shader_module(r,m);
 struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<1000000;i++){
  if(!hit)lru_evict_node(&r->shader_module_cache,&r->entry.node);
  m=get_and_ref_shader_module_for_key(r,&key);pgraph_vk_unref_shader_module(r,m);
 }
 timespec_get(&b,TIME_UTC);sink=r->creates+r->info.refs;lru_evict_node(&r->shader_module_cache,&r->entry.node);
 return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/1000000;
}
#endif
int main(void){
#if defined(__x86_64__) || defined(__i386__)
 __builtin_cpu_init();if(__builtin_cpu_supports("sse2"))fixture_simd|=RETRO_SIMD_SSE2;if(__builtin_cpu_supports("avx2"))fixture_simd|=RETRO_SIMD_AVX2;
#endif
 correctness();printf("key=%zu bytes; real LRU and XXH3; fixture SIMD mask=%llu\n",sizeof(ShaderModuleCacheKey),(unsigned long long)fixture_simd);
#ifndef CHECKED
 puts("Both variants share one cache object and one noinline lookup function; hit controls do not enter either initializer.");
 puts("Modeled GPU module creation/ready storage; actual lookup/init/scan/hash; ns/lookup");
 for(unsigned round=0;round<5;round++)for(unsigned hit=0;hit<2;hit++){
  double t[2];for(unsigned k=0;k<2;k++){unsigned w=k^(round&1);t[w]=bench(w,hit);}
  printf("round=%u hit=%u baseline=%.3f current=%.3f ns/lookup\n",round,hit,t[0],t[1]);
 }
 free(bench_state);
#endif
}
