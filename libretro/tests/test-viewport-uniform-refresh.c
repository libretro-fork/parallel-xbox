/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
enum { UNISEC_LIVE, UNISEC_VSHC, UNISEC_LTCTX, UNISEC_LIGHT, UNISEC_PP, UNISEC_FOGP, UNISEC__COUNT };
enum { VshUniform_c, VshUniform_ltctxa, VshUniform_ltctxb, VshUniform_ltc1,
 VshUniform_lightInfiniteDirection, VshUniform_lightInfiniteHalfVector,
 VshUniform_lightLocalAttenuation, VshUniform_lightLocalPosition,
 VshUniform_pointParams, VshUniform_fogParam, VshUniform__COUNT };
#define NV097_SET_VIEWPORT_OFFSET 0
#define NV097_SET_VIEWPORT_SCALE 0
#define NV_IGRAPH_XF_XFCTX_VPOFF 59
#define NV_IGRAPH_XF_XFCTX_VPSCL 58
#define QEMU_BUILD_BUG_MSG(c,m) _Static_assert(!(c),m)
typedef int VshUniformLocs[VshUniform__COUNT];
typedef struct { uint32_t c[192][4]; } VshUniformValues;
typedef struct { bool is_fixed_function; } VshState;
typedef struct {
 uint32_t vsh_constants[192][4]; bool vsh_constants_dirty[192];
 bool point_params_dirty,fog_params_dirty,ltctxa_dirty[4],ltctxb_dirty[4],ltc1_dirty[4],light_dirty;
} PGRAPHState;
typedef struct { VshUniformValues values; unsigned gen[UNISEC__COUNT]; bool valid,ff_valid; } VshUniformSource;
typedef struct { bool valid,sec_valid; unsigned sec_gen[UNISEC__COUNT]; VshUniformValues v; } UniformCacheEntry;
typedef struct { UniformCacheEntry e[4]; } UniformCache;
typedef struct { size_t val_offs,size,count; } UniformInfo;
typedef void (*UniformUploadFn)(void*,const UniformInfo*,int,const void*);
static unsigned long long fills;
static void pgraph_glsl_set_vsh_uniform_values(PGRAPHState *pg,const VshState *state,const VshUniformLocs locs,VshUniformValues *values)
{
 (void)state;
 if(locs[VshUniform_c]!=-1) fills++;
 /* COPY */
}
/* MAP */
/* METHODS */
/* FUNCTIONS */
typedef struct {
 PGRAPHState pg; VshUniformSource src; UniformCache cache;
 VshUniformValues gpu[4]; unsigned active; unsigned long long uploads;
} Fixture;
static void upload(void *opaque,const UniformInfo *info,int loc,const void *value)
{
 Fixture *f=opaque;assert(loc==0);
 memcpy(&f->gpu[f->active],value,info->size*info->count);f->uploads++;
}
static void draw(Fixture *f,unsigned i)
{
 VshState state={.is_fixed_function=(i&1)!=0};VshUniformLocs locs;
 for(unsigned j=0;j<VshUniform__COUNT;j++) locs[j]=-1;
 locs[VshUniform_c]=0;UniformInfo info={0,16,192};f->active=i%4;
 pgraph_vsh_uniform_source_refresh(&f->pg,&f->src,&state,locs);
 uniform_cache_apply(&f->cache,f->active,f->src.gen,&info,locs,&f->src.values,1,pgraph_vsh_uniform_section,upload,f);
}
static void write_viewport(Fixture *f,bool candidate,unsigned i,unsigned pattern)
{
 for(unsigned slot=0;slot<4;slot++) {
  uint32_t v=pattern==0?0:pattern==1?(i&1):i*2654435761u+slot;
  (candidate?candidate_OFFSET:baseline_OFFSET)(&f->pg,slot*4,v);
  (candidate?candidate_SCALE:baseline_SCALE)(&f->pg,slot*4,~v);
 }
}
static void correctness(void)
{
 Fixture *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));assert(a&&b);
 for(unsigned i=0;i<100000;i++) {
  if(i%113==0) {
   a->src.valid=b->src.valid=false;
   memset(&a->cache,0,sizeof(a->cache));memset(&b->cache,0,sizeof(b->cache));
  }
  if(i%7==0) {
   a->pg.vsh_constants[0][0]=b->pg.vsh_constants[0][0]=i;
   a->pg.vsh_constants_dirty[0]=b->pg.vsh_constants_dirty[0]=true;
  }
  write_viewport(a,false,i,(i/97)%3);write_viewport(b,true,i,(i/97)%3);
  if(i%3==0) continue;
  draw(a,i);draw(b,i);
  assert(!memcmp(a->gpu,b->gpu,sizeof(a->gpu)));
  assert(a->uploads==b->uploads);
  assert(!memcmp(&a->src.values,&b->src.values,sizeof(a->src.values)));
 }
 puts("100000 state steps: identical bound bytes/upload counts; four programs, resets, pending changes, batched writes");
 free(a);free(b);
}
#ifndef CHECKED
static volatile unsigned long long sink;
static double bench(bool candidate,unsigned pattern,unsigned batch)
{
 Fixture *f=calloc(1,sizeof(*f));assert(f);struct timespec start,end;fills=0;
 timespec_get(&start,TIME_UTC);
 for(unsigned i=0;i<500000;i++) {
  write_viewport(f,candidate,i,pattern);
  if(i%batch==batch-1) draw(f,i/batch);
 }
 timespec_get(&end,TIME_UTC);sink=f->uploads+fills+f->gpu[f->active].c[58][0];
 printf(" fills=%llu uploads=%llu",fills,f->uploads);free(f);
 return ((end.tv_sec-start.tv_sec)*1e9+end.tv_nsec-start.tv_nsec)/500000;
}
#endif
int main(void)
{
 correctness();
#ifndef CHECKED
 puts("Only constants bound; production refresh/cache; ns per eight writes including amortized draw; no GPU/game timing.");
 for(unsigned round=0;round<5;round++) for(unsigned batch=1;batch<=16;batch*=16)
 for(unsigned pattern=0;pattern<3;pattern++) {
  double t[2];printf("round=%u batch=%u pattern=%u",round,batch,pattern);
  for(unsigned j=0;j<2;j++) {
   unsigned which=j^(round&1);printf(" %s",which?"candidate":"baseline");
   t[which]=bench(which,pattern,batch);
  }
  printf(" baseline=%.3f candidate=%.3f ns/step\n",t[0],t[1]);
 }
#endif
}
