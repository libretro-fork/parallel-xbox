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
static retro_atomic_int_t live_sources, source_allocations;
static void *tracked_source_alloc(size_t n) { void *p=calloc(1,n); assert(p); retro_atomic_fetch_add_int(&live_sources,1); retro_atomic_fetch_add_int(&source_allocations,1); return p; }
#define g_malloc0 tracked_source_alloc
static void g_free(void *p) { if(p) { free(p); retro_atomic_fetch_sub_int(&live_sources,1); } }
/* SOURCE FUNCTIONS */
/* MODULE TYPES */
typedef struct { unsigned device;uint64_t next_shader_module_identity; } PGRAPHVkState;
typedef struct {
 int refcnt; uint64_t identity;unsigned module; ShaderLayouts *layouts;
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
static bool pgraph_vk_spirv_1_6,pgraph_vk_preserve_float32,vk_spec_push_constants,force_collision;
static unsigned compiles;
static unsigned key_hash_calls;
static uint64_t fast_hash(const uint8_t *p,size_t n){if(n==sizeof(ShaderModuleCacheKey))key_hash_calls++;uint64_t h=17;if(force_collision)return 7;while(n--)h=h*33+*p++;return h;}
static MString *vk_module_glsl(const ShaderModuleCacheKey *key){MString *m=malloc(sizeof(*m));assert(m);*m=*key;return m;}
static const char *mstring_get_str(MString *m){return (const char *)m;}
static void mstring_unref(MString *m){free(m);}
static unsigned payload_byte(const ShaderModuleCacheKey *key){return (key->id*13+key->kind+(pgraph_vk_spirv_1_6?111:0)+(pgraph_vk_preserve_float32?53:0))&255;}
static ShaderSource *pgraph_vk_compile_shader(unsigned kind,const char *str)
{
 const ShaderModuleCacheKey *key=(const void *)str;assert(kind==key->kind);compiles++;
 uint32_t *data=malloc((key->len+3)/4*4);assert(data);memset(data,payload_byte(key),(key->len+3)/4*4);
 GlslcResult result={.words=data,.num_words=(key->len+3)/4,.allocation_words=(key->len+3)/4};
 result.uniforms.count=1;result.uniforms.size=16;
 result.uniforms.members=calloc(1,sizeof(GlslcUniform)+6);assert(result.uniforms.members);
 char *name=(char *)(result.uniforms.members+1);memcpy(name,"value",6);
 result.uniforms.members[0]=(GlslcUniform){.name=name,.dim_v=4,.dim_a=1};
 return pgraph_vk_shader_source_take(&result);
}
static ShaderSource *vk_module_spirv(const ShaderModuleCacheKey *key)
{MString *code=vk_module_glsl(key);ShaderSource *spv=pgraph_vk_compile_shader(key->kind,mstring_get_str(code));mstring_unref(code);return spv;}
static int vk_state_module_keys(const ShaderState *st,bool push,ShaderModuleCacheKey *keys)
{keys[0]=(ShaderModuleCacheKey){.kind=1,.id=1,.len=64,.opts={push}};keys[1]=(ShaderModuleCacheKey){.kind=2,.id=2,.len=128};keys[2]=(ShaderModuleCacheKey){.kind=3,.id=st->id+100,.len=256};return 3;}
/* RECORD */
static int vk_spec_out;
static VkSpecDone emitted[3];
static unsigned emitted_n,queue_limit=3,reservations;
static size_t retro_spsc_write(void *ring,const void *data,size_t n){assert(ring==&vk_spec_out&&n==sizeof(VkSpecDone));if(emitted_n==queue_limit)return 0;emitted[emitted_n++]=*(const VkSpecDone *)data;return n;}
/* Model both direct reservation and the unchanged copy/drop fallback. */
static bool retro_spsc_write_reserve(void *ring,void **out,size_t n){
 assert(ring==&vk_spec_out&&n==sizeof(VkSpecDone));*out=NULL;
 if(!vk_spec_push_constants||emitted_n==queue_limit)return false;
 *out=&emitted[emitted_n];reservations++;return true;
}
static void retro_spsc_write_end(void *ring,size_t n){assert(ring==&vk_spec_out&&n==sizeof(VkSpecDone)&&emitted_n<queue_limit);emitted_n++;}
#define VK_SPEC_READY 768
/* READY STATE */
/* PRODUCTION */
static ShaderSource *take_ready_for_test(const ShaderModuleCacheKey *key)
{ return vk_spec_take_ready(key,fast_hash((const uint8_t *)key,sizeof(*key))); }

static ShaderSource *compile_for_test(const ShaderModuleCacheKey *key)
{ return vk_spec_compile_module(key,fast_hash((const uint8_t *)key,sizeof(*key))); }

static uint32_t seed=19;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void *consumer(void *arg){ShaderSource *a=arg;for(unsigned i=0;i<200000;i++){ShaderSource *b=pgraph_vk_shader_source_ref(a);assert(b==a&&b->num_words==16);assert(!strcmp(b->layouts->uniforms.members[0].name,"value"));pgraph_vk_shader_source_unref(b);}return NULL;}
typedef struct {ShaderSource *source;ShaderLayouts *layouts;pthread_mutex_t lock;pthread_cond_t cond;unsigned arrived;} LastOwners;
typedef struct {LastOwners *owners;bool source;} LastRelease;
static void *release_last_owner(void *arg){
 LastRelease *release=arg;LastOwners *o=release->owners;
 assert(!pthread_mutex_lock(&o->lock));o->arrived++;
 if(o->arrived==2)assert(!pthread_cond_broadcast(&o->cond));
 while(o->arrived!=2)assert(!pthread_cond_wait(&o->cond,&o->lock));
 assert(!pthread_mutex_unlock(&o->lock));
 if(release->source)pgraph_vk_shader_source_unref(o->source);else pgraph_vk_shader_layouts_unref(o->layouts);
 return NULL;
}
int main(void)
{
 retro_atomic_int_init(&live_sources,0);retro_atomic_int_init(&source_allocations,0);
 /* Direct and compiler results move the exact owners, including prefix views.
  * Metadata stays alive independently after the last source-word consumer. */
 for(unsigned i=0;i<1024;i++) {
  GlslcResult result={0};unsigned prefix=i&1?8:0;
  uint32_t *base=malloc((16+prefix)*4);assert(base);
  result.words=base+prefix;result.num_words=16;result.allocation_words=16+prefix;
  result.allocation=prefix?base:NULL;
  GlslcLayout *blocks[]={&result.uniforms,&result.push_constants};
  for(unsigned b=0;b<2;b++)if(i&(2u<<b)) {
   blocks[b]->count=1;blocks[b]->size=16;
   blocks[b]->members=calloc(1,sizeof(GlslcUniform)+6);assert(blocks[b]->members);
   char *name=(char *)(blocks[b]->members+1);memcpy(name,"value",6);
   blocks[b]->members[0]=(GlslcUniform){.name=name,.dim_v=4,.dim_a=1};
  }
  GlslcUniform *u=result.uniforms.members,*pc=result.push_constants.members;
  int before=retro_atomic_load_relaxed_int(&source_allocations);
  ShaderSource *taken=pgraph_vk_shader_source_take(&result);
  assert(retro_atomic_load_relaxed_int(&source_allocations)==before+1);
  assert(taken->layouts_own_source==!!(u||pc));
  if(u||pc)assert(taken==(ShaderSource *)(taken->layouts+1));
  assert(taken->words==base+prefix && taken->words_allocation==(prefix?base:NULL));
  assert(!result.words&&!result.allocation&&!result.uniforms.members&&!result.push_constants.members);
  size_t bytes=sizeof(*taken)+(16+prefix)*4;
  if(u||pc){
   assert(taken->layouts->uniforms.members==u && taken->layouts->push_constants.members==pc);
   bytes+=sizeof(*taken->layouts)+(!!u+!!pc)*(sizeof(GlslcUniform)+6);
   retro_atomic_fetch_add_int(&taken->layouts->refs,1);
   ShaderLayouts *retained=taken->layouts;
   assert(taken->len==bytes);pgraph_vk_shader_source_unref(taken);
   assert(!taken->words&&!taken->words_allocation&&!taken->num_words);
   if(u)assert(!strcmp(retained->uniforms.members[0].name,"value"));
   if(pc)assert(!strcmp(retained->push_constants.members[0].name,"value"));
   pgraph_vk_shader_layouts_unref(retained);
  }else{assert(taken->len==bytes);pgraph_vk_shader_source_unref(taken);}
  assert(!retro_atomic_load_relaxed_int(&live_sources));
 }

 /* Legacy standalone source/layout owners still free independently. */
 ShaderSource *separate=pgraph_vk_shader_source_new(malloc(4),1);
 separate->layouts=tracked_source_alloc(sizeof(*separate->layouts));retro_atomic_int_init(&separate->layouts->refs,1);
 assert(!separate->layouts_own_source);pgraph_vk_shader_source_unref(separate);assert(!retro_atomic_load_relaxed_int(&live_sources));
 printf("Source ownership: 1024 direct/compiler/prefix/empty moves use one header allocation each; retained source header=%zu bytes; no retained word pointers\n",sizeof(ShaderSource));
 /* Interior module views retain and release the allocation base. */
 uint32_t *view_base=malloc(20*sizeof(*view_base));assert(view_base);
 ShaderSource *view=pgraph_vk_shader_source_new(view_base+4,16);
 view->words_allocation=view_base;
 assert(pgraph_vk_shader_source_ref(view)==view);
 pgraph_vk_shader_source_unref(view);view->words[0]=123;
 assert(view_base[4]==123);pgraph_vk_shader_source_unref(view);
 assert(!retro_atomic_load_relaxed_int(&live_sources));

 for(unsigned i=0;i<VK_SPEC_READY;i++)vk_spec_ready_order[i]=i;
 ShaderSource *empty=pgraph_vk_shader_source_new(NULL,0);assert(!empty->words&&!empty->num_words);pgraph_vk_shader_source_unref(empty);
 ShaderModuleCacheKey k={.kind=1,.id=42,.len=64};
 ShaderSource *a=compile_for_test(&k),*b=compile_for_test(&k);assert(a==b&&compiles==1);
 vk_spec_modules_clear();assert(!vk_spec_module_bytes&&a->words==b->words);pgraph_vk_shader_source_unref(a);assert(((unsigned char *)b->words)[0]==payload_byte(&k));pgraph_vk_shader_source_unref(b);assert(!retro_atomic_load_relaxed_int(&live_sources));
 force_collision=true;a=compile_for_test(&k);k.id++;b=compile_for_test(&k);assert(a!=b);assert(((unsigned char *)a->words)[0]!=payload_byte(&k));pgraph_vk_shader_source_unref(a);pgraph_vk_shader_source_unref(b);vk_spec_modules_clear();force_collision=false;
 a=compile_for_test(&k);pgraph_vk_spirv_1_6=true;b=compile_for_test(&k);assert(a!=b&&((unsigned char *)a->words)[0]!=((unsigned char *)b->words)[0]);pgraph_vk_shader_source_unref(a);pgraph_vk_shader_source_unref(b);vk_spec_modules_clear();
 a=compile_for_test(&k);pgraph_vk_preserve_float32=true;b=compile_for_test(&k);assert(a!=b&&((unsigned char *)a->words)[0]!=((unsigned char *)b->words)[0]);pgraph_vk_shader_source_unref(a);pgraph_vk_shader_source_unref(b);vk_spec_modules_clear();
 k.len=VK_SPEC_MODULE_BYTES+1;a=compile_for_test(&k);b=compile_for_test(&k);assert(a!=b&&!vk_spec_module_bytes);pgraph_vk_shader_source_unref(a);pgraph_vk_shader_source_unref(b);
 /* Metadata alone can take an otherwise fitting payload over the cap. */
 k.len=VK_SPEC_MODULE_BYTES-sizeof(ShaderSource);a=compile_for_test(&k);b=compile_for_test(&k);assert(a!=b&&a->len>VK_SPEC_MODULE_BYTES&&!vk_spec_module_bytes);pgraph_vk_shader_source_unref(a);pgraph_vk_shader_source_unref(b);
 /* Every state's end marker must still be emitted on module cache hits. */
 for(unsigned trial=0;trial<100;trial++){
  vk_spec_push_constants=(trial&1)!=0;
  ShaderState st={trial};emitted_n=0;unsigned before_hashes=key_hash_calls;vk_spec_compile_state(&st);assert(emitted_n==3&&key_hash_calls-before_hashes==3);
  uint64_t h=fast_hash((const uint8_t *)&st,sizeof(st));assert(!emitted[0].state_hash&&!emitted[1].state_hash&&emitted[2].state_hash==h);
  for(unsigned i=0;i<3;i++)vk_spec_ready_push(&emitted[i]);
  ShaderSource *same=take_ready_for_test(&emitted[0].key);assert(same==emitted[0].spirv&&vk_spec_ready_n==2);pgraph_vk_shader_source_unref(same);
  vk_spec_ready_clear();assert(vk_spec_ready_order==vk_spec_ready_order_storage);
 }
 vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_sources));
 /* Insertion/eviction order agrees with an independent FIFO oracle.
  * Adopting a record never moves any other record's payload. */
 ShaderModuleCacheKey fifo[VK_SPEC_READY];unsigned nfifo=0;
 for(unsigned n=0;n<10000;n++){
  ShaderModuleCacheKey key={.kind=3,.id=n+100000,.len=16};
  VkSpecDone d={.key=key,.key_hash=fast_hash((const uint8_t *)&key,sizeof(key)),.spirv=compile_for_test(&key)};
  if(nfifo==VK_SPEC_READY){memmove(fifo,fifo+1,(nfifo-1)*sizeof(fifo[0]));nfifo--;}
  fifo[nfifo++]=key;vk_spec_ready_push(&d);assert(vk_spec_ready_n==nfifo);
  if((rnd()&3)==0){
   unsigned take=rnd()%nfifo;VkSpecDone *still=&vk_spec_ready[vk_spec_ready_order[nfifo-1]];ShaderModuleCacheKey saved_key=still->key;ShaderSource *saved_spirv=still->spirv;
   ShaderSource *got=take_ready_for_test(&fifo[take]);assert(got);pgraph_vk_shader_source_unref(got);
   if(take!=nfifo-1)assert(!memcmp(&still->key,&saved_key,sizeof(saved_key))&&still->spirv==saved_spirv);
   memmove(fifo+take,fifo+take+1,(nfifo-take-1)*sizeof(fifo[0]));nfifo--;
  }
  bool seen[VK_SPEC_READY]={0};for(unsigned i=0;i<VK_SPEC_READY;i++){assert(vk_spec_ready_order[i]<VK_SPEC_READY&&!seen[vk_spec_ready_order[i]]);seen[vk_spec_ready_order[i]]=true;if(i<nfifo)assert(!memcmp(&vk_spec_ready[vk_spec_ready_order[i]].key,&fifo[i],sizeof(fifo[i])));}
 }
 vk_spec_ready_clear();assert(vk_spec_ready_order==vk_spec_ready_order_storage);vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_sources));
 /* Dropped queue records release their own references, not the memo's. */
 queue_limit=0;emitted_n=0;ShaderState st={1000};vk_spec_compile_state(&st);assert(!emitted_n);vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_sources));queue_limit=3;
 /* Consumer references survive concurrent ref/unref, eviction and clearing. */
 k=(ShaderModuleCacheKey){.kind=1,.id=42,.len=64};ShaderSource *shared=compile_for_test(&k);pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,consumer,shared));
 for(unsigned n=0;n<5000;n++){
  k.id=rnd();k.len=1+rnd()%700000;k.opts[0]=rnd();a=compile_for_test(&k);assert(a->num_words==(k.len+3)/4&&((unsigned char *)a->words)[0]==payload_byte(&k));assert(vk_spec_module_bytes<=VK_SPEC_MODULE_BYTES);pgraph_vk_shader_source_unref(a);if(n%97==0)vk_spec_modules_clear();
 }
 for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(shared->num_words==16);pgraph_vk_shader_source_unref(shared);vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_sources));
 /* Module descriptors borrow the same immutable layout allocation. Words
  * can be released first; upload buffers remain independently writable. */
 k=(ShaderModuleCacheKey){.kind=1,.id=42,.len=64};a=compile_for_test(&k);
 expected_words=a->words;PGRAPHVkState renderer={0};
 ShaderModuleInfo *one=pgraph_vk_create_shader_module_from_spirv(&renderer,pgraph_vk_shader_source_ref(a));
 ShaderModuleInfo *two=pgraph_vk_create_shader_module_from_spirv(&renderer,a);
 assert(one->identity==1 && two->identity==2);
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
 /* Module layouts may retire while a memo still owns the words. */
 a=compile_for_test(&k);expected_words=a->words;
 one=pgraph_vk_create_shader_module_from_spirv(&renderer,pgraph_vk_shader_source_ref(a));
 pgraph_vk_destroy_shader_module(&renderer,one);assert(a->words==expected_words&&!strcmp(a->layouts->uniforms.members[0].name,"value"));
 pgraph_vk_shader_source_unref(a);vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_sources));
 /* Race the final word owner against the last independent layout consumer. */
 for(unsigned i=0;i<512;i++){
  GlslcResult result={.words=malloc(64),.num_words=16,.allocation_words=16};assert(result.words);
  result.uniforms.count=1;result.uniforms.size=16;result.uniforms.members=calloc(1,sizeof(GlslcUniform)+6);assert(result.uniforms.members);
  char *name=(char *)(result.uniforms.members+1);memcpy(name,"value",6);result.uniforms.members[0]=(GlslcUniform){.name=name,.dim_v=4,.dim_a=1};
  LastOwners owners={.source=pgraph_vk_shader_source_take(&result)};owners.layouts=owners.source->layouts;retro_atomic_fetch_add_int(&owners.layouts->refs,1);
  assert(!pthread_mutex_init(&owners.lock,NULL));assert(!pthread_cond_init(&owners.cond,NULL));
  LastRelease release[2]={{&owners,!!(i&1)},{&owners,!(i&1)}};pthread_t last[2];
  for(unsigned j=0;j<2;j++)assert(!pthread_create(last+j,NULL,release_last_owner,release+j));
  for(unsigned j=0;j<2;j++)assert(!pthread_join(last[j],NULL));
  assert(!pthread_cond_destroy(&owners.cond));assert(!pthread_mutex_destroy(&owners.lock));assert(!retro_atomic_load_relaxed_int(&live_sources));
 }
 puts("Source retirement: module-first release and 512 synchronized final source/layout races PASS");
 assert(reservations>0);
 puts("PASS: immutable module sharing, exact keys/collisions, SPIR-V versions, byte cap, oversize bypass, eviction, queue drops, state markers, adoption and 800000 concurrent ref/unref pairs; no leaked sources/layouts; shared layouts outlive words and module destruction");
}
