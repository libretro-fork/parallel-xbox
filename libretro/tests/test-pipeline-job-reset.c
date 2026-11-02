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
#ifdef CHECKED
static size_t cleared;
static void *count_clear(void *dst,int value,size_t bytes){cleared+=bytes;return memset(dst,value,bytes);}
#define memset count_clear
#endif
/* FUNCTIONS */
#ifdef CHECKED
#undef memset
#endif
static uint32_t seed=0x7ab23146;
static uint32_t random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void correctness(void){
 PipelineKey key;for(size_t i=0;i<sizeof(key);i++)((uint8_t*)&key)[i]=(uint8_t)random_value();
 LibJob zero;memset(&zero,0,sizeof(zero));
 for(unsigned trial=0;trial<100000;trial++){
  struct {uint8_t before[31];LibJob job;uint8_t after[17];} a,b;
  memset(&a,0x97,sizeof(a));memset(&b,0x38,sizeof(b));
  ((uint8_t*)&key)[random_value()%sizeof(key)]=(uint8_t)random_value();
  memcpy(&a.job.key,&key,sizeof(key));memcpy(&b.job.key,&key,sizeof(key));
  LibJob expected;memset(&expected,0,sizeof(expected));memcpy(&expected.key,&key,sizeof(key));
#ifdef CHECKED
  cleared=0;
#endif
  reference_reset(&a.job);
#ifdef CHECKED
  assert(cleared==sizeof(LibJob));cleared=0;
#endif
  pipeline_job_reset(&b.job);
#ifdef CHECKED
  assert(cleared==sizeof(LibJob)-sizeof(PipelineKey));
#endif
  assert(!memcmp(&a.job,&zero,sizeof(LibJob)));
  assert(!memcmp(&b.job,&expected,sizeof(LibJob)));
  for(unsigned i=0;i<31;i++)assert(a.before[i]==0x97&&b.before[i]==0x38);
  for(unsigned i=0;i<17;i++)assert(a.after[i]==0x97&&b.after[i]==0x38);
 }
 puts("100000 poisoned resets: every metadata/padding byte zero, every key byte retained, guards intact; counted build checks clear bytes");
}
#ifndef CHECKED
static volatile unsigned sink;
static double bench(unsigned which){
 LibJob job;memset(&job,0xa7,sizeof(job));
 void(*fn)(LibJob*)=which?pipeline_job_reset:reference_reset;
 struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<5000000;i++)fn(&job);
 timespec_get(&b,TIME_UTC);sink=((uint8_t*)&job)[0]+((uint8_t*)&job)[sizeof(job)-1];
 return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/5000000;
}
#endif
int main(void){correctness();printf("job=%zu key=%zu baseline-clear=%zu current-clear=%zu\n",sizeof(LibJob),sizeof(PipelineKey),sizeof(LibJob),sizeof(LibJob)-sizeof(PipelineKey));
#ifndef CHECKED
 puts("Isolated reset function; actual declarations; no fill/hash/queue/driver timing; ns/reset");
 for(unsigned round=0;round<7;round++){
  double t[2];for(unsigned k=0;k<2;k++){unsigned w=k^(round&1);t[w]=bench(w);}
  printf("round=%u baseline=%.3f current=%.3f ns/reset\n",round,t[0],t[1]);
 }
#endif
}
