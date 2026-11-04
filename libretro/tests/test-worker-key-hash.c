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
#include "qemu/fast-hash.h"
#include <libretro.h>
static uint64_t fixture_simd;
uint64_t cpu_features_get(void){return fixture_simd;}
/* KEY */
typedef struct {unsigned refs;} ShaderSource;
/* RECORD */
static ShaderSource sources[3];
static ShaderModuleCacheKey prepared[3];
static VkSpecDone emitted[3];
static uint64_t lookup_hashes[3];
static unsigned emitted_n,lookup_n,module_count,queue_limit;
static bool vk_spec_push_constants;
static int vk_spec_out;
#ifdef CHECKED
static size_t hashed_bytes,hash_calls,zeroed_bytes;
#endif
static uint64_t counted_hash(const uint8_t *p,size_t n){
#ifdef CHECKED
 hashed_bytes+=n;hash_calls++;
#endif
 return fast_hash(p,n);
}
static int vk_state_module_keys(const ShaderState *s,bool push,ShaderModuleCacheKey *keys){(void)s;(void)push;memcpy(keys,prepared,module_count*sizeof(*keys));return module_count;}
static ShaderSource *vk_spec_compile_module(const ShaderModuleCacheKey *key,uint64_t hash){
#ifdef CHECKED
 assert(hash==fast_hash((const uint8_t*)key,sizeof(*key)));
#else
 (void)key;
#endif
 assert(lookup_n<3);lookup_hashes[lookup_n]=hash;sources[lookup_n].refs++;return &sources[lookup_n++];
}
#ifndef REFERENCE_REUSES_HASH
static ShaderSource *reference_module(const ShaderModuleCacheKey *key){return vk_spec_compile_module(key,counted_hash((const uint8_t*)key,sizeof(*key)));}
#endif
static void pgraph_vk_shader_source_unref(ShaderSource *s){assert(s->refs>1);s->refs--;}
static size_t retro_spsc_write(void *ring,const void *data,size_t size){assert(ring==&vk_spec_out&&size==sizeof(VkSpecDone));if(emitted_n==queue_limit)return 0;memcpy(&emitted[emitted_n++],data,size);return size;}
static void *counted_memset(void *p,int value,size_t n){
#ifdef CHECKED
 zeroed_bytes+=n;
#endif
 return memset(p,value,n);
}
#define memset counted_memset
#define fast_hash counted_hash
/* FUNCTIONS */
#undef fast_hash
#undef memset
static void reset(void){emitted_n=lookup_n=0;memset(emitted,0,sizeof(emitted));memset(lookup_hashes,0,sizeof(lookup_hashes));for(unsigned i=0;i<3;i++)sources[i].refs=1;
#ifdef CHECKED
 hash_calls=hashed_bytes=zeroed_bytes=0;
#endif
}
static uint32_t seed=0x79abc632;
static uint32_t random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void correctness(void){
 ShaderState state;for(size_t i=0;i<sizeof(state);i++)((uint8_t*)&state)[i]=(uint8_t)random_value();
 for(size_t i=0;i<sizeof(prepared);i++)((uint8_t*)prepared)[i]=(uint8_t)random_value();
 for(unsigned trial=0;trial<100000;trial++){
  ((uint8_t*)&state)[random_value()%sizeof(state)]=(uint8_t)random_value();
  ((uint8_t*)prepared)[random_value()%sizeof(prepared)]=(uint8_t)random_value();
  module_count=2+(trial&1);queue_limit=(trial>>1)%4;vk_spec_push_constants=(trial&8)!=0;
  reset();reference_state(&state);VkSpecDone expected[3];memcpy(expected,emitted,sizeof(expected));uint64_t expected_hashes[3];memcpy(expected_hashes,lookup_hashes,sizeof(expected_hashes));unsigned refs[3];for(unsigned i=0;i<3;i++)refs[i]=sources[i].refs;unsigned count=emitted_n;
#ifdef CHECKED
  assert(zeroed_bytes==module_count*sizeof(VkSpecDone));
#ifdef REFERENCE_REUSES_HASH
  assert(hash_calls==1+module_count&&hashed_bytes==sizeof(state)+module_count*sizeof(ShaderModuleCacheKey));
#else
  assert(hash_calls==1+2*module_count&&hashed_bytes==sizeof(state)+2*module_count*sizeof(ShaderModuleCacheKey));
#endif
#endif
  reset();vk_spec_compile_state(&state);
  assert(count==emitted_n&&lookup_n==module_count&&!memcmp(expected,emitted,sizeof(expected))&&!memcmp(expected_hashes,lookup_hashes,sizeof(expected_hashes)));
  for(unsigned i=0;i<3;i++)assert(refs[i]==sources[i].refs&&sources[i].refs==1+(unsigned)(i<emitted_n));
  for(unsigned i=0;i<emitted_n;i++){
   assert(emitted[i].key_hash==fast_hash((const uint8_t*)&prepared[i],sizeof(prepared[i])));
   assert(emitted[i].state_hash==(i==module_count-1?fast_hash((const uint8_t*)&state,sizeof(state)):0));
  }
#ifdef CHECKED
  assert(hash_calls==1+module_count&&hashed_bytes==sizeof(state)+module_count*sizeof(ShaderModuleCacheKey));
  assert(zeroed_bytes==module_count*(sizeof(VkSpecDone)-sizeof(ShaderModuleCacheKey)));
#endif
 }
 puts("100000 states: complete record bytes, hashes, drops, markers and references match; counted build checks key hashes and skipped key clearing");
}
#ifndef CHECKED
static volatile uint64_t sink;
static double bench(unsigned which,unsigned count){
 ShaderState state;memset(&state,0,sizeof(state));module_count=count;queue_limit=3;void(*run)(const ShaderState*)=which?vk_spec_compile_state:reference_state;
 struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<200000;i++){reset();state.vsh.programmable.program_data[0][0]=i;run(&state);}
 timespec_get(&b,TIME_UTC);sink=emitted[module_count-1].key_hash;
 return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/200000;
}
#endif
int main(void){
#if defined(__x86_64__) || defined(__i386__)
 __builtin_cpu_init();
 if(__builtin_cpu_supports("sse2"))fixture_simd|=RETRO_SIMD_SSE2;
 if(__builtin_cpu_supports("avx2"))fixture_simd|=RETRO_SIMD_AVX2;
#endif
 printf("Fixture CPU feature mask=%llu (GCC runtime SSE2/AVX2 detection; scalar elsewhere)\n",(unsigned long long)fixture_simd);
 correctness();printf("key=%zu state=%zu bytes; real util/fast-hash.c and bundled XXH3\n",sizeof(ShaderModuleCacheKey),sizeof(ShaderState));
#ifndef CHECKED
 puts("Modeled module lookup/queue, real hash; no GLSL/SPIR-V/GPU timing; ns/state");
 for(unsigned round=0;round<5;round++)for(unsigned count=2;count<=3;count++){
  double t[2];for(unsigned k=0;k<2;k++){unsigned w=k^(round&1);t[w]=bench(w,count);}
  printf("round=%u modules=%u baseline=%.3f current=%.3f ns/state\n",round,count,t[0],t[1]);
 }
#endif
}
