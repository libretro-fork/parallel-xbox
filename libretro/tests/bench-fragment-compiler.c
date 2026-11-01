/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <sched.h>
#endif
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"

static void require(bool ok,const char *message) {if(!ok){fprintf(stderr,"%s\n",message);exit(1);}}
#ifndef PROFILE_ALLOCATIONS
static uint64_t wall_ns(void) {
#ifdef _WIN32
 LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
 return (uint64_t)((long double)now.QuadPart*1000000000.0L/freq.QuadPart);
#else
 struct timespec now;require(!clock_gettime(CLOCK_MONOTONIC,&now),"wall clock failed");return (uint64_t)now.tv_sec*1000000000u+now.tv_nsec;
#endif
}
static uint64_t cpu_ns(void) {
#ifdef _WIN32
 FILETIME created,exited,kernel,user;require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user),"CPU clock failed");
 ULARGE_INTEGER k={.LowPart=kernel.dwLowDateTime,.HighPart=kernel.dwHighDateTime},u={.LowPart=user.dwLowDateTime,.HighPart=user.dwHighDateTime};
 return (k.QuadPart+u.QuadPart)*100u;
#else
 struct timespec now;require(!clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&now),"CPU clock failed");return (uint64_t)now.tv_sec*1000000000u+now.tv_nsec;
#endif
}
#endif
#ifdef PROFILE_ALLOCATIONS
static bool recording;
static uint64_t calls,requested;
void *__real_malloc(size_t size);
void *__real_calloc(size_t count,size_t size);
void *__real_realloc(void *old,size_t size);
void *__wrap_malloc(size_t size) {if(recording){calls++;requested+=size;}return __real_malloc(size);}
void *__wrap_calloc(size_t count,size_t size) {if(recording){calls++;requested+=(uint64_t)count*size;}return __real_calloc(count,size);}
void *__wrap_realloc(void *old,size_t size) {if(recording){calls++;requested+=size;}return __real_realloc(old,size);}
#endif
static char *load(const char *directory,unsigned version,unsigned i) {
 char path[4096];int n=snprintf(path,sizeof(path),"%s/%s/%u.frag",directory,version?"candidate":"parent",i);require(n>0&&(size_t)n<sizeof(path),"input path too long");
 FILE *f=fopen(path,"rb");require(f!=NULL,"input file missing");require(!fseek(f,0,SEEK_END),"input seek failed");long length=ftell(f);require(length>0&&length<8*1024*1024,"invalid source size");rewind(f);
 char *s=malloc((size_t)length+1);require(s!=NULL,"allocation failed");require(fread(s,1,length,f)==(size_t)length,"input read failed");require(!fclose(f),"input close failed");s[length]=0;return s;
}
static void pin_thread(int cpu) {
 if(cpu<0)return;
#ifdef _WIN32
 require((unsigned)cpu<sizeof(DWORD_PTR)*8,"CPU index exceeds this processor group");
 require(SetThreadAffinityMask(GetCurrentThread(),((DWORD_PTR)1)<<cpu)!=0,"CPU affinity failed");
#else
 require(cpu<CPU_SETSIZE,"CPU index exceeds affinity set");cpu_set_t set;CPU_ZERO(&set);CPU_SET(cpu,&set);
 require(!sched_setaffinity(0,sizeof(set),&set),"CPU affinity failed");
#endif
}
static uint64_t mix(uint64_t hash,uint64_t value) {return (hash^value)*1099511628211ULL;}
static uint64_t layout_hash(const GlslcLayout *layout) {
 uint64_t h=mix(1469598103934665603ULL,layout->count);h=mix(h,layout->size);
 for(size_t i=0;i<layout->count;i++) {
  const GlslcUniform *m=&layout->members[i];for(const unsigned char *p=(const unsigned char *)m->name;*p;p++)h=mix(h,*p);
  h=mix(h,m->dim_v);h=mix(h,m->dim_a);h=mix(h,m->stride);h=mix(h,m->offset);
 }
 return h;
}
static volatile uint64_t sink;
static uint64_t compile(const char *source,const GlslcOptions *options,uint64_t *metadata) {
 GlslcResult result={0};if(!glslc_compile(GLSLC_STAGE_FRAGMENT,source,options,&result)){fprintf(stderr,"compile failed: %s\n",result.error);exit(1);}
 require(result.num_words>=5,"invalid module");uint64_t bytes=result.num_words*4;
 if(metadata)*metadata=mix(layout_hash(&result.uniforms),layout_hash(&result.push_constants));
 sink+=result.words[3];free(result.words);free(result.uniforms.members);free(result.push_constants.members);return bytes;
}
#ifndef PROFILE_ALLOCATIONS
static void batch(char **sources,unsigned count,unsigned repeat,const GlslcOptions *options,uint64_t *cpu,uint64_t *wall) {
 uint64_t c=cpu_ns(),w=wall_ns();for(unsigned r=0;r<repeat;r++)for(unsigned i=0;i<count;i++)compile(sources[i],options,NULL);
 *wall=wall_ns()-w;*cpu=cpu_ns()-c;
}
#endif
int main(int argc,char **argv) {
 require(argc==4||argc==5,"usage: benchmark CORPUS COUNT SPIRV_VERSION [CPU]");pin_thread(argc==5?atoi(argv[4]):-1);unsigned count=(unsigned)strtoul(argv[2],NULL,10),version=(unsigned)strtoul(argv[3],NULL,10);require(count>0&&count<=4096&&(version==15||version==16),"invalid benchmark arguments");
 GlslcOptions options={.spirv_1_6=version==16,.emit_layout=true};char **sources[2]={calloc(count,sizeof(char *)),calloc(count,sizeof(char *))};require(sources[0]&&sources[1],"allocation failed");
 for(unsigned v=0;v<2;v++)for(unsigned i=0;i<count;i++)sources[v][i]=load(argv[1],v,i);
 uint64_t modules[2]={0};for(unsigned i=0;i<count;i++){uint64_t a,b;modules[0]+=compile(sources[0][i],&options,&a);modules[1]+=compile(sources[1][i],&options,&b);require(a==b,"uniform metadata changed");}
 printf("{\"spirv\":%u,\"metadata_equal\":true,\"module_bytes\":[%" PRIu64 ",%" PRIu64 "]}\n",version,modules[0],modules[1]);
#ifdef PROFILE_ALLOCATIONS
 for(unsigned v=0;v<2;v++) {
  calls=requested=0;recording=true;for(unsigned i=0;i<count;i++)compile(sources[v][i],&options,NULL);recording=false;require(calls>0,"allocation wrappers inactive");
  printf("{\"spirv\":%u,\"version\":%u,\"compiles\":%u,\"allocation_calls\":%" PRIu64 ",\"requested_bytes\":%" PRIu64 ",\"module_bytes\":%" PRIu64 "}\n",version,v,count,calls,requested,modules[v]);
 }
#else
 unsigned repeat=1;uint64_t cpu,wall;
 for(;;){batch(sources[0],count,repeat,&options,&cpu,&wall);if(cpu>=400000000u)break;require(repeat<65536,"timer calibration failed");repeat*=2;}
 for(unsigned round=0;round<8;round++)for(unsigned order=0;order<2;order++) {
  unsigned v=order^(round&1u);batch(sources[v],count,repeat,&options,&cpu,&wall);
  printf("{\"spirv\":%u,\"version\":%u,\"round\":%u,\"repeat\":%u,\"compiles\":%u,\"cpu_ns\":%" PRIu64 ",\"wall_ns\":%" PRIu64 "}\n",version,v,round,repeat,count*repeat,cpu,wall);
 }
#endif
 for(unsigned v=0;v<2;v++){for(unsigned i=0;i<count;i++)free(sources[v][i]);free(sources[v]);}return sink==0;
}
