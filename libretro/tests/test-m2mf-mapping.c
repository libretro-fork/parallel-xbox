/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
typedef uint64_t hwaddr;
/* TYPES */
typedef struct {hwaddr bytes;} MemoryRegion;
typedef struct {MemoryRegion ramin,*vram;uint8_t *ramin_ptr,*vram_ptr;
 struct {uint64_t core_clock_freq;} pramdac;
 struct {uint32_t numerator,denominator;uint64_t time_offset;}ptimer;} NV2AState;
static unsigned ctz32(uint32_t x){return __builtin_ctz(x);}
#define MIN(a,b) ((a)<(b)?(a):(b))
static unsigned loads,clock_reads;
static uint64_t clock_ns;
static hwaddr memory_region_size(MemoryRegion *m){return m->bytes;}
static uint32_t ldl_le_p(const void *p){const uint8_t*b=p;loads++;return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(8*i);}
/* DMA */
#define QEMU_CLOCK_VIRTUAL 0
#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)
static uint64_t qemu_clock_get_ns(unsigned kind){clock_reads++;return clock_ns++;}
static uint64_t muldiv64(uint64_t v,uint64_t a,uint64_t b){return (__uint128_t)v*a/b;}
/* CONSTANTS */
/* TIMER */
int main(void)
{
 uint8_t ramin[64]={0},vram[256]={0};MemoryRegion vr={256};
 NV2AState d={.ramin={64},.vram=&vr,.ramin_ptr=ramin,.vram_ptr=vram};unsigned cases=0;
 for(unsigned cap=0;cap<=64;cap++)
 for(unsigned off=0;off<=65;off++) {
  d.ramin.bytes=cap;NVDMAView view={.data=vram,.address=99,.bytes=99};loads=0;
  bool okay=nv_dma_map_checked(&d,off,&view);
  assert(okay==(off+12<=cap));assert(loads==(okay?3u:0u));
  if(okay)assert(view.data==vram&&view.address==0&&view.bytes==1);
  else assert(!view.data&&!view.address&&!view.bytes);
  cases++;
 }
 d.ramin.bytes=64;
 const uint32_t bases[]={0,1,127,255,256,4095,0x08000001};
 const uint32_t limits[]={0,1,127,255,256,UINT32_MAX};
 for(unsigned b=0;b<sizeof(bases)/sizeof(bases[0]);b++)
 for(unsigned l=0;l<sizeof(limits)/sizeof(limits[0]);l++) {
  uint32_t base=bases[b];put(ramin,0x3d|((base&4095)<<20));put(ramin+4,limits[l]);put(ramin+8,base&~4095u);
  NVDMAView view;bool okay=nv_dma_map_checked(&d,0,&view);base&=0x07ffffff;
  assert(okay==(base<256));if(!okay)continue;
  uint64_t count=(uint64_t)limits[l]+1;if(count>256-base)count=256-base;
  assert(view.data==vram+base&&view.bytes==count&&view.address==base);
  assert(view.dma_class==0x3d&&view.dma_target==0);
  for(unsigned offset=0;offset<=257;offset++)
  for(unsigned size=0;size<=8;size++) {
   bool fits=offset<=count&&size<=count-offset;
   uint8_t *p=nv_dma_view_range(&view,offset,size);
   assert(p==(fits?vram+base+offset:NULL));cases++;
  }
  assert(!nv_dma_view_range(&view,UINT64_MAX,1));
  assert(!nv_dma_view_range(&view,0,UINT64_MAX));
 }
 NVDMAView view;assert(!nv_dma_map_checked(&d,UINT64_MAX,&view));
 for(unsigned target=0;target<4;target++) {
  put(ramin,0x03|(target<<16));put(ramin+4,3);put(ramin+8,0);
  assert(nv_dma_map_checked(&d,0,&view));
  assert(view.dma_class==3&&view.dma_target==target);cases++;
 }
 d.pramdac.core_clock_freq=1000000000;d.ptimer.numerator=d.ptimer.denominator=1;d.ptimer.time_offset=0;
 clock_ns=(UINT64_C(1)<<27)-1;clock_reads=0;
 assert(ptimer_snapshot(&d)==UINT64_C(0xffffffe0)&&clock_reads==1);
 assert(ptimer_snapshot(&d)==UINT64_C(0x100000000)&&clock_reads==2);cases+=2;

 for(unsigned i=0;i<10000;i++) {
  d.pramdac.core_clock_freq=133000000+(i%7)*1000000;
  d.ptimer.numerator=1+i%17;d.ptimer.denominator=1+i%11;
  d.ptimer.time_offset=(uint64_t)i<<42;
  uint64_t now=(UINT64_C(1)<<27)-1+i*7919;
  clock_ns=now;clock_reads=0;
  uint64_t ticks=(__uint128_t)now*d.pramdac.core_clock_freq/1000000000;
  ticks=(__uint128_t)ticks*d.ptimer.denominator/d.ptimer.numerator;
  uint64_t expected=((ticks+d.ptimer.time_offset)<<5)&UINT64_C(0x1fffffffffffffe0);
  uint64_t timestamp=ptimer_snapshot(&d);
  assert(timestamp==expected&&clock_reads==1);
  assert(((uint64_t)(uint32_t)(timestamp>>32)<<32|(uint32_t)timestamp)==expected);cases++;
 }
 printf("PASS: %u checked DMA/range/timestamp cases\n",cases);
 return 0;
}
