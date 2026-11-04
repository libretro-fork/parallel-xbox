/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t hwaddr,ram_addr_t,vaddr;
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define RAM_ADDR_INVALID UINT64_MAX
#define BP_MEM_READ 1
#define BP_MEM_WRITE 2
#define LR_DSP_EXTERNAL_GUARD ((void)0)
typedef struct { uint64_t base; } MemoryRegion;
typedef struct MemAccessCallback {
 MemoryRegion *mr;hwaddr addr,len;
 void (*func)(void*,MemoryRegion*,hwaddr,hwaddr,bool);
 void *opaque;int watch_flags;struct MemAccessCallback *next;
} MemAccessCallback;
typedef struct { struct { MemAccessCallback *first; } mem_access_callbacks; } CPUState;
#define QTAILQ_FOREACH(cb,h,e) for((cb)=(h)->first;(cb);(cb)=(cb)->next)
static uint64_t memory_region_get_ram_addr(MemoryRegion *mr){return mr->base;}
static int retro_atomic_load_relaxed_int(const int *p){return *p;}
typedef struct { unsigned calls;uint64_t offset,len;bool write; } Record;
static void record(void *arg,MemoryRegion *mr,hwaddr a,hwaddr n,bool write) {
 Record *r=arg;(void)mr;r->calls++;r->offset=a;r->len=n;r->write=write;
}
static bool dsp_accept=true;
static uint64_t dsp_offset,dsp_len;
static unsigned dsp_matches;
static bool lr_dsp_ownership_match(MemoryRegion *mr,hwaddr a,hwaddr n) {
 (void)mr;dsp_matches++;dsp_offset=a;dsp_len=n;return dsp_accept;
}
static void lr_dsp_ownership_callback(void *arg,MemoryRegion *mr,hwaddr a,hwaddr n,bool write) {
 record(arg,mr,a,n,write);
}
#define RCU_READ_LOCK_GUARD() ((void)0)
#define QTAILQ_FOREACH_RCU QTAILQ_FOREACH
/* PRODUCTION */
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m,uint64_t *start,uint64_t *bytes) {
 __uint128_t ae=(__uint128_t)a+n,be=(__uint128_t)b+m;
 __uint128_t lo=a>b?a:b,hi=ae<be?ae:be;
 if(!n||!m||lo>=hi)return false;
 *start=(uint64_t)lo;*bytes=(uint64_t)(hi-lo);return true;
}
static uint64_t seed=0x9172635498ab21;
static uint64_t random64(void){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;}
int main(void) {
 CPUState cpu={0};MemoryRegion mr={.base=4096};MemAccessCallback cb[3];Record records[3];unsigned cases=0;
 const uint64_t edges[][4]={
  {10,35,20,10},{10,35,40,10},{20,10,10,35},{20,0,20,10},
  {0,0,0,1},{0,1,0,0},{0,1,1,1},{9,1,0,9},
  {UINT64_MAX,1,UINT64_MAX,1},{UINT64_MAX-10,11,UINT64_MAX-2,3},
  {UINT64_MAX-2,3,UINT64_MAX-10,11},{0,UINT64_MAX,UINT64_MAX,1},
  {0,UINT64_MAX,0,UINT64_MAX},{UINT64_MAX-1,2,0,UINT64_MAX}
 };
 for(unsigned pass=0;pass<100014;pass++)for(unsigned write=0;write<2;write++)for(unsigned accept=0;accept<2;accept++) {
  memset(cb,0,sizeof(cb));memset(records,0,sizeof(records));cpu.mem_access_callbacks.first=cb;
  uint64_t a,n,b,m;
  if(pass<14){a=edges[pass][0];n=edges[pass][1];b=edges[pass][2];m=edges[pass][3];mr.base=0;}
  else {mr.base=4096;a=mr.base+random64()%4096;n=random64()%2048;b=mr.base+random64()%4096;m=random64()%2048;}
  dsp_accept=accept;dsp_matches=0;int flags=0;uint64_t starts[3]={0},lengths[3]={0};bool hit[3];
  for(unsigned i=0;i<3;i++) {
   cb[i].mr=&mr;cb[i].addr=b;cb[i].len=i==2?0:m;cb[i].opaque=&records[i];
   cb[i].func=i==1?lr_dsp_ownership_callback:record;cb[i].watch_flags=i==0?1:i==1?2:0;cb[i].next=i==2?NULL:&cb[i+1];
   hit[i]=overlap(a,n,cb[i].addr,cb[i].len,&starts[i],&lengths[i]);
   if(hit[i]&&(i!=1||accept))flags|=cb[i].watch_flags;
  }
  assert(mem_access_callback_address_matches(&cpu,a,n)==flags);
  if(hit[1]){assert(dsp_matches==1&&dsp_offset==starts[1]-cb[1].addr&&dsp_len==lengths[1]);}
  else assert(!dsp_matches);
  mem_check_access_callback_ramaddr(&cpu,a,n,write?BP_MEM_WRITE:BP_MEM_READ);
  for(unsigned i=0;i<3;i++) {
   bool called=hit[i]&&(i!=1||accept);assert(records[i].calls==(unsigned)called);
   if(called){assert(records[i].offset==starts[i]-mr.base&&records[i].len==lengths[i]&&records[i].write==(bool)write);}
  }
  cases++;
 }
 printf("PASS:%u callback match/dispatch spans, exact overlaps/empty/final-address/DSP gates/read-write flags\n",cases);
}
