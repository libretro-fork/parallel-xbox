/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#define TARGET_PAGE_SIZE 4096
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef uint64_t hwaddr;
typedef void (*dsp_dma_run_func)(void*,uint8_t*,size_t);
static uint8_t ram[65536];
typedef struct {void *ram;uint8_t *ram_ptr;} MCPXAPUState;
static bool ownership_is_worker,ownership_gp_dma_guard=true,pending,expect_pending;
static unsigned waits,physical_reads,chunks,dirty,read_guards,write_guards;
static uint64_t live,readonly=1ull<<1;
static int address_space_memory;
static uint64_t memory_region_size(void *p){assert(p==ram);return sizeof(ram);}
static uint64_t memory_region_get_ram_addr(void *p){assert(p==ram);return 0;}
static void guard(uint64_t addr,uint64_t len,bool write){
 if(write)write_guards++;else read_guards++;
 for(uint64_t page=addr/4096;pending && page<=(addr+len-1)/4096;page++)
  if((live&(1ull<<page)) && (write || !(readonly&(1ull<<page)))){waits++;pending=false;break;}
}
static void lr_dsp_ownership_ram(uint64_t addr,uint64_t len){guard(addr,len,true);}
static void lr_dsp_ownership_read(uint64_t addr,uint64_t len){guard(addr,len,false);}
static uint32_t ldl_le_p(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static uint32_t ldl_le_phys(void *space,uint64_t addr){assert(space==&address_space_memory);physical_reads++;lr_dsp_ownership_read(addr,4);return ldl_le_p(ram+addr);}
static void memory_region_set_dirty(void *p,uint64_t addr,size_t len){assert(p==ram && addr+len<sizeof(ram));dirty++;}
/* PRODUCTION WALK */
static void transfer(void *p,uint8_t *data,size_t len){assert(data>=ram && data+len<ram+sizeof(ram));assert(pending==expect_pending);chunks++;}
int main(void){
 MCPXAPUState d={ram,ram};uint32_t target=0x3000;memcpy(ram+0x1000,&target,4);target=0x4000;memcpy(ram+0x1008,&target,4);
 for(unsigned i=0;i<2000;i++){
  pending=true;live=(1ull<<1)|(1ull<<3);expect_pending=false;unsigned w=waits;
  scatter_gather_walk(&d,0x1000,1,0,16,false,transfer,NULL);assert(waits==w+1);
  pending=true;scatter_gather_walk(&d,0x1000,1,0,16,true,transfer,NULL);assert(waits==w+2);
  pending=true;expect_pending=true;scatter_gather_walk(&d,0x1000,1,4096,16,false,transfer,NULL);assert(waits==w+2);
  pending=true;expect_pending=false;scatter_gather_walk(&d,0x1000,1,4095,2,false,transfer,NULL);assert(waits==w+3);
  target=0x1000;memcpy(ram+0x1000,&target,4);pending=true;expect_pending=true;
  scatter_gather_walk(&d,0x1000,1,0,16,false,transfer,NULL);assert(waits==w+3);
  pending=true;expect_pending=false;scatter_gather_walk(&d,0x1000,1,0,16,true,transfer,NULL);assert(waits==w+4);
  target=0x3000;memcpy(ram+0x1000,&target,4);
 }
 ownership_is_worker=true;pending=true;expect_pending=true;unsigned g=read_guards+write_guards,ph=physical_reads;
 scatter_gather_walk(&d,0x1000,1,0,16,true,transfer,NULL);assert(g==read_guards+write_guards && ph==physical_reads);
 assert(dirty==4001);
 puts("PASS: production DMA walker guards CPU GP read/write spans before pointer use, shares read-only tables, handles page splits/disjoint ranges and never self-guards worker DMA (2000 rounds)");
}
