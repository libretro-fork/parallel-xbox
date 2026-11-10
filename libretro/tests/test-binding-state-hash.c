/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include <libretro.h>
#include <encodings/xxh3.h>
#include "qemu/lru.h"
#include "qemu/fast-hash.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define QEMU_ALIGNED(n) __attribute__((aligned(n)))
#define unlikely(x) __builtin_expect(!!(x),0)
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END(...) ((void)0)
#define NV2A_VK_DPRINTF(...) ((void)0)
#define VK_SPEC_ADOPTED 2
#define VK_SPEC_PIPE_LANDED 4
#define NV2A_PROF_SHADER_FALLBACK 1
#define NV2A_PROF_SHADER_BIND_NOTDIRTY 2
typedef struct ShaderModuleInfo ShaderModuleInfo;
/* TYPES */
typedef struct {Lru shader_cache;ShaderBinding entry,initial,*shader_binding;bool shader_bindings_changed,ahead_valid,sibling_retry;ShaderState ahead_state;uint64_t ahead_hash;} PGRAPHVkState;
struct PGRAPHState {PGRAPHVkState *vk_renderer_state;};
/* These historical hash/copy schedules have no outstanding worker job.
 * test-pending-key-lend exercises the real retry/lend path separately. Fail
 * if a schedule accidentally enters it instead of silently modeling a miss. */
static bool vk_try_pending_binding(PGRAPHState *pg)
{
    (void)pg;
    abort();
}
typedef struct {unsigned generated,uniforms,fallback,notdirty,queues,releases,adds;uint64_t queued_hash,released[2],added;ShaderState queued;} Log;
static Log log_data;
static ShaderState requested;
static bool dirty,canonical,seen_request,seen_sibling;
static unsigned request_phase,sibling_phase;
static uint64_t request_hash,sibling_hash,fixture_simd;
uint64_t cpu_features_get(void){return fixture_simd;}
#ifdef CHECKED
static unsigned hash_calls;
#endif
#ifdef CHECKED
#define CHECK_HASH(x) assert(x)
#else
#define CHECK_HASH(x) ((void)0)
#endif
static uint64_t counted_hash(const uint8_t *p,size_t n){
#ifdef CHECKED
 hash_calls++;
#endif
 return fast_hash(p,n);
}
static void profile(unsigned n){if(n==NV2A_PROF_SHADER_FALLBACK)log_data.fallback++;else{assert(n==NV2A_PROF_SHADER_BIND_NOTDIRTY);log_data.notdirty++;}}
#define nv2a_profile_inc_counter(n) profile(n)
__attribute__((noinline)) bool pgraph_glsl_check_shader_state_dirty(PGRAPHState *pg,const ShaderState *s){(void)pg;(void)s;return dirty;}
__attribute__((noinline)) ShaderState pgraph_glsl_get_shader_state(PGRAPHState *pg){(void)pg;log_data.generated++;return requested;}
__attribute__((noinline)) bool pgraph_glsl_psh_canonicalize_dynamic(PshState *s){if(!canonical)return false;s->alpha_dynamic=true;return true;}

static bool vk_seen_contains(uint64_t h){assert(h==request_hash||h==sibling_hash);return h==request_hash?seen_request:seen_sibling;}
static int vk_spec_phase(uint64_t h){assert(h==request_hash||h==sibling_hash);return h==request_hash?request_phase:sibling_phase;}
static void vk_spec_release_adopted(uint64_t h){assert(log_data.releases<2);log_data.released[log_data.releases++]=h;}
static void vk_spec_queue(uint64_t h,const ShaderState *s){assert(!log_data.queues);log_data.queues++;log_data.queued_hash=h;memcpy(&log_data.queued,s,sizeof(*s));CHECK_HASH(h==fast_hash((const uint8_t*)s,sizeof(*s)));}
static void vk_spec_queue_view(uint64_t h,const ShaderStateView *view){
 assert(!log_data.queues);log_data.queues++;log_data.queued_hash=h;
 memcpy(&log_data.queued,view->head,VK_SHADER_HEAD_BYTES);
 memcpy((uint8_t*)&log_data.queued+VK_SHADER_HEAD_BYTES,&view->tail,sizeof(view->tail));
 CHECK_HASH(h==fast_hash((const uint8_t*)&log_data.queued,sizeof(log_data.queued)));
}
static void vk_seen_add(uint64_t h){log_data.adds++;log_data.added=h;}
static void update_shader_uniforms(PGRAPHState *pg){assert(pg->vk_renderer_state->shader_binding);log_data.uniforms++;}
static void init_entry(Lru *lru,LruNode *node,const void *key){CHECK_HASH(node->hash==fast_hash(key,sizeof(ShaderState)));ShaderBinding *b=container_of(node,ShaderBinding,node);memcpy(&b->state,key,sizeof(b->state));}
static bool compare_entry(Lru *lru,LruNode *node,const void *key){return memcmp(&container_of(node,ShaderBinding,node)->state,key,sizeof(ShaderState));}
static void shader_cache_entry_init_modules(Lru *lru,LruNode *node){
 (void)lru;ShaderBinding *binding=container_of(node,ShaderBinding,node);(void)binding;
 CHECK_HASH(node->hash==fast_hash((const uint8_t*)&binding->state,sizeof(binding->state)));
}
static uint64_t counted_pair(const void *a,size_t na,const void *b,size_t nb,uint64_t seed){
#ifdef CHECKED
 hash_calls++;
#endif
 return xxh3_64_pair(a,na,b,nb,seed);
}
#define xxh3_64_pair counted_pair
#define fast_hash counted_hash
/* FUNCTIONS */
#undef fast_hash
#undef xxh3_64_pair
static PGRAPHVkState *setup(void){PGRAPHVkState *r=calloc(1,sizeof(*r));assert(r);lru_init(&r->shader_cache);r->shader_cache.init_node=init_entry;r->shader_cache.compare_nodes=compare_entry;lru_add_free(&r->shader_cache,&r->entry.node);return r;}
static void prepare(PGRAPHVkState *r,unsigned initial){
 memcpy(&r->initial.state,&requested,sizeof(requested));if(initial==2)((uint8_t*)&r->initial.state)[0]^=0x40;
 r->shader_binding=initial?&r->initial:NULL;r->shader_bindings_changed=true;r->ahead_valid=false;r->ahead_hash=0;memset(&r->ahead_state,0x97,sizeof(r->ahead_state));
}
static uint32_t seed=0x789abc23;
static uint32_t random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void hashes(void){ShaderState sibling;memcpy(&sibling,&requested,sizeof(sibling));sibling.psh.alpha_dynamic=true;request_hash=fast_hash((const uint8_t*)&requested,sizeof(requested));sibling_hash=fast_hash((const uint8_t*)&sibling,sizeof(sibling));}
static void view_callbacks(void){
 PGRAPHVkState *r=setup();ShaderState source,expected;
 for(unsigned trial=0;trial<10000;trial++){
  for(size_t i=0;i<sizeof(source);i++)((uint8_t*)&source)[i]=(uint8_t)random_value();
  memcpy(&expected,&source,sizeof(expected));ShaderStateView view;view.head=&source;
  memcpy(&view.tail,(const uint8_t*)&source+VK_SHADER_HEAD_BYTES,sizeof(view.tail));
  r->entry.node.hash=fast_hash((const uint8_t*)&source,sizeof(source));
  shader_cache_entry_init_view(&r->shader_cache,&r->entry.node,&view);
  assert(!shader_cache_entry_compare_view(&r->shader_cache,&r->entry.node,&view));
  ((uint8_t*)&source)[trial%VK_SHADER_HEAD_BYTES]^=1;
  assert(shader_cache_entry_compare_view(&r->shader_cache,&r->entry.node,&view));
  ((uint8_t*)&source)[trial%VK_SHADER_HEAD_BYTES]^=1;
  ((uint8_t*)&view.tail)[trial%sizeof(view.tail)]^=1;
  assert(shader_cache_entry_compare_view(&r->shader_cache,&r->entry.node,&view));
  memset(&source,0xdb,sizeof(source));memset(&view.tail,0xdc,sizeof(view.tail));
  assert(!memcmp(&r->entry.state,&expected,sizeof(expected)));
 }
 free(r);puts("10000 actual view callbacks: complete owned bytes, prefix/tail collision rejection and expired borrowed scratch pass");
}
static void correctness(void){
 PGRAPHVkState *a=setup(),*b=setup();PGRAPHState pa={a},pb={b};
 for(size_t i=0;i<sizeof(requested);i++)((uint8_t*)&requested)[i]=(uint8_t)random_value();
 for(unsigned trial=0;trial<100000;trial++){
  ((uint8_t*)&requested)[random_value()%sizeof(requested)]=(uint8_t)random_value();requested.psh.alpha_dynamic=(trial&1)!=0;
  dirty=(trial&2)!=0;canonical=(trial&4)!=0;seen_request=(trial&8)!=0;seen_sibling=(trial&16)!=0;request_phase=(trial/32)%5;sibling_phase=(trial/160)%5;unsigned initial=(trial/800)%3;hashes();
  prepare(a,initial);prepare(b,initial);memset(&log_data,0,sizeof(log_data));
#ifdef CHECKED
  hash_calls=0;
#endif
  reference_bind(&pa);Log expected;memcpy(&expected,&log_data,sizeof(expected));
#ifdef CHECKED
  unsigned old_hashes=hash_calls;hash_calls=0;
#endif
  memset(&log_data,0,sizeof(log_data));pgraph_vk_bind_shaders(&pb);
  assert(!memcmp(&log_data,&expected,sizeof(expected))&&a->shader_bindings_changed==b->shader_bindings_changed&&a->ahead_valid==b->ahead_valid&&a->ahead_hash==b->ahead_hash);
  assert(!memcmp(&a->ahead_state,&b->ahead_state,sizeof(a->ahead_state))&&!memcmp(&a->shader_binding->state,&b->shader_binding->state,sizeof(ShaderState)));
  if(b->shader_bindings_changed)assert(b->shader_binding->node.hash==fast_hash((const uint8_t*)&b->shader_binding->state,sizeof(ShaderState))&&log_data.added==b->shader_binding->node.hash);
#ifdef CHECKED
#ifdef REFERENCE_REUSES_HASH
  assert(old_hashes==hash_calls);
#else
  assert(old_hashes-hash_calls==(unsigned)b->shader_bindings_changed);
#endif
#endif
 }
 free(a);free(b);puts("100000 binding schedules: exact selected/ahead/queued states, LRU digests, phase/release/seen decisions, profiles and uniform calls match; counted build validates the baseline-specific hash delta");
}
#ifndef CHECKED
static volatile uint64_t sink;
static double bench(bool current,unsigned mode){
 static PGRAPHVkState *shared_bench;
 if(!shared_bench)shared_bench=setup();
 PGRAPHVkState *r=shared_bench;
 lru_init(&r->shader_cache);r->shader_cache.init_node=init_entry;r->shader_cache.compare_nodes=compare_entry;
 lru_add_free(&r->shader_cache,&r->entry.node);
 PGRAPHState pg={r};memset(&requested,0,sizeof(requested));requested.psh.alpha_dynamic=false;
 dirty=true;canonical=mode!=6;seen_request=mode==0;seen_sibling=mode!=5;
 request_phase=mode==3||mode==8?VK_SPEC_ADOPTED:mode==4?1:mode==7?VK_SPEC_PIPE_LANDED:0;
 sibling_phase=mode==5?0:VK_SPEC_ADOPTED;requested.psh.alpha_dynamic=mode==8;hashes();
 void(*run)(PGRAPHState*)=current?pgraph_vk_bind_shaders:reference_bind;struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<200000;i++){prepare(r,mode==2?1:0);memset(&log_data,0,sizeof(log_data));run(&pg);}
 timespec_get(&b,TIME_UTC);sink=log_data.added+r->shader_bindings_changed;
 return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/200000;
}
#endif
int main(void){
#if defined(_WIN32)
 if(getenv("BINDING_BENCH_PIN")){
  DWORD_PTR process_mask,system_mask;assert(GetProcessAffinityMask(GetCurrentProcess(),&process_mask,&system_mask));
  DWORD_PTR cpu=process_mask&(~process_mask+1);
  const char *chosen_cpu=getenv("BINDING_BENCH_CPU");
  if(chosen_cpu){unsigned long index=strtoul(chosen_cpu,NULL,10);assert(index<8*sizeof(DWORD_PTR));cpu=(DWORD_PTR)1<<index;assert(cpu&process_mask);}
  assert(SetThreadAffinityMask(GetCurrentThread(),cpu));
  printf("Pinned fixture thread to CPU mask %llu\n",(unsigned long long)cpu);
 }
#endif
#if defined(__x86_64__) || defined(__i386__)
 __builtin_cpu_init();if(__builtin_cpu_supports("sse2"))fixture_simd|=RETRO_SIMD_SSE2;if(__builtin_cpu_supports("avx2"))fixture_simd|=RETRO_SIMD_AVX2;
#endif
#if defined(__x86_64__) || defined(__i386__)
 const char *backend=getenv("BINDING_BENCH_BACKEND");
 if(backend){
  fixture_simd=0;
  if(!strcmp(backend,"sse2")){if(!__builtin_cpu_supports("sse2"))return 77;fixture_simd=RETRO_SIMD_SSE2;}
  else if(!strcmp(backend,"avx2")){if(!__builtin_cpu_supports("avx2"))return 77;fixture_simd=RETRO_SIMD_SSE2|RETRO_SIMD_AVX2;}
  else if(!strcmp(backend,"avx512")){if(!__builtin_cpu_supports("avx512f")||!__builtin_cpu_supports("avx512bw"))return 77;fixture_simd=RETRO_SIMD_SSE2|RETRO_SIMD_AVX2|RETRO_SIMD_AVX512;}
  else assert(!strcmp(backend,"scalar"));
 }
#endif
 view_callbacks();correctness();printf("ShaderState=%zu bytes; real LRU/XXH3; fixture SIMD mask=%llu\n",sizeof(ShaderState),(unsigned long long)fixture_simd);
#ifndef CHECKED
 puts("Modeled services; modes 0 resident, 1 sibling/queue original, 2 equal, 3 sibling/ahead, 4 sibling/pending, 5 queue sibling, 6 rejected, 7 landed, 8 canonical-neutral; ns/call");
 for(unsigned round=0;round<5;round++)for(unsigned mode=0;mode<9;mode++){
  double t[2];for(unsigned k=0;k<2;k++){unsigned w=k^(round&1);t[w]=bench(w,mode);}
  printf("round=%u mode=%u baseline=%.3f current=%.3f ns/call\n",round,mode,t[0],t[1]);
 }
#endif
}
