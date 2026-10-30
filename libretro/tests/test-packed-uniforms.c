/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum UniformElementType { TEST_FLOAT };
/* TYPES */
typedef struct {uint64_t uniform_epoch;unsigned uniform_sec_gen[UNISEC__COUNT];bool uniform_valid;bool uniform_staged_valid;} ShaderModuleInfo;
typedef struct {uint64_t uniform_epoch,uniform_upload_epoch;struct{unsigned gen[UNISEC__COUNT];bool valid,ff_valid;}vsh_uni_src;const ShaderModuleInfo *uni_last_pushed[2];} PGRAPHVkState;
static uintptr_t watched_start,watched_end;
static size_t copied,copies;
static void *count_copy(void *dst,const void *src,size_t n)
{
 uintptr_t a=(uintptr_t)dst;
 if(a>=watched_start&&a<watched_end){assert(n<=watched_end-a);copied+=n;copies++;}
 return memcpy(dst,src,n);
}
#define memcpy count_copy
/* PRODUCTION */
#undef memcpy
static uint32_t seed=17;
static uint32_t random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void watch(void *p,size_t n){watched_start=(uintptr_t)p;watched_end=watched_start+n;copied=copies=0;}
int main(void)
{
 uint8_t packed[512],expected[512],source[256];
 for(unsigned i=0;i<sizeof(source);i++)source[i]=(uint8_t)i;
 memset(packed,0xcd,sizeof(packed));
 ShaderUniform fields[2]={{.dim_v=3,.dim_a=4,.offset=8,.stride=16}};
 ShaderUniformLayout layout={.uniforms=fields,.num_uniforms=1,.total_size=80,.allocation=packed};
 watch(packed,sizeof(packed));
 assert(uniform_update(&layout,1,source,48,true));assert(copied==48&&copies==4);
 copied=copies=0;assert(!uniform_update(&layout,1,source,48,false));assert(!copied);
 source[25]^=0x80;
 assert(uniform_update(&layout,1,source,48,false));assert(copied==12&&copies==1);
 for(unsigned i=0;i<4;i++){assert(!memcmp(packed+8+i*16,source+i*12,12));for(unsigned j=12;j<16;j++)assert(packed[8+i*16+j]==0xcd);}
 fields[0]=(ShaderUniform){.dim_v=4,.dim_a=16,.offset=16,.stride=16};layout.total_size=272;
 watch(packed,sizeof(packed));assert(uniform_update(&layout,1,source,256,true));assert(copied==256&&copies==1);
 /* Live and tracked sections, invalidation epochs, unused uniforms and
  * recycled module storage, without a duplicate unpacked value cache. */
 struct {uint32_t live,pad[3],constants[12];} values={.live=0x7fc01234};
 for(unsigned i=0;i<12;i++)values.constants[i]=i*71;
 UniformInfo info[2]={{.size=4,.count=1,.val_offs=0},{.size=12,.count=4,.val_offs=16}};
 int locs[2]={1,2};int8_t sections[2]={UNISEC_LIVE,UNISEC_VSHC};
 fields[0]=(ShaderUniform){.dim_v=1,.dim_a=1,.offset=0};
 fields[1]=(ShaderUniform){.dim_v=3,.dim_a=4,.offset=16,.stride=16};
 layout.num_uniforms=2;layout.total_size=80;
 PGRAPHVkState r={0};ShaderModuleInfo mod={0};
 memset(packed,0xab,sizeof(packed));watch(packed,sizeof(packed));
 assert(vk_apply_uniform_updates(&r,&layout,info,locs,&values,2,&mod,sections));assert(copied==52);
 mod.uniform_staged_valid=true;
 watch(packed,sizeof(packed));assert(!vk_apply_uniform_updates(&r,&layout,info,locs,&values,2,&mod,sections));assert(!copied&&mod.uniform_staged_valid);
 values.constants[3]++;values.live^=0x80000000;
 watch(packed,sizeof(packed));assert(vk_apply_uniform_updates(&r,&layout,info,locs,&values,2,&mod,sections));assert(copied==4&&!mod.uniform_staged_valid);
 r.vsh_uni_src.gen[UNISEC_VSHC]++;
 watch(packed,sizeof(packed));assert(vk_apply_uniform_updates(&r,&layout,info,locs,&values,2,&mod,sections));assert(copied==12);
 r.vsh_uni_src.valid=r.vsh_uni_src.ff_valid=true;r.uni_last_pushed[0]=r.uni_last_pushed[1]=&mod;
 pgraph_vk_uniform_sources_invalidate(&r);assert(r.uniform_upload_epoch==1);assert(!r.vsh_uni_src.valid&&!r.vsh_uni_src.ff_valid&&!r.uni_last_pushed[0]&&!r.uni_last_pushed[1]);
 watch(packed,sizeof(packed));assert(vk_apply_uniform_updates(&r,&layout,info,locs,&values,2,&mod,sections));assert(copied==52);
 locs[1]=-1;values.constants[0]++;r.vsh_uni_src.gen[UNISEC_VSHC]++;
 watch(packed,sizeof(packed));assert(!vk_apply_uniform_updates(&r,&layout,info,locs,&values,2,&mod,NULL));assert(!copied);
 locs[1]=2;memset(&mod,0,sizeof(mod));watch(packed,sizeof(packed));
 assert(vk_apply_uniform_updates(&r,&layout,info,locs,&values,2,&mod,sections));assert(copied==52);
 /* Independent packing oracle, preserving all padding/guard bytes and
  * comparing exact bits (including arbitrary NaN payloads). */
 for(unsigned trial=0;trial<20000;trial++){
  unsigned dim=1+random_value()%4,num=1+random_value()%16,count=1+random_value()%num;
  unsigned element=4*dim,stride=num==1?0:(random_value()&1?16:element),offset=random_value()%17;
  fields[0]=(ShaderUniform){.dim_v=dim,.dim_a=num,.offset=offset,.stride=stride};
  layout.num_uniforms=1;layout.total_size=offset+(num-1)*(stride?stride:element)+element;
  for(unsigned i=0;i<sizeof(packed);i++)packed[i]=(uint8_t)random_value();
  for(unsigned i=0;i<sizeof(source);i++)source[i]=(uint8_t)random_value();
  if(trial%3==0)for(unsigned i=0;i<count;i++)memcpy(packed+offset+i*(stride?stride:element),source+i*element,element);
  memcpy(expected,packed,sizeof(packed));bool force=trial%7==0,changed=force;
  for(unsigned i=0;i<count;i++){
   uint8_t *dst=expected+offset+i*(stride?stride:element);
   changed|=memcmp(dst,source+i*element,element)!=0;memcpy(dst,source+i*element,element);
  }
  assert(uniform_update(&layout,1,source,count*element,force)==changed);
  assert(!memcmp(packed,expected,sizeof(packed)));
 }
 puts("packed uniforms: padding, sparse writes, generations, epochs and 20000 packing oracles passed");
}
