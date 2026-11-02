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
/* TYPES */
typedef struct {ShaderState ahead_state;} PGRAPHVkState;
typedef struct {PipelineKey key;} LibJob;
#ifdef CHECKED
static size_t copied;
static void *count_copy(void *dst,const void *src,size_t bytes){copied+=bytes;return memcpy(dst,src,bytes);}
#define memcpy count_copy
#endif
/* FUNCTIONS */
#ifdef CHECKED
#undef memcpy
#endif
static uint32_t seed=0x712fa938;
static uint32_t random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void fill(void *dst,size_t size){for(size_t i=0;i<size;i++)((uint8_t*)dst)[i]=(uint8_t)random_value();}
static void correctness(void){
 PipelineKey draw,saved,expected;PGRAPHVkState r,saved_r;fill(&draw,sizeof(draw));fill(&r,sizeof(r));
 for(unsigned trial=0;trial<100000;trial++){
  struct {uint8_t before[31];LibJob job;uint8_t after[17];} a,b;
  memset(&a,0x97,sizeof(a));memset(&b,0x38,sizeof(b));
  ((uint8_t*)&draw)[random_value()%sizeof(draw)]=(uint8_t)random_value();
  ((uint8_t*)&r)[random_value()%sizeof(r)]=(uint8_t)random_value();
  memcpy(&saved,&draw,sizeof(draw));memcpy(&saved_r,&r,sizeof(r));
  memcpy(&expected,&draw,sizeof(draw));memcpy(&expected.shader_state,&r.ahead_state,sizeof(ShaderState));
#ifdef CHECKED
  copied=0;
#endif
  reference(&r,&a.job,&draw);
#ifdef CHECKED
  assert(copied==sizeof(PipelineKey)+sizeof(ShaderState));copied=0;
#endif
  current(&r,&b.job,&draw);
#ifdef CHECKED
  assert(copied==sizeof(PipelineKey));
#endif
  assert(!memcmp(&a.job.key,&expected,sizeof(expected)));
  assert(!memcmp(&b.job.key,&expected,sizeof(expected)));
  assert(!memcmp(&draw,&saved,sizeof(draw))&&!memcmp(&r,&saved_r,sizeof(r)));
  for(unsigned i=0;i<31;i++)assert(a.before[i]==0x97&&b.before[i]==0x38);
  for(unsigned i=0;i<17;i++)assert(a.after[i]==0x97&&b.after[i]==0x38);
  /* The worker key must own its bytes independently of both sources. */
  memset(&draw,0xa6,sizeof(draw));memset(&r,0x5b,sizeof(r));
  assert(!memcmp(&b.job.key,&expected,sizeof(expected)));
  memcpy(&draw,&saved,sizeof(draw));memcpy(&r,&saved_r,sizeof(r));
 }
 puts("100000 speculative-key oracle/source/guard/ownership cases passed; counted build checks exact copied bytes");
}
#ifndef CHECKED
static volatile unsigned sink;
static double bench(unsigned which,unsigned changing){
 PipelineKey draw;PGRAPHVkState r;LibJob job;fill(&draw,sizeof(draw));fill(&r,sizeof(r));
 void(*fn)(PGRAPHVkState*,LibJob*,const PipelineKey*)=which?current:reference;
 struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<3000000;i++){
  if(changing){((uint8_t*)&r.ahead_state)[0]=(uint8_t)i;((uint8_t*)&draw)[sizeof(draw)-1]=(uint8_t)(i>>3);}
  fn(&r,&job,&draw);
 }
 timespec_get(&b,TIME_UTC);sink=((uint8_t*)&job.key.shader_state)[0]+((uint8_t*)&job.key)[sizeof(PipelineKey)-1];
 return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/3000000;
}
#endif
int main(void){correctness();printf("key=%zu shader=%zu baseline-copied=%zu current-copied=%zu\n",sizeof(PipelineKey),sizeof(ShaderState),sizeof(PipelineKey)+sizeof(ShaderState),sizeof(PipelineKey));
#ifndef CHECKED
 puts("Isolated copy block, actual types, no hash/queue/driver timing; ns/key");
 for(unsigned round=0;round<5;round++)for(unsigned changing=0;changing<2;changing++){
  double t[2];for(unsigned k=0;k<2;k++){unsigned w=k^(round&1);t[w]=bench(w,changing);}
  printf("round=%u changing=%u baseline=%.3f current=%.3f ns/key\n",round,changing,t[0],t[1]);
 }
#endif
}
