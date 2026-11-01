/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define VK_SPEC_READY 768
#define VK_SPEC_READY_HEADROOM 32
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef struct { uint64_t code, kind, attrs[8]; } ShaderModuleCacheKey;
typedef struct { unsigned id, refs; } ShaderSource;
typedef struct { uint64_t key_hash; ShaderModuleCacheKey key; ShaderSource *spirv; uint64_t state_hash; } VkSpecDone;
static VkSpecDone vk_spec_ready[VK_SPEC_READY];
static unsigned vk_spec_ready_n;
#ifdef BASELINE
static unsigned vk_spec_ready_order[VK_SPEC_READY];
#else
static unsigned vk_spec_ready_order_storage[VK_SPEC_READY+VK_SPEC_READY_HEADROOM];
static unsigned *vk_spec_ready_order=vk_spec_ready_order_storage;
#endif
static uint64_t fast_hash(const uint8_t *key, size_t n) { assert(n==sizeof(ShaderModuleCacheKey)); ShaderModuleCacheKey k;memcpy(&k,key,n);
#ifdef BENCH
 return k.code*UINT64_C(11400714819323198485);
#else
 return k.code%17;
#endif
}
static void pgraph_vk_shader_source_unref(ShaderSource *s) { assert(s && s->refs);s->refs--; }
#ifndef BENCH
static size_t moved;
static void *tracked_move(void *d,const void *s,size_t n) {
#ifndef BASELINE
 assert(n);
#endif
 moved+=n;return memmove(d,s,n);
}
#define memmove tracked_move
#endif
/* PRODUCTION */
#undef memmove
static void init(void) { memset(vk_spec_ready,0,sizeof(vk_spec_ready));vk_spec_ready_n=0;
#ifndef BASELINE
 vk_spec_ready_order=vk_spec_ready_order_storage;
#endif
 for(unsigned i=0;i<VK_SPEC_READY;i++)vk_spec_ready_order[i]=i;
}
static ShaderModuleCacheKey key(unsigned id) { ShaderModuleCacheKey k={0};k.code=id;k.kind=id%3;for(unsigned i=0;i<8;i++)k.attrs[i]=(uint64_t)id*(i+1);return k; }
#ifndef BENCH
#define RECORDS 70000
static ShaderSource sources[RECORDS];
static unsigned model[VK_SPEC_READY], model_n, made, checks, compactions;
static unsigned random_state=42;
static unsigned random_u(void) {random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static void verify(void)
{
 bool seen[VK_SPEC_READY]={0};assert(vk_spec_ready_n==model_n);
 for(unsigned i=0;i<VK_SPEC_READY;i++) {
  unsigned slot=vk_spec_ready_order[vk_spec_ready_position(i)];assert(slot<VK_SPEC_READY && !seen[slot]);seen[slot]=true;
  if(i<model_n) {assert(vk_spec_ready[slot].spirv==&sources[model[i]]);assert(sources[model[i]].refs==1);}
  else assert(!vk_spec_ready[slot].spirv);
 }
 checks++;
}
static void push(void)
{
 assert(made<RECORDS);unsigned id=made++;sources[id]=(ShaderSource){id,1};
 ShaderModuleCacheKey k=key(id%997);
 VkSpecDone d={.key_hash=fast_hash((const uint8_t *)&k,sizeof(k)),.key=k,.spirv=&sources[id],.state_hash=id+1};
 bool full=model_n==VK_SPEC_READY;size_t before=moved;unsigned evicted=full?model[0]:0;
#ifndef BASELINE
 unsigned head_before=vk_spec_ready_order-vk_spec_ready_order_storage;
#endif
 vk_spec_ready_push(&d);
 if(full) {
#ifndef BASELINE
  assert(moved-before==(head_before+1==VK_SPEC_READY_HEADROOM?VK_SPEC_READY*sizeof(unsigned):0));
  compactions+=head_before+1==VK_SPEC_READY_HEADROOM;
#else
  (void)before;
#endif
  assert(!sources[evicted].refs);memmove(model,model+1,(--model_n)*sizeof(model[0]));
 }
 model[model_n++]=id;verify();
}
static void take(unsigned code)
{
 ShaderModuleCacheKey k=key(code);unsigned found=model_n;
 for(unsigned i=0;i<model_n;i++)if(model[i]%997==code){found=i;break;}
 ShaderSource *got=vk_spec_take_ready(&k);
 if(found==model_n)assert(!got);
 else {assert(got==&sources[model[found]] && got->refs==1);pgraph_vk_shader_source_unref(got);memmove(model+found,model+found+1,(--model_n-found)*sizeof(model[0]));}
 verify();
}
static void clear(void) {vk_spec_ready_clear();for(unsigned i=0;i<model_n;i++)assert(!sources[model[i]].refs);model_n=0;verify();}
int main(void)
{
 init();for(unsigned i=0;i<3*VK_SPEC_READY;i++)push();
 for(unsigned i=0;i<50000;i++) {
  unsigned value=random_u();
  if(i && i%4096==0)clear();
  if(value%3!=0 || !model_n)push();
  else take(value%4 && model_n ? model[random_u()%model_n]%997 : 2000+value%97);
 }
 clear();for(unsigned i=0;i<made;i++)assert(!sources[i].refs);
#ifndef BASELINE
 assert(compactions>100);
#endif
 printf("Vulkan ready order: %u FIFO/slot/ownership checks, %u module lifetimes, %u compactions; hash collisions and clears PASS\n",checks,made,compactions);
}
#else
#ifdef _WIN32
#include <windows.h>
static double now(void) {LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return t.QuadPart*(1e9/f.QuadPart);}
#else
static double now(void) {struct timespec t;timespec_get(&t,TIME_UTC);return t.tv_sec*1e9+t.tv_nsec;}
#endif
static volatile uintptr_t sink;
int main(void)
{
 static ShaderSource source={1,UINT32_MAX};
 for(unsigned mode=0;mode<5;mode++) {
  init();for(unsigned i=0;i<VK_SPEC_READY;i++){ShaderModuleCacheKey k=key(i);VkSpecDone d={.key_hash=fast_hash((const uint8_t *)&k,sizeof(k)),.key=k,.spirv=&source};vk_spec_ready_push(&d);}
  if(mode>=3)for(unsigned i=0;i<VK_SPEC_READY/2+13;i++){ShaderModuleCacheKey k=key(VK_SPEC_READY+i);VkSpecDone d={.key_hash=fast_hash((const uint8_t *)&k,sizeof(k)),.key=k,.spirv=&source};vk_spec_ready_push(&d);}
  unsigned loops=mode?100000:1000000;double start=now();
  for(unsigned i=0;i<loops;i++) {
   if(mode==0) {ShaderModuleCacheKey k=key(9000);VkSpecDone d={.key_hash=fast_hash((const uint8_t *)&k,sizeof(k)),.key=k,.spirv=&source};vk_spec_ready_push(&d);}
   else if(mode==1 || mode==3){ShaderModuleCacheKey k=key(999999);sink=(uintptr_t)vk_spec_take_ready(&k);}
   else {unsigned slot=vk_spec_ready_order[vk_spec_ready_position((i*37)%vk_spec_ready_n)];ShaderModuleCacheKey k=vk_spec_ready[slot].key;ShaderSource *got=vk_spec_take_ready(&k);assert(got);VkSpecDone d={.key_hash=fast_hash((const uint8_t *)&k,sizeof(k)),.key=k,.spirv=got};vk_spec_ready_push(&d);}
  }
  printf("mode=%u ns=%.3f\n",mode,(now()-start)/loops);vk_spec_ready_clear();
 }
}
#endif
