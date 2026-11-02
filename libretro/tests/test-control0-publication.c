/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#define ctz32 __builtin_ctz
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef struct {
 atomic_uint regs[2];bool dirty[2];uint32_t observed[2],surface_xor[2];unsigned calls;
#ifdef CHECKED
 unsigned reads,writes;
#endif
} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
static unsigned index_of(uint32_t r){assert(r==NV_PGRAPH_CONTROL_0||r==NV_PGRAPH_SETUPRASTER);return r==NV_PGRAPH_SETUPRASTER;}
static uint32_t pgraph_reg_r(PGRAPHState *pg,uint32_t r){
#ifdef CHECKED
 pg->reads++;
#endif
 return atomic_load_explicit(&pg->regs[index_of(r)],memory_order_relaxed);
}
static void pgraph_reg_w(PGRAPHState *pg,uint32_t r,uint32_t v){
 if(pgraph_reg_r(pg,r)!=v)pg->dirty[index_of(r)]=true;
#ifdef CHECKED
 pg->writes++;
#endif
 atomic_store_explicit(&pg->regs[index_of(r)],v,memory_order_relaxed);
}
#define PG_SET_MASK(r,m,v) do{uint32_t x=pgraph_reg_r(pg,r);SET_MASK(x,m,v);pgraph_reg_w(pg,r,x);}while(0)
static void surface_update(NV2AState *d,bool upload,bool color,bool zeta){
 assert(!upload&&color&&zeta);PGRAPHState *pg=&d->pgraph;pg->calls++;
 for(unsigned i=0;i<2;i++){
  pg->observed[i]=atomic_load_explicit(&pg->regs[i],memory_order_relaxed);
  /* A surface callback may update state before the method reads it. */
  atomic_store_explicit(&pg->regs[i],pg->observed[i]^pg->surface_xor[i],memory_order_relaxed);
 }
}
static struct {struct{void(*surface_update)(NV2AState*,bool,bool,bool);}ops;} renderer={{surface_update}};
#define pgraph_renderer(pg) (&renderer)
__attribute__((noinline)) static void reference(NV2AState *d,uint32_t parameter){PGRAPHState *pg=&d->pgraph;
/* REFERENCE */
}
__attribute__((noinline)) static void current(NV2AState *d,uint32_t parameter){PGRAPHState *pg=&d->pgraph;
/* CURRENT */
}
static uint32_t seed=0x73ab1234;
static uint32_t random_value(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void correctness(void){
 for(unsigned t=0;t<200000;t++){
  NV2AState a={0},b={0};uint32_t p=random_value(),old[2];
  for(unsigned i=0;i<2;i++){
   old[i]=random_value();atomic_init(&a.pgraph.regs[i],old[i]);atomic_init(&b.pgraph.regs[i],old[i]);
   a.pgraph.dirty[i]=b.pgraph.dirty[i]=(t>>i)&1;
   a.pgraph.surface_xor[i]=b.pgraph.surface_xor[i]=t%3?0:random_value();
  }
  if(t%4==0){p=0;uint32_t v=old[0]^a.pgraph.surface_xor[0];
   if(v&NV_PGRAPH_CONTROL_0_STENCIL_WRITE_ENABLE)p|=NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE;
   if(v&NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE)p|=NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE;
  }
  reference(&a,p);current(&b,p);
  for(unsigned i=0;i<2;i++){
   assert(atomic_load(&a.pgraph.regs[i])==atomic_load(&b.pgraph.regs[i]));
   assert(a.pgraph.dirty[i]==b.pgraph.dirty[i]);
   assert(a.pgraph.observed[i]==old[i]&&b.pgraph.observed[i]==old[i]);
  }
  assert(a.pgraph.calls==1&&b.pgraph.calls==1);
#ifdef CHECKED
  uint32_t after=old[0]^a.pgraph.surface_xor[0];
  bool changed=atomic_load(&b.pgraph.regs[0])!=after;
  assert(a.pgraph.reads==6&&a.pgraph.writes==3);
  assert(b.pgraph.reads==3+(unsigned)changed&&b.pgraph.writes==1+(unsigned)changed);
#endif
 }
 puts("200000 CONTROL0 cases: final bits/dirty flags, pre-update surface state, callback changes and atomic-access counts passed");
}
#ifndef CHECKED
static volatile uint32_t sink;
static double bench(bool candidate,unsigned pattern){
 NV2AState d={0};void(*fn)(NV2AState*,uint32_t)=candidate?current:reference;
 struct timespec a,b;timespec_get(&a,TIME_UTC);
 for(unsigned i=0;i<2000000;i++){
  uint32_t p=pattern==0?0:pattern==1?((i&1)?NV097_SET_CONTROL0_STENCIL_WRITE_ENABLE|NV097_SET_CONTROL0_Z_PERSPECTIVE_ENABLE:0):i*2654435761u;
  fn(&d,p);
 }
 timespec_get(&b,TIME_UTC);sink=atomic_load(&d.pgraph.regs[0])+d.pgraph.calls;
 return ((b.tv_sec-a.tv_sec)*1e9+b.tv_nsec-a.tv_nsec)/2000000;
}
#endif
int main(void){correctness();
#ifndef CHECKED
 puts("Isolated relaxed-atomic method with modeled surface callback; ns/call, no GPU/game timing.");
 for(unsigned r=0;r<5;r++)for(unsigned p=0;p<3;p++){
  double t[2];for(unsigned k=0;k<2;k++){unsigned w=k^(r&1);t[w]=bench(w,p);}
  printf("round=%u pattern=%u baseline=%.3f current=%.3f ns/call\n",r,p,t[0],t[1]);
 }
#endif
}
