/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t hwaddr;typedef int MemTxAttrs;typedef int MemTxResult;
typedef struct {void*ram_block;} MemoryRegion;typedef int AddressSpace;
static MemoryRegion mr;static uint8_t ram[32];static bool external,direct=true,guarded;
static unsigned loans,guard_calls,dirty,mmio,read_calls,write_calls;
static bool lr_dsp_external_begin(void){if(external)loans++;return external;}
static void cleanup(bool*held){if(*held){assert(loans);loans--;}}
#define LR_DSP_EXTERNAL_GUARD bool held __attribute__((cleanup(cleanup)))=lr_dsp_external_begin()
static void lr_dsp_ownership_ram(uint64_t addr,uint64_t len)
{assert(addr==0x1008 && len==4);assert(loans==(unsigned)external);guarded=true;guard_calls++;write_calls++;}
static void lr_dsp_ownership_read(uint64_t addr,uint64_t len){lr_dsp_ownership_ram(addr,len);write_calls--;read_calls++;}
#define glue(a,b) a
#define SUFFIX
#define ARG1_DECL AddressSpace *as
#define TRANSLATE(a,x,l,w,attrs) translate(a,x,l)
#define RCU_READ_LOCK() ((void)0)
#define RCU_READ_UNLOCK() ((void)0)
#define MO_32 4
#define MEMTX_OK 0
enum device_endian {DEVICE_NATIVE_ENDIAN,DEVICE_LITTLE_ENDIAN,DEVICE_BIG_ENDIAN};
static MemoryRegion *translate(hwaddr a,hwaddr*x,hwaddr*l){*x=8;return &mr;}
static bool memory_access_is_direct(MemoryRegion*m,bool w,MemTxAttrs attrs){return direct;}
static bool prepare_mmio_access(MemoryRegion*m){return false;}
static int devend_memop(enum device_endian e){return 0;}
static int memory_region_dispatch_read(MemoryRegion*m,hwaddr a,uint64_t*v,int op,MemTxAttrs attrs)
{assert(loans==(unsigned)external);mmio++;*v=0x99;return 0;}
static int memory_region_dispatch_write(MemoryRegion*m,hwaddr a,uint64_t v,int op,MemTxAttrs attrs)
{assert(loans==(unsigned)external);mmio++;return 0;}
static void bql_unlock(void){}
static void fuzz_dma_read_cb(hwaddr a,unsigned len,MemoryRegion*m){}
static uint64_t memory_region_get_ram_addr(MemoryRegion*m){return 0x1000;}
static uint8_t *qemu_map_ram_ptr(void*b,hwaddr addr){assert(guarded && loans==(unsigned)external);return ram+addr;}
static uint32_t ldl_p(void*p){uint32_t v;assert(guarded);memcpy(&v,p,4);return v;}
#define ldl_le_p ldl_p
#define ldl_be_p ldl_p
static void stl_p(void*p,uint32_t v){assert(guarded);memcpy(p,&v,4);}
#define stl_le_p stl_p
#define stl_be_p stl_p
static void invalidate_and_set_dirty(MemoryRegion*m,hwaddr a,unsigned n)
{assert(guarded && loans==(unsigned)external);dirty++;}
/* PRODUCTION SCALAR */
int main(void)
{
    for(unsigned i=0;i<1000;i++)for(unsigned ext=0;ext<2;ext++) {
        external=ext;direct=true;guarded=false;
        MemTxResult result=-1;address_space_stl_internal(NULL,0,0x12345678,0,&result,DEVICE_LITTLE_ENDIAN);
        assert(!loans && result==0);guarded=false;
        assert(address_space_ldl_internal(NULL,0,0,&result,DEVICE_LITTLE_ENDIAN)==0x12345678 && !loans);
        direct=false;guarded=false;
        assert(address_space_ldl_internal(NULL,0,0,&result,DEVICE_NATIVE_ENDIAN)==0x99 && !loans && !guarded);
    }
    assert(guard_calls==4000 && dirty==2000 && mmio==2000 && read_calls==2000 && write_calls==2000);
    puts("PASS: production scalar read/write guard before direct RAM access; 2000 CPU/external pairs retain leases through access and dirty tracking, release at return, and preserve MMIO behavior");
}
