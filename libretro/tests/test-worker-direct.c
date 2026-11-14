/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <retro_spsc.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include <encodings/xxh3.h>
#include <libretro.h>
#ifdef _WIN32
#include <windows.h>
#endif
/* Opt-in placement for benchmarks; correctness runs stay unpinned by default. */
static void pin_thread(const char *name){
 const char *value=getenv(name);if(!value)return;
#ifdef _WIN32
 char *end;unsigned long index=strtoul(value,&end,10);
 assert(value[0]&&!*end&&index<8*sizeof(DWORD_PTR));
 DWORD_PTR process_mask,system_mask,mask=(DWORD_PTR)1<<index;
 assert(GetProcessAffinityMask(GetCurrentProcess(),&process_mask,&system_mask));
 assert(process_mask&mask);
 assert(SetThreadAffinityMask(GetCurrentThread(),mask));
 assert(GetCurrentProcessorNumber()==index);
#else
 fprintf(stderr,"Explicit CPU placement currently requires Windows: %s\n",name);_Exit(1);
#endif
}
static double monotonic_ns(void){
#ifdef _WIN32
 LARGE_INTEGER counter,frequency;
 assert(QueryPerformanceFrequency(&frequency)&&QueryPerformanceCounter(&counter));
 return (double)counter.QuadPart*1e9/(double)frequency.QuadPart;
#else
 struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));
 return (double)t.tv_sec*1e9+t.tv_nsec;
#endif
}
static retro_atomic_int_t started,start_gate;
static void wait_for_start(const char *placement){
 pin_thread(placement);
 retro_atomic_fetch_add_int(&started,1);
 while(!retro_atomic_load_acquire_int(&start_gate))sched_yield();
}
static uint64_t fixture_simd;
uint64_t cpu_features_get(void){return fixture_simd;}
/* KEY */
/* SOURCE */
/* RECORD */
static ShaderSource sources[3];
static ShaderModuleCacheKey prepared[3];
static bool vk_spec_push_constants;
static retro_spsc_t vk_spec_out;
static unsigned lookup_n;
static int vk_state_module_keys(const ShaderState *s,bool push,ShaderModuleCacheKey *keys){(void)s;(void)push;memcpy(keys,prepared,sizeof(prepared));lookup_n=0;return 3;}
static ShaderSource *vk_spec_compile_module(const ShaderModuleCacheKey *key,uint64_t hash){
 unsigned i=lookup_n++;assert(i<3&&hash==xxh3_64((const uint8_t*)key,sizeof(*key), 0));
 retro_atomic_fetch_add_int(&sources[i].refs,1);return &sources[i];
}
static void pgraph_vk_shader_source_unref(ShaderSource *s){assert(retro_atomic_fetch_sub_int(&s->refs,1)>1);}
/* FUNCTIONS */
static void reset(size_t position,size_t occupied){
 retro_atomic_size_init(&vk_spec_out.head,position);
 retro_atomic_size_init(&vk_spec_out.tail,position-occupied);
 vk_spec_out.cached_head=position-occupied;vk_spec_out.cached_tail=position-occupied;
 memset(vk_spec_out.buffer,0xa5,vk_spec_out.capacity);
 for(unsigned i=0;i<3;i++)retro_atomic_int_init(&sources[i].refs,1);
}
static void correctness(void){
 ShaderState state;memset(&state,0x39,sizeof(state));
 uint8_t *expected=malloc(vk_spec_out.capacity);assert(expected);
 for(unsigned trial=0;trial<20000;trial++){
  size_t pos=trial&1?SIZE_MAX-trial*31u:trial*2347u;
  size_t room[]={vk_spec_out.capacity,0,1,sizeof(VkSpecDone)-1,sizeof(VkSpecDone),2*sizeof(VkSpecDone)};
  size_t occupied=vk_spec_out.capacity-room[trial%6];
  ((uint8_t*)&state)[trial%sizeof(state)]++;
  reset(pos,occupied);
  if(trial&2)vk_spec_out.cached_tail=pos-vk_spec_out.capacity;
  reference_state(&state);
  size_t head=retro_atomic_load_relaxed_size(&vk_spec_out.head);
  int refs[3];for(unsigned i=0;i<3;i++)refs[i]=retro_atomic_load_relaxed_int(&sources[i].refs);
  memcpy(expected,vk_spec_out.buffer,vk_spec_out.capacity);
  reset(pos,occupied);
  if(trial&2)vk_spec_out.cached_tail=pos-vk_spec_out.capacity;
  vk_spec_compile_state(&state);
  assert(head==retro_atomic_load_relaxed_size(&vk_spec_out.head));
  assert(!memcmp(expected,vk_spec_out.buffer,vk_spec_out.capacity));
  for(unsigned i=0;i<3;i++)assert(refs[i]==retro_atomic_load_relaxed_int(&sources[i].refs));
 }
 free(expected);
 puts("20000 real-queue states: full buffer bytes, partial/full drops, reference counts, unaligned/wrapped cursors and size_t rollover match frozen builder");
}
#ifdef BENCHMARK
enum {STATES=131072};
#else
enum {STATES=32768};
#endif
static unsigned mode;
static void (*builder[2])(const ShaderState *)={reference_state,vk_spec_compile_state};
static void state_for(ShaderState *s,unsigned serial){memset(s,0,sizeof(*s));memcpy(s,&serial,sizeof(serial));}
static void *produce(void *unused){
 (void)unused;ShaderState s;wait_for_start("WORKER_BENCH_PRODUCER_CPU");
 for(unsigned i=0;i<STATES;i++){
  while(retro_spsc_write_avail(&vk_spec_out)<3*sizeof(VkSpecDone))sched_yield();
  state_for(&s,i);builder[mode](&s);
 }
 return NULL;
}
static void *consume(void *unused){
 (void)unused;VkSpecDone actual,expected;ShaderState s;wait_for_start("WORKER_BENCH_CONSUMER_CPU");
 for(unsigned i=0;i<STATES;i++)for(unsigned j=0;j<3;j++){
  while(retro_spsc_read_avail(&vk_spec_out)<sizeof(actual))sched_yield();
  assert(retro_spsc_read(&vk_spec_out,&actual,sizeof(actual))==sizeof(actual));
  memset(&expected,0,sizeof(expected));memcpy(&expected.key,&prepared[j],sizeof(expected.key));
  expected.key_hash=xxh3_64((const uint8_t*)&prepared[j],sizeof(prepared[j]), 0);expected.spirv=&sources[j];
  state_for(&s,i);expected.state_hash=j==2?xxh3_64((const uint8_t*)&s,sizeof(s), 0):0;
  assert(!memcmp(&expected,&actual,sizeof(actual)));
  pgraph_vk_shader_source_unref(actual.spirv);
 }
 return NULL;
}
static double concurrent(unsigned which){
 reset(SIZE_MAX-12345,0);mode=which;pthread_t producer,consumer;
 retro_atomic_int_init(&started,0);retro_atomic_int_init(&start_gate,0);
 assert(!pthread_create(&consumer,NULL,consume,NULL));assert(!pthread_create(&producer,NULL,produce,NULL));
 while(retro_atomic_load_acquire_int(&started)!=2)sched_yield();
 double a=monotonic_ns();retro_atomic_store_release_int(&start_gate,1);
 assert(!pthread_join(producer,NULL));assert(!pthread_join(consumer,NULL));
 double b=monotonic_ns();assert(!retro_spsc_read_avail(&vk_spec_out));
 for(unsigned i=0;i<3;i++)assert(retro_atomic_load_relaxed_int(&sources[i].refs)==1);
 return (b-a)/STATES;
}
#ifdef BENCHMARK
static double sequential(unsigned which){
 reset(0,0);ShaderState s;VkSpecDone d;double a=monotonic_ns();
 for(unsigned i=0;i<STATES*4;i++){
  state_for(&s,i);builder[which](&s);
  for(unsigned j=0;j<3;j++){assert(retro_spsc_read(&vk_spec_out,&d,sizeof(d))==sizeof(d));pgraph_vk_shader_source_unref(d.spirv);}
 }
 double b=monotonic_ns();return (b-a)/(STATES*4);
}
#endif
int main(void){
 pin_thread("WORKER_BENCH_PRODUCER_CPU");
 printf("Placement: producer=%s consumer=%s; monotonic timer; gated thread start\n",
        getenv("WORKER_BENCH_PRODUCER_CPU")?getenv("WORKER_BENCH_PRODUCER_CPU"):"scheduler",
        getenv("WORKER_BENCH_CONSUMER_CPU")?getenv("WORKER_BENCH_CONSUMER_CPU"):"scheduler");
#if defined(__x86_64__) || defined(__i386__)
 __builtin_cpu_init();
 if(__builtin_cpu_supports("sse2"))fixture_simd|=RETRO_SIMD_SSE2;
 if(__builtin_cpu_supports("avx2"))fixture_simd|=RETRO_SIMD_AVX2;
#endif
 for(size_t i=0;i<sizeof(prepared);i++)((uint8_t*)prepared)[i]=(uint8_t)(i*31+17);
 assert(retro_spsc_init(&vk_spec_out,3*256*sizeof(VkSpecDone)));correctness();
#ifndef BENCHMARK
 concurrent(0);concurrent(1);
 puts("196608 concurrently transferred records: exact bytes, ordering, markers and ownership passed");
#else
 puts("Actual production/frozen builders + real SPSC/XXH3; compilation mocked; CPU ns/state, no GPU timing");
 for(unsigned round=0;round<7;round++){
  double seq[2],con[2];for(unsigned k=0;k<2;k++){unsigned w=k^(round&1);seq[w]=sequential(w);con[w]=concurrent(w);}
  printf("round=%u sequential old=%.3f current=%.3f concurrent old=%.3f current=%.3f\n",round,seq[0],seq[1],con[0],con[1]);
 }
#endif
 retro_spsc_free(&vk_spec_out);
}
