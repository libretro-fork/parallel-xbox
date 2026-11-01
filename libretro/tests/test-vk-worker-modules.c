/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <retro_atomic.h>
#define TRUE true
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
/* SOURCE TYPES */
static retro_atomic_int_t live_sources;
static void *tracked_source_alloc(size_t n) { void *p=calloc(1,n); assert(p); retro_atomic_fetch_add_int(&live_sources,1); return p; }
#define g_malloc0 tracked_source_alloc
static void g_free(void *p) { if(p) { free(p); retro_atomic_fetch_sub_int(&live_sources,1); } }
/* SOURCE FUNCTIONS */
/* MODULE TYPES */
typedef struct { unsigned device; } PGRAPHVkState;
typedef struct {
 int refcnt; unsigned module; ShaderLayouts *layouts;
 ShaderUniformLayout uniforms,push_constants;
} ShaderModuleInfo;
static const uint32_t *expected_words;
static unsigned pgraph_vk_create_shader_module_from_words(PGRAPHVkState *r,const uint32_t *words,size_t count)
{ (void)r; assert(words==expected_words && count==16); return 7; }
static void vkDestroyShaderModule(unsigned device,unsigned module,void *allocator)
{ (void)device; assert(module==7 && !allocator); }
/* MODULE FUNCTIONS */
typedef struct {unsigned kind,id,len,opts[4];} ShaderModuleCacheKey;
typedef struct {unsigned id;} ShaderState;
typedef ShaderModuleCacheKey MString;
static bool pgraph_vk_spirv_1_6,vk_spec_push_constants,force_collision;
static unsigned compiles;
static uint64_t fast_hash(const uint8_t *p,size_t n){uint64_t h=17;if(force_collision)return 7;while(n--)h=h*33+*p++;return h;}
static MString *vk_module_glsl(const ShaderModuleCacheKey *key){MString *m=malloc(sizeof(*m));assert(m);*m=*key;return m;}
static const char *mstring_get_str(MString *m){return (const char *)m;}
static void mstring_unref(MString *m){free(m);}
static unsigned payload_byte(const ShaderModuleCacheKey *key){return (key->id*13+key->kind+(pgraph_vk_spirv_1_6?111:0))&255;}
static ShaderSource *pgraph_vk_compile_shader(unsigned kind,const char *str)
{
 const ShaderModuleCacheKey *key=(const void *)str;assert(kind==key->kind);compiles++;
 uint32_t *data=malloc((key->len+3)/4*4);assert(data);memset(data,payload_byte(key),(key->len+3)/4*4);
 ShaderSource *source=pgraph_vk_shader_source_new(data,(key->len+3)/4);
 source->layouts=tracked_source_alloc(sizeof(*source->layouts));source->layouts->refs=1;
 source->layouts->uniforms.count=1;source->layouts->uniforms.size=16;
 source->layouts->uniforms.members=calloc(1,sizeof(GlslcUniform)+6);assert(source->layouts->uniforms.members);
 char *name=(char *)(source->layouts->uniforms.members+1);memcpy(name,"value",6);
 source->layouts->uniforms.members[0]=(GlslcUniform){.name=name,.dim_v=4,.dim_a=1};
 source->len+=sizeof(*source->layouts)+sizeof(GlslcUniform)+6;
 return source;
}
static ShaderSource *vk_module_spirv(const ShaderModuleCacheKey *key)
{MString *code=vk_module_glsl(key);ShaderSource *spv=pgraph_vk_compile_shader(key->kind,mstring_get_str(code));mstring_unref(code);return spv;}
static int vk_state_module_keys(const ShaderState *st,bool push,ShaderModuleCacheKey *keys)
{keys[0]=(ShaderModuleCacheKey){.kind=1,.id=1,.len=64,.opts={push}};keys[1]=(ShaderModuleCacheKey){.kind=2,.id=2,.len=128};keys[2]=(ShaderModuleCacheKey){.kind=3,.id=st->id+100,.len=256};return 3;}
/* RECORD */
static int vk_spec_out;
static VkSpecDone emitted[3];
static unsigned emitted_n,queue_limit=3;
static size_t retro_spsc_write(void *ring,const void *data,size_t n){assert(ring==&vk_spec_out&&n==sizeof(VkSpecDone));if(emitted_n==queue_limit)return 0;emitted[emitted_n++]=*(const VkSpecDone *)data;return n;}
#define VK_SPEC_READY 768
/* READY STATE */
/* PRODUCTION */
static uint32_t seed=19;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void *consumer(void *arg){ShaderSource *a=arg;for(unsigned i=0;i<200000;i++){ShaderSource *b=pgraph_vk_shader_source_ref(a);assert(b==a&&b->num_words==16);assert(!strcmp(b->layouts->uniforms.members[0].name,"value"));pgraph_vk_shader_source_unref(b);}return NULL;}
int main(void)
{
 retro_atomic_int_init(&live_sources,0);
 for(unsigned i=0;i<VK_SPEC_READY;i++)vk_spec_ready_order[i]=i;
 ShaderSource *empty=pgraph_vk_shader_source_new(NULL,0);assert(!empty->words&&!empty->num_words);pgraph_vk_shader_source_unref(empty);
 ShaderModuleCacheKey k={.kind=1,.id=42,.len=64};
 ShaderSource *a=vk_spec_compile_module(&k),*b=vk_spec_compile_module(&k);assert(a==b&&compiles==1);
 vk_spec_modules_clear();assert(!vk_spec_module_bytes&&a->words==b->words);pgraph_vk_shader_source_unref(a);assert(((unsigned char *)b->words)[0]==payload_byte(&k));pgraph_vk_shader_source_unref(b);assert(!retro_atomic_load_relaxed_int(&live_sources));
 force_collision=true;a=vk_spec_compile_module(&k);k.id++;b=vk_spec_compile_module(&k);assert(a!=b);assert(((unsigned char *)a->words)[0]!=payload_byte(&k));pgraph_vk_shader_source_unref(a);pgraph_vk_shader_source_unref(b);vk_spec_modules_clear();force_collision=false;
 a=vk_spec_compile_module(&k);pgraph_vk_spirv_1_6=true;b=vk_spec_compile_module(&k);assert(a!=b&&((unsigned char *)a->words)[0]!=((unsigned char *)b->words)[0]);pgraph_vk_shader_source_unref(a);pgraph_vk_shader_source_unref(b);vk_spec_modules_clear();
 k.len=VK_SPEC_MODULE_BYTES+1;a=vk_spec_compile_module(&k);b=vk_spec_compile_module(&k);assert(a!=b&&!vk_spec_module_bytes);pgraph_vk_shader_source_unref(a);pgraph_vk_shader_source_unref(b);
 /* Metadata alone can take an otherwise fitting payload over the cap. */
 k.len=VK_SPEC_MODULE_BYTES-sizeof(ShaderSource);a=vk_spec_compile_module(&k);b=vk_spec_compile_module(&k);assert(a!=b&&a->len>VK_SPEC_MODULE_BYTES&&!vk_spec_module_bytes);pgraph_vk_shader_source_unref(a);pgraph_vk_shader_source_unref(b);
 /* Every state's end marker must still be emitted on module cache hits. */
 for(unsigned trial=0;trial<100;trial++){
  ShaderState st={trial};emitted_n=0;vk_spec_compile_state(&st);assert(emitted_n==3);
  uint64_t h=fast_hash((const uint8_t *)&st,sizeof(st));assert(!emitted[0].state_hash&&!emitted[1].state_hash&&emitted[2].state_hash==h);
  for(unsigned i=0;i<3;i++)vk_spec_ready_push(&emitted[i]);
  ShaderSource *same=vk_spec_take_ready(&emitted[0].key);assert(same==emitted[0].spirv&&vk_spec_ready_n==2);pgraph_vk_shader_source_unref(same);
  vk_spec_ready_clear();assert(vk_spec_ready_order==vk_spec_ready_order_storage);
 }
 vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_sources));
 /* Insertion/eviction order agrees with an independent FIFO oracle.
  * Adopting a record never moves any other record's payload. */
 ShaderModuleCacheKey fifo[VK_SPEC_READY];unsigned nfifo=0;
 for(unsigned n=0;n<10000;n++){
  ShaderModuleCacheKey key={.kind=3,.id=n+100000,.len=16};
  VkSpecDone d={.key=key,.key_hash=fast_hash((const uint8_t *)&key,sizeof(key)),.spirv=vk_spec_compile_module(&key)};
  if(nfifo==VK_SPEC_READY){memmove(fifo,fifo+1,(nfifo-1)*sizeof(fifo[0]));nfifo--;}
  fifo[nfifo++]=key;vk_spec_ready_push(&d);assert(vk_spec_ready_n==nfifo);
  if((rnd()&3)==0){
   unsigned take=rnd()%nfifo;VkSpecDone *still=&vk_spec_ready[vk_spec_ready_order[nfifo-1]];ShaderModuleCacheKey saved_key=still->key;ShaderSource *saved_spirv=still->spirv;
   ShaderSource *got=vk_spec_take_ready(&fifo[take]);assert(got);pgraph_vk_shader_source_unref(got);
   if(take!=nfifo-1)assert(!memcmp(&still->key,&saved_key,sizeof(saved_key))&&still->spirv==saved_spirv);
   memmove(fifo+take,fifo+take+1,(nfifo-take-1)*sizeof(fifo[0]));nfifo--;
  }
  bool seen[VK_SPEC_READY]={0};for(unsigned i=0;i<VK_SPEC_READY;i++){assert(vk_spec_ready_order[i]<VK_SPEC_READY&&!seen[vk_spec_ready_order[i]]);seen[vk_spec_ready_order[i]]=true;if(i<nfifo)assert(!memcmp(&vk_spec_ready[vk_spec_ready_order[i]].key,&fifo[i],sizeof(fifo[i])));}
 }
 vk_spec_ready_clear();assert(vk_spec_ready_order==vk_spec_ready_order_storage);vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_sources));
 /* Dropped queue records release their own references, not the memo's. */
 queue_limit=0;emitted_n=0;ShaderState st={1000};vk_spec_compile_state(&st);assert(!emitted_n);vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_sources));queue_limit=3;
 /* Consumer references survive concurrent ref/unref, eviction and clearing. */
 k=(ShaderModuleCacheKey){.kind=1,.id=42,.len=64};ShaderSource *shared=vk_spec_compile_module(&k);pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,consumer,shared));
 for(unsigned n=0;n<5000;n++){
  k.id=rnd();k.len=1+rnd()%700000;k.opts[0]=rnd();a=vk_spec_compile_module(&k);assert(a->num_words==(k.len+3)/4&&((unsigned char *)a->words)[0]==payload_byte(&k));assert(vk_spec_module_bytes<=VK_SPEC_MODULE_BYTES);pgraph_vk_shader_source_unref(a);if(n%97==0)vk_spec_modules_clear();
 }
 for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(shared->num_words==16);pgraph_vk_shader_source_unref(shared);vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_sources));
 /* Module descriptors borrow the same immutable layout allocation. Words
  * can be released first; upload buffers remain independently writable. */
 k=(ShaderModuleCacheKey){.kind=1,.id=42,.len=64};a=vk_spec_compile_module(&k);
 expected_words=a->words;PGRAPHVkState renderer={0};
 ShaderModuleInfo *one=pgraph_vk_create_shader_module_from_spirv(&renderer,pgraph_vk_shader_source_ref(a));
 ShaderModuleInfo *two=pgraph_vk_create_shader_module_from_spirv(&renderer,a);
 assert(one->uniforms.uniforms==two->uniforms.uniforms && one->layouts==two->layouts);
 vk_spec_modules_clear(); /* destroys words/source but keeps shared layouts */
 /* One shared immutable layout and one allocation per mutable module. */
 assert(retro_atomic_load_relaxed_int(&live_sources)==3);
 assert(!strcmp(one->uniforms.uniforms[0].name,"value"));
 memset(one->uniforms.allocation,0xa5,16);
 for(unsigned i=0;i<16;i++)assert(!((unsigned char*)two->uniforms.allocation)[i]);
 pgraph_vk_destroy_shader_module(&renderer,one);
 assert(!strcmp(two->uniforms.uniforms[0].name,"value"));
 pgraph_vk_destroy_shader_module(&renderer,two);
 assert(!retro_atomic_load_relaxed_int(&live_sources));
 puts("PASS: immutable module sharing, exact keys/collisions, SPIR-V versions, byte cap, oversize bypass, eviction, queue drops, state markers, adoption and 800000 concurrent ref/unref pairs; no leaked sources/layouts; shared layouts outlive words and module destruction");
}
