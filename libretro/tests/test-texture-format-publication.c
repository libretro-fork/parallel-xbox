/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#define ctz32 __builtin_ctz
#include "hw/xbox/nv2a/nv2a_regs.h"
#ifdef _WIN32
#include <windows.h>
static double now_ns(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
#include <time.h>
static double now_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
#if defined(TEST_TEXTURE_PALETTE)
#define REGISTER_BASE NV_PGRAPH_TEXPALETTE0
#define METHOD_BASE NV097_SET_TEXTURE_PALETTE
#define TEST_SLOTS 4
#define BASE_WRITES 3
#define BASE_READS 8
#define TOGGLE_MASK NV097_SET_TEXTURE_PALETTE_LENGTH
#elif defined(TEST_TRANSFORM_MODE)
#define REGISTER_BASE NV_PGRAPH_CSV0_D
#define METHOD_BASE NV097_SET_TRANSFORM_EXECUTION_MODE
#define TEST_SLOTS 1
#define BASE_WRITES 2
#define BASE_READS 4
#define TOGGLE_MASK NV097_SET_TRANSFORM_EXECUTION_MODE_RANGE_MODE
#elif defined(TEST_FOG_COLOR)
#define REGISTER_BASE NV_PGRAPH_FOGCOLOR
#define METHOD_BASE NV097_SET_FOG_COLOR
#define TEST_SLOTS 1
#define BASE_WRITES 4
#define BASE_READS 8
#define TOGGLE_MASK (1u<<16)
#elif defined(TEST_LIGHT_CONTROL)
#define REGISTER_BASE NV_PGRAPH_CSV0_C
#define METHOD_BASE NV097_SET_LIGHT_CONTROL
#define TEST_SLOTS 1
#define BASE_WRITES 3
#define BASE_READS 6
#define TOGGLE_MASK NV097_SET_LIGHT_CONTROL_SEPARATE_SPECULAR
#elif defined(TEST_COLOR_MATERIAL)
#define REGISTER_BASE NV_PGRAPH_CSV0_C
#define METHOD_BASE NV097_SET_COLOR_MATERIAL
#define TEST_SLOTS 1
#define BASE_WRITES 4
#define BASE_READS 8
#define TOGGLE_MASK (1u<<6)
#else
#define REGISTER_BASE NV_PGRAPH_TEXFMT0
#define METHOD_BASE NV097_SET_TEXTURE_FORMAT
#define TEST_SLOTS 4
#define BASE_WRITES 9
#define BASE_READS 20
#define TOGGLE_MASK (1u<<8)
#endif
typedef struct {atomic_uint regs[4];bool dirty[4],texture_dirty[4];
#ifdef COUNT_ACCESS
unsigned reads,writes;
#endif
} PGRAPHState;
static unsigned slot_of(unsigned r){assert(r>=REGISTER_BASE&&(r-REGISTER_BASE)%4==0);unsigned s=(r-REGISTER_BASE)/4;assert(s<4);return s;}
static uint32_t pgraph_reg_r(PGRAPHState *p,unsigned r){
#ifdef COUNT_ACCESS
p->reads++;
#endif
return atomic_load_explicit(&p->regs[slot_of(r)],memory_order_relaxed);}
static void pgraph_reg_w(PGRAPHState *p,unsigned r,uint32_t value){unsigned s=slot_of(r);if(pgraph_reg_r(p,r)!=value)p->dirty[s]=true;
#ifdef COUNT_ACCESS
p->writes++;
#endif
atomic_store_explicit(&p->regs[s],value,memory_order_relaxed);}
#define PG_SET_MASK(r,m,v) do {uint32_t rv=pgraph_reg_r(pg,r);SET_MASK(rv,m,v);pgraph_reg_w(pg,r,rv);}while(0)
static void reference(PGRAPHState *pg,unsigned method __attribute__((unused)),uint32_t parameter)
/* REFERENCE */
static void current(PGRAPHState *pg,unsigned method __attribute__((unused)),uint32_t parameter)
/* CURRENT */
static uint32_t random_word(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
static volatile uint32_t checksum;
#ifndef COUNT_ACCESS
static double measure(void (*fn)(PGRAPHState*,unsigned,uint32_t),unsigned pattern){
PGRAPHState p={0};uint32_t seed=0x12345678,parameter=0x12345678;
reference(&p,METHOD_BASE,parameter);
for(unsigned i=0;i<10000;i++)fn(&p,METHOD_BASE,parameter);
const unsigned count=2000000;double start=now_ns();
for(unsigned i=0;i<count;i++){
 if(pattern==1)parameter^=TOGGLE_MASK;
 if(pattern==2)parameter=random_word(&seed);
 fn(&p,METHOD_BASE,parameter);
}
double elapsed=(now_ns()-start)/count;checksum^=atomic_load(&p.regs[0]);return elapsed;
}
static int compare(const void*a,const void*b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
#endif
int main(void){uint32_t seed=0x125487ae;unsigned changed=0,repeated=0;
for(unsigned trial=0;trial<200000;trial++){
 unsigned slot=trial%TEST_SLOTS,method=METHOD_BASE+slot*64;
 uint32_t parameter=random_word(&seed),prev=random_word(&seed);
 PGRAPHState a={0},b={0};
 if(trial&1){atomic_store(&a.regs[slot],prev);reference(&a,method,parameter);prev=atomic_load(&a.regs[slot]);}
 for(unsigned j=0;j<4;j++){
  uint32_t value=j==slot?prev:random_word(&seed);atomic_store(&a.regs[j],value);atomic_store(&b.regs[j],value);
  a.dirty[j]=b.dirty[j]=(trial&2)!=0;a.texture_dirty[j]=b.texture_dirty[j]=(trial&4)!=0;
 }
#ifdef COUNT_ACCESS
a.reads=a.writes=b.reads=b.writes=0;
#endif
 reference(&a,method,parameter);current(&b,method,parameter);
 bool differs=atomic_load(&a.regs[slot])!=prev;changed+=differs;repeated+=!differs;
 for(unsigned j=0;j<4;j++){assert(atomic_load(&a.regs[j])==atomic_load(&b.regs[j]));assert(a.dirty[j]==b.dirty[j]&&a.texture_dirty[j]==b.texture_dirty[j]);}
#ifdef COUNT_ACCESS
 assert(a.reads==BASE_READS&&a.writes==BASE_WRITES);assert(b.reads==1u+(unsigned)differs&&b.writes==differs);
#endif
}
printf("Register method: 200000 all-slot/random/reserved-bit/repeated/pre-dirty state cases passed (%u changed, %u unchanged)\n",changed,repeated);
#ifdef COUNT_ACCESS
printf("Publication counts: unchanged %u -> 0 stores, changed %u -> 1; dirty flags and final words identical\n",BASE_WRITES,BASE_WRITES);
#else
const char *names[]={"unchanged","alternating","randomized"};
for(unsigned pattern=0;pattern<3;pattern++){double times[2][5];for(unsigned round=0;round<5;round++)for(unsigned order=0;order<2;order++){unsigned variant=(round+order)%2;times[variant][round]=measure(variant?current:reference,pattern);}for(unsigned v=0;v<2;v++)qsort(times[v],5,sizeof(double),compare);printf("BENCH %s: baseline/current ns per method %.3f/%.3f; final register checksum %u\n",names[pattern],times[0][2],times[1][2],checksum);}
#endif
return 0;}
