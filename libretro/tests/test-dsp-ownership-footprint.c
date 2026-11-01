/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include "hw/xbox/mcpx/apu/apu_regs.h"
#define DSP_RAM_PAGES 32768
#define TARGET_PAGE_SIZE 4096
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
typedef uint64_t ram_addr_t;
static uint64_t ownership_pages[512],ownership_tables[512];
static retro_eventcount_t gpu_ownership_changed;
/* PRODUCTION GPU */
static uint8_t ram[1024*1024];static unsigned reads;
typedef struct {uint8_t *ram_ptr;void *ram;uint32_t regs[0x20000];} MCPXAPUState;
static size_t memory_region_size(void*p){assert(p==ram);return sizeof(ram);}
static ram_addr_t memory_region_get_ram_addr(void*p){assert(p==ram);return 0;}
static uint32_t apu_reg_r(MCPXAPUState*d,unsigned n){return d->regs[n];}
static uint32_t ldl_le_p(uint8_t*p){assert(p>=ram && p+4<=ram+sizeof(ram));unsigned page=(unsigned)(p-ram)/4096;assert(retro_atomic_load_acquire_int(&gpu_page_refs[page])==-1);uint32_t v;memcpy(&v,p,4);reads++;return v;}
typedef struct MemAccessCallback {uint64_t addr,len;retro_atomic_int_t watch_flags;struct MemAccessCallback *next;} MemAccessCallback;
static MemAccessCallback *own_callback;
static struct {struct {MemAccessCallback *first;} mem_access_callbacks;} cpu;
#define QTAILQ_FOREACH(v,h,f) for((v)=(h)->first;(v);(v)=(v)->next)
#define qemu_get_cpu(i) (&cpu)
static bool translated[256];
#define DIRTY_MEMORY_CODE 1
static bool physical_memory_get_dirty_flag(uint64_t addr,unsigned client){assert(client==1 && addr<sizeof(ram));return !translated[addr/4096];}
/* PRODUCTION FOOTPRINT */
static void clear(void){memset(ownership_pages,0,sizeof(ownership_pages));memset(ownership_tables,0,sizeof(ownership_tables));}
int main(void){
 static MCPXAPUState d;d.ram=ram;d.ram_ptr=ram;
 const unsigned base[]={NV_PAPU_EPSADDR,NV_PAPU_EPFADDR},limit[]={NV_PAPU_EPSMAXSGE,NV_PAPU_EPFMAXSGE};
 assert(retro_eventcount_init(&gpu_ownership_changed));
 for(unsigned i=0;i<2;i++)d.regs[base[i]]=4096*(i+1);
 for(unsigned run=0;run<2000;run++){
  for(unsigned i=0;i<2;i++){uint32_t target=0x10000+i*4096+run;memcpy(ram+d.regs[base[i]],&target,4);}
  clear();unsigned before=reads;assert(ownership_reserve_tables(&d));assert(reads==before);assert(ownership_candidate(&d));assert(reads==before+2);
  uint64_t expected=(1ull<<1)|(1ull<<2)|(1ull<<16)|(1ull<<17)|(run?(1ull<<18):0);
  assert(ownership_pages[0]==expected && ownership_tables[0]==((1ull<<1)|(1ull<<2)));
  for(unsigned w=1;w<512;w++)assert(!ownership_pages[w] && !ownership_tables[w]);
  assert(gpu_try_dsp_pages());gpu_release_dsp_pages();
 }
 clear();unsigned before=reads;lr_gpu_ram_claim(17,0x1000,4);assert(!ownership_reserve_tables(&d) && reads==before);assert(!retro_atomic_load_relaxed_int(&gpu_page_refs[2]));lr_gpu_ram_drop(17);
 clear();assert(ownership_reserve_tables(&d));translated[16]=true;assert(!ownership_candidate(&d));translated[16]=false;
 MemAccessCallback cb={.addr=0x10000,.len=4096};retro_atomic_int_init(&cb.watch_flags,1);cpu.mem_access_callbacks.first=&cb;assert(!ownership_candidate(&d));retro_atomic_store_relaxed_int(&cb.watch_flags,0);assert(ownership_candidate(&d));cpu.mem_access_callbacks.first=NULL;gpu_release_dsp_pages();
 uint32_t alias=0x2000;memcpy(ram+0x1000,&alias,4);clear();assert(ownership_reserve_tables(&d) && !ownership_candidate(&d));gpu_release_dsp_pages();
 clear();d.regs[limit[1]]=UINT32_MAX;before=reads;assert(!ownership_reserve_tables(&d) && reads==before);
 lr_gpu_ram_claim(0,0,4);assert(!gpu_try_dsp_pages());
 retro_eventcount_free(&gpu_ownership_changed);puts("EP footprint: reserved-before-read, 2000 unaligned traversals, GPU conflicts, code pages, callbacks, table aliases and invalid bounds passed");
}
