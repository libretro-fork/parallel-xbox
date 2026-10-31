/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <retro_atomic.h>
#define TRUE true
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct {char *data;unsigned len;} GArray;
typedef void (*GDestroyNotify)(void *);
typedef struct {GArray a;retro_atomic_int_t ref_count;unsigned elt_size,alloc;GDestroyNotify clear_func;} GRealArray;
typedef char gchar;typedef unsigned guint;typedef size_t gsize;typedef uint8_t guint8;
typedef GArray GByteArray;
/* Blocks the arrays own: their structs and the buffers they take */
static retro_atomic_int_t live_arrays;
static void *new_array(size_t n){assert(n==1);void *r=calloc(1,sizeof(GRealArray));assert(r);retro_atomic_fetch_add_int(&live_arrays,1);return r;}
#define g_new0(type,n) ((type *)new_array(n))
GByteArray *g_byte_array_new_take(guint8 *,gsize);
static void g_free(void *p){if(p){free(p);retro_atomic_fetch_sub_int(&live_arrays,1);}}
GArray *g_array_ref(GArray *);
void g_array_unref(GArray *);
static GByteArray *g_byte_array_ref(GByteArray *a){return g_array_ref(a);}
static void g_byte_array_unref(GByteArray *a){g_array_unref(a);}
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
static GByteArray *pgraph_vk_compile_glsl_to_spv(unsigned kind,const char *str)
{
 const ShaderModuleCacheKey *key=(const void *)str;assert(kind==key->kind);compiles++;
 guint8 *data=malloc(key->len);assert(data);retro_atomic_fetch_add_int(&live_arrays,1);memset(data,payload_byte(key),key->len);return g_byte_array_new_take(data,key->len);
}
static GByteArray *vk_module_spirv(const ShaderModuleCacheKey *key)
{MString *code=vk_module_glsl(key);GByteArray *spv=pgraph_vk_compile_glsl_to_spv(key->kind,mstring_get_str(code));mstring_unref(code);return spv;}
static int vk_state_module_keys(const ShaderState *st,bool push,ShaderModuleCacheKey *keys)
{keys[0]=(ShaderModuleCacheKey){.kind=1,.id=1,.len=64,.opts={push}};keys[1]=(ShaderModuleCacheKey){.kind=2,.id=2,.len=128};keys[2]=(ShaderModuleCacheKey){.kind=3,.id=st->id+100,.len=256};return 3;}
/* RECORD */
static int vk_spec_out;
static VkSpecDone emitted[3];
static unsigned emitted_n,queue_limit=3;
static size_t retro_spsc_write(void *ring,const void *data,size_t n){assert(ring==&vk_spec_out&&n==sizeof(VkSpecDone));if(emitted_n==queue_limit)return 0;emitted[emitted_n++]=*(const VkSpecDone *)data;return n;}
#define VK_SPEC_READY 768
static VkSpecDone vk_spec_ready[VK_SPEC_READY];
static unsigned vk_spec_ready_n;
static unsigned vk_spec_ready_order[VK_SPEC_READY];
/* PRODUCTION */
static uint32_t seed=19;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void *consumer(void *arg){GByteArray *a=arg;for(unsigned i=0;i<200000;i++){GByteArray *b=g_byte_array_ref(a);assert(b==a&&b->len==64);g_byte_array_unref(b);}return NULL;}
int main(void)
{
 retro_atomic_int_init(&live_arrays,0);
 for(unsigned i=0;i<VK_SPEC_READY;i++)vk_spec_ready_order[i]=i;
 GByteArray *empty=g_byte_array_new_take(NULL,0);assert(!empty->data&&!empty->len);g_byte_array_unref(empty);
 ShaderModuleCacheKey k={.kind=1,.id=42,.len=64};
 GByteArray *a=vk_spec_compile_module(&k),*b=vk_spec_compile_module(&k);assert(a==b&&compiles==1);
 vk_spec_modules_clear();assert(!vk_spec_module_bytes&&a->data==b->data);g_byte_array_unref(a);assert((unsigned char)b->data[0]==payload_byte(&k));g_byte_array_unref(b);assert(!retro_atomic_load_relaxed_int(&live_arrays));
 force_collision=true;a=vk_spec_compile_module(&k);k.id++;b=vk_spec_compile_module(&k);assert(a!=b);assert((unsigned char)a->data[0]!=payload_byte(&k));g_byte_array_unref(a);g_byte_array_unref(b);vk_spec_modules_clear();force_collision=false;
 a=vk_spec_compile_module(&k);pgraph_vk_spirv_1_6=true;b=vk_spec_compile_module(&k);assert(a!=b&&(unsigned char)a->data[0]!=(unsigned char)b->data[0]);g_byte_array_unref(a);g_byte_array_unref(b);vk_spec_modules_clear();
 k.len=VK_SPEC_MODULE_BYTES+1;a=vk_spec_compile_module(&k);b=vk_spec_compile_module(&k);assert(a!=b&&!vk_spec_module_bytes);g_byte_array_unref(a);g_byte_array_unref(b);
 /* Every state's end marker must still be emitted on module cache hits. */
 for(unsigned trial=0;trial<100;trial++){
  ShaderState st={trial};emitted_n=0;vk_spec_compile_state(&st);assert(emitted_n==3);
  uint64_t h=fast_hash((const uint8_t *)&st,sizeof(st));assert(!emitted[0].state_hash&&!emitted[1].state_hash&&emitted[2].state_hash==h);
  for(unsigned i=0;i<3;i++)vk_spec_ready_push(&emitted[i]);
  GByteArray *same=vk_spec_take_ready(&emitted[0].key);assert(same==emitted[0].spirv&&vk_spec_ready_n==2);g_byte_array_unref(same);
  for(unsigned i=0;i<vk_spec_ready_n;i++)g_byte_array_unref(vk_spec_ready[vk_spec_ready_order[i]].spirv);vk_spec_ready_n=0;
 }
 vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_arrays));
 /* Insertion/eviction order agrees with an independent FIFO oracle.
  * Adopting a record never moves any other record's payload. */
 ShaderModuleCacheKey fifo[VK_SPEC_READY];unsigned nfifo=0;
 for(unsigned n=0;n<10000;n++){
  ShaderModuleCacheKey key={.kind=3,.id=n+100000,.len=16};
  VkSpecDone d={.key=key,.key_hash=fast_hash((const uint8_t *)&key,sizeof(key)),.spirv=vk_spec_compile_module(&key)};
  if(nfifo==VK_SPEC_READY){memmove(fifo,fifo+1,(nfifo-1)*sizeof(fifo[0]));nfifo--;}
  fifo[nfifo++]=key;vk_spec_ready_push(&d);assert(vk_spec_ready_n==nfifo);
  if((rnd()&3)==0){
   unsigned take=rnd()%nfifo;VkSpecDone *still=&vk_spec_ready[vk_spec_ready_order[nfifo-1]];ShaderModuleCacheKey saved_key=still->key;GByteArray *saved_spirv=still->spirv;
   GByteArray *got=vk_spec_take_ready(&fifo[take]);assert(got);g_byte_array_unref(got);
   if(take!=nfifo-1)assert(!memcmp(&still->key,&saved_key,sizeof(saved_key))&&still->spirv==saved_spirv);
   memmove(fifo+take,fifo+take+1,(nfifo-take-1)*sizeof(fifo[0]));nfifo--;
  }
  bool seen[VK_SPEC_READY]={0};for(unsigned i=0;i<VK_SPEC_READY;i++){assert(vk_spec_ready_order[i]<VK_SPEC_READY&&!seen[vk_spec_ready_order[i]]);seen[vk_spec_ready_order[i]]=true;if(i<nfifo)assert(!memcmp(&vk_spec_ready[vk_spec_ready_order[i]].key,&fifo[i],sizeof(fifo[i])));}
 }
 for(unsigned i=0;i<vk_spec_ready_n;i++)g_byte_array_unref(vk_spec_ready[vk_spec_ready_order[i]].spirv);vk_spec_ready_n=0;vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_arrays));
 /* Dropped queue records release their own references, not the memo's. */
 queue_limit=0;emitted_n=0;ShaderState st={1000};vk_spec_compile_state(&st);assert(!emitted_n);vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_arrays));queue_limit=3;
 /* Consumer references survive concurrent ref/unref, eviction and clearing. */
 k=(ShaderModuleCacheKey){.kind=1,.id=42,.len=64};GByteArray *shared=vk_spec_compile_module(&k);pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,consumer,shared));
 for(unsigned n=0;n<5000;n++){
  k.id=rnd();k.len=1+rnd()%700000;k.opts[0]=rnd();a=vk_spec_compile_module(&k);assert(a->len==k.len&&(unsigned char)a->data[0]==payload_byte(&k));assert(vk_spec_module_bytes<=VK_SPEC_MODULE_BYTES);g_byte_array_unref(a);if(n%97==0)vk_spec_modules_clear();
 }
 for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(shared->len==64);g_byte_array_unref(shared);vk_spec_modules_clear();assert(!retro_atomic_load_relaxed_int(&live_arrays));
 puts("PASS: immutable module sharing, exact keys/collisions, SPIR-V versions, byte cap, oversize bypass, eviction, queue drops, state markers, adoption and 800000 concurrent ref/unref pairs; no leaked arrays");
}
