/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real address_space_map, with bounded RAM and a pending GPU writeback. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#define XBOX 1
#define BP_MEM_WRITE 2
#define BP_MEM_READ 1
#define BOUNCE_BUFFER_MAGIC 0x1234
#define MIN(a,b) ((a)<(b)?(a):(b))
#define RCU_READ_LOCK_GUARD() ((void)0)
#define trace_address_space_map(...) ((void)0)
#define g_malloc0(n) calloc(1,n)
typedef uint64_t hwaddr;
typedef uint32_t MemTxAttrs;
typedef struct {size_t max_bounce_buffer_size;retro_atomic_64_t bounce_buffer_size;} AddressSpace;
typedef struct {int dummy;} CPUState;
typedef struct {int dummy;} FlatView;
typedef struct {void *ram_block;hwaddr base;bool direct;unsigned refs;} MemoryRegion;
typedef struct {unsigned magic;MemoryRegion *mr;hwaddr addr,len;uint8_t buffer[];} BounceBuffer;
static uint8_t ram[128];static MemoryRegion mr;static FlatView fv;static CPUState cpu;
static hwaddr translated=16,ram_limit=16,last_addr,last_len;
static unsigned calls;static int last_flags;static bool pending,fail_ptr;
static FlatView *address_space_to_flatview(AddressSpace *as){return &fv;}
static MemoryRegion *flatview_translate(FlatView*f,hwaddr a,hwaddr*x,hwaddr*l,bool w,MemTxAttrs attrs)
{*x=8;*l=MIN(*l,translated);return &mr;}
static bool memory_access_is_direct(MemoryRegion*m,bool w,MemTxAttrs attrs){return m->direct;}
static void memory_region_ref(MemoryRegion*m){m->refs++;}
static hwaddr flatview_extend_translation(FlatView*f,hwaddr a,hwaddr target,MemoryRegion*m,hwaddr x,hwaddr l,bool w,MemTxAttrs attrs)
{return MIN(target,translated);}
static void fuzz_dma_read_cb(hwaddr a,hwaddr l,MemoryRegion*m){}
static void *qemu_ram_ptr_length(void*block,hwaddr x,hwaddr*l,bool lock,bool write)
{*l=MIN(*l,ram_limit);assert(x+*l<=sizeof(ram));return fail_ptr?NULL:ram+x;}
static CPUState *qemu_get_cpu(int n){assert(n==0);return &cpu;}
static hwaddr memory_region_get_ram_addr(MemoryRegion*m){return m->base;}
static void settle(void){if(pending){memset(ram+8,0x33,16);pending=false;}}
static void mem_check_access_callback_ramaddr(CPUState*c,hwaddr a,hwaddr n,int flags)
{assert(c==&cpu);calls++;last_addr=a;last_len=n;last_flags=flags;settle();}
static void flatview_read(FlatView*f,hwaddr a,MemTxAttrs attrs,void*out,hwaddr n)
{memset(out,0x44,n);}
/* PRODUCTION MAP */
static void reset(void){memset(ram,0,sizeof(ram));mr=(MemoryRegion){.base=0x1000,.direct=true};calls=0;pending=true;fail_ptr=false;translated=16;ram_limit=16;}
int main(void)
{
    AddressSpace as={.max_bounce_buffer_size=64};hwaddr n;uint8_t *p;
    reset();n=32;p=address_space_map(&as,0x2000,&n,true,0);
    assert(p==ram+8&&n==16);
    memset(p,0x77,n);settle();for(unsigned i=0;i<n;i++)assert(p[i]==0x77);
    assert(calls==1&&last_addr==0x1008&&last_len==16&&last_flags==BP_MEM_WRITE);
    reset();n=16;p=address_space_map(&as,0x2000,&n,false,0);
    assert(calls==1&&last_flags==BP_MEM_READ);for(unsigned i=0;i<n;i++)assert(p[i]==0x33);
    reset();ram_limit=7;n=32;p=address_space_map(&as,0x2000,&n,true,0);assert(p&&n==7&&last_len==7);
    reset();fail_ptr=true;n=16;p=address_space_map(&as,0x2000,&n,true,0);assert(!p&&calls==0&&pending);
    reset();n=0;p=address_space_map(&as,0x2000,&n,true,0);assert(!p&&calls==0);
    reset();mr.direct=false;n=16;p=address_space_map(&as,0x2000,&n,true,0);assert(p&&calls==0);free(p - offsetof(BounceBuffer,buffer));
    puts("PASS: mapped DMA settles pending GPU writes before lending RAM, preserves new DMA data, reads current GPU data, clips callback ranges, and leaves failed/empty/bounce maps alone");
}
