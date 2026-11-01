/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <limits.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "qemu/fast-hash.h"
static bool boost,collision;
static bool pgraph_gpu_boost_gl(void) {return boost;}
static uint64_t fixture_hash(const uint8_t *p,size_t n) {return collision?7:fast_hash(p,n);}
#define fast_hash fixture_hash
typedef unsigned GLenum;
#define GL_GEOMETRY_SHADER 0x8DD9
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
static size_t allocation_bytes;
static void *poison(size_t bytes) {
 allocation_bytes=bytes;if(!bytes)return NULL;
 void *p=malloc(bytes);assert(p);memset(p,0xa5,bytes);return p;
}
#undef g_new
#define g_new(type,n) ((type *)poison((size_t)(n)*sizeof(type)))
/* PRODUCTION */
static void check_keys(ShaderState *states,unsigned count) {
 ShaderModuleCacheKey *expected=calloc(3*count+1,sizeof(*expected));assert(expected);
 unsigned used=0;GHashTable *seen=pgraph_seed_hash_set_new();
 for(unsigned i=0;i<count;i++) {
  ShaderModuleCacheKey keys[3];bool geom=state_module_keys(&states[i],keys);
  for(unsigned k=geom?0:1;k<3;k++)if(pgraph_seed_hash_set_add(seen,fast_hash((const uint8_t *)&keys[k],sizeof(keys[k]))))expected[used++]=keys[k];
 }
 g_hash_table_destroy(seen);
 unsigned n=UINT_MAX;ShaderModuleCacheKey *actual=build_keys(states,count,&n);
 assert(allocation_bytes==(size_t)3*count*sizeof(*actual) && n==used);
 if(n)assert(!memcmp(actual,expected,n*sizeof(*actual)));
 for(size_t i=(size_t)n*sizeof(*actual);i<allocation_bytes;i++)assert(((unsigned char *)actual)[i]==0xa5);
 free(actual);free(expected);
}
static unsigned reference(const ShaderState *src,ShaderState *out,unsigned count) {
 unsigned used=0;GHashTable *seen=pgraph_seed_hash_set_new();
 for(unsigned i=0;i<count;i++) {
  ShaderState state=src[i];
  if(pgraph_glsl_psh_canonicalize_dynamic(&state.psh) && pgraph_seed_hash_set_add(seen,fast_hash((const uint8_t *)&state,sizeof(state))))out[used++]=state;
 }
 g_hash_table_destroy(seen);return used;
}
int main(void) {
 unsigned sizes[]={0,1,2,7,32,129,512,4096},cases=0;size_t states=0;
 for(unsigned b=0;b<2;b++)for(unsigned col=0;col<2;col++)for(unsigned mode=0;mode<4;mode++)for(unsigned c=0;c<sizeof(sizes)/sizeof(sizes[0]);c++) {
  unsigned count=sizes[c];boost=b;collision=col;
  ShaderState *input=calloc(count+2,sizeof(*input)),*expected=malloc((count+1)*sizeof(*input)),*actual=malloc((count+2)*sizeof(*input));assert(input&&expected&&actual);
  for(unsigned i=0;i<count;i++) {
   ShaderState *s=&input[i+1];
   s->vsh.programmable.program_data[0][0]=mode==1?0:mode==2?i%7:i;
   s->geom.primitive_mode=i%2?PRIM_TYPE_POINTS:PRIM_TYPE_TRIANGLES;
   s->geom.polygon_front_mode=s->geom.polygon_back_mode=POLY_MODE_FILL;
   s->psh.combiner_control=i*17;s->psh.final_inputs_0=i;
   s->psh.rgb_inputs[i%8]=i;s->psh.alpha_test=i%2;
   s->psh.combiner_dynamic=mode==0 || (mode==2 && i%3==0);
  }
  memset(input,0x5a,sizeof(*input));memset(input+count+1,0x6b,sizeof(*input));
  memcpy(actual,input,(count+2)*sizeof(*input));memset(expected,0xa5,(count+1)*sizeof(*input));
  unsigned n=reference(input+1,expected,count),got=seed_compact_dynamic_states(actual+1,count);assert(n==got);
  assert(!memcmp(actual+1,expected,n*sizeof(*input)));
  assert(!memcmp(input,actual,sizeof(*input)) && !memcmp(input+count+1,actual+count+1,sizeof(*input)));
  check_keys(actual+1,n);
  /* Repeated filtering rejects already-canonical states; no stale tail is read. */
  assert(seed_compact_dynamic_states(actual+1,n)==0);
  cases++;states+=count;free(input);free(expected);free(actual);
 }
 assert(seed_compact_dynamic_states(NULL,0)==0);
 printf("Seed compaction: %u model cases, %zu input states, real key layout/hash/canonicalizer, duplicates, forced hash collisions, modes, canaries and poisoned module-key output PASS (state bytes=%zu; key bytes=%zu)\n",cases,states,sizeof(ShaderState),sizeof(ShaderModuleCacheKey));
}
