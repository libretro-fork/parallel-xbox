/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define QEMU_BUILD_BUG_ON(e) _Static_assert(!(e),#e)
#include "exec/tlb-flags.h"
#define MAX(a,b) ((a)>(b)?(a):(b))
#define NB_MMU_MODES 2
#define CPU_VTLB_SIZE 2
#define TARGET_PAGE_SIZE 4096
#define TARGET_PAGE_MASK (~UINT64_C(4095))
#define BP_MEM_READ 1
#define BP_MEM_WRITE 2
typedef uint64_t vaddr;typedef uint64_t ram_addr_t;
typedef enum {MMU_DATA_LOAD,MMU_DATA_STORE,MMU_INST_FETCH} MMUAccessType;
typedef union {struct {uintptr_t addr_read,addr_write,addr_code;};uintptr_t addr_idx[3];} CPUTLBEntry;
typedef struct {uint64_t xlat_section;int slow_flags[3];} CPUTLBEntryFull;
typedef struct {CPUTLBEntry *table;size_t mask;} CPUTLBDescFast;
typedef struct {CPUTLBEntryFull *fulltlb;CPUTLBEntry vtable[2];CPUTLBEntryFull vfulltlb[2];} Desc;
typedef struct {struct {struct {struct {int lock;} c;Desc d[2];}tlb;}neg;CPUTLBDescFast fast[2];} CPUState;
static CPUState cpu;static CPUTLBEntry primary[2][4];static CPUTLBEntryFull full[2][4];
static unsigned flushes;static int guest_wp,other_wp;
static bool watched[256];
static bool lr_dsp_tlb_watched(uint64_t ram){return ram/4096<256 && watched[ram/4096];}
static void assert_cpu_is_self(CPUState*c){assert(c==&cpu);}
static void qemu_spin_lock(int*p){}static void qemu_spin_unlock(int*p){}
static size_t tlb_n_entries(CPUTLBDescFast*f){return f->mask+1;}
static CPUTLBDescFast *cpu_tlb_fast(CPUState*c,int mode){assert(mode>=0&&mode<2);return &c->fast[mode];}
static int cpu_watchpoint_address_matches(CPUState*c,vaddr p,size_t n){return guest_wp;}
static int mem_access_callback_address_matches(CPUState*c,ram_addr_t p,size_t n){return other_wp;}
static void tlb_flush(CPUState*c){flushes++;memset(primary,-1,sizeof(primary));for(unsigned m=0;m<2;m++)memset(c->neg.tlb.d[m].vtable,-1,sizeof(c->neg.tlb.d[m].vtable));}
static uint64_t tlb_read_idx(const CPUTLBEntry*e,unsigned access){return e->addr_idx[access];}
static void copy_tlb_helper_locked(CPUTLBEntry*a,const CPUTLBEntry*b){*a=*b;}
static void tlb_dirty_index_mark(Desc*d,CPUTLBDescFast*f,size_t index){}

/* PRODUCTION TLB */
static void fill(CPUTLBEntry*e,CPUTLBEntryFull*f,uint64_t page,uint64_t ram)
{
    memset(f,0,sizeof(*f));f->xlat_section=ram-page;
    for(unsigned a=0;a<3;a++)e->addr_idx[a]=page;
}

int main(void)
{
    memset(primary,-1,sizeof(primary));watched[2]=true;
    for(unsigned m=0;m<2;m++){cpu.fast[m].table=primary[m];cpu.fast[m].mask=3;cpu.neg.tlb.d[m].fulltlb=full[m];memset(cpu.neg.tlb.d[m].vtable,-1,sizeof(cpu.neg.tlb.d[m].vtable));}
    fill(&primary[0][0],&full[0][0],0x1000,0x2000);dsp_tlb_record(&cpu,0,0,0x1000,0x2000);
    fill(&primary[0][3],&full[0][3],0x3000,0x2000);dsp_tlb_record(&cpu,0,3,0x3000,0x2000);
    fill(&cpu.neg.tlb.d[1].vtable[0],&cpu.neg.tlb.d[1].vfulltlb[0],0x4000,0x2000);
    primary[0][0].addr_write|=TLB_NOTDIRTY;
    for(unsigned n=0;n<2000;n++) {
        lr_dsp_tlb_refresh(&cpu,true);
        assert(primary[0][0].addr_read&TLB_FORCE_SLOW);assert(full[0][3].slow_flags[1]&TLB_WATCHPOINT);
        assert(cpu.neg.tlb.d[1].vfulltlb[0].slow_flags[0]&TLB_WATCHPOINT);
        assert(primary[0][0].addr_code==0x1000);
        lr_dsp_tlb_refresh(&cpu,false);
        assert(primary[0][0].addr_read==0x1000);
        assert(primary[0][0].addr_write==(0x1000|TLB_NOTDIRTY));
        assert(cpu.neg.tlb.d[1].vtable[0].addr_read==0x4000);
    }
    guest_wp=BP_MEM_READ;other_wp=BP_MEM_WRITE;lr_dsp_tlb_refresh(&cpu,true);lr_dsp_tlb_refresh(&cpu,false);
    assert(full[0][0].slow_flags[0]&TLB_WATCHPOINT);assert(full[0][0].slow_flags[1]&TLB_WATCHPOINT);
    guest_wp=other_wp=0;full[0][0].slow_flags[0]|=TLB_BSWAP;
    lr_dsp_tlb_refresh(&cpu,false);assert(full[0][0].slow_flags[0]==TLB_BSWAP);assert(primary[0][0].addr_read&TLB_FORCE_SLOW);
    /* Same slot/virtual address remapped to a different physical page. */
    fill(&primary[0][0],&full[0][0],0x1000,0x5000);lr_dsp_tlb_refresh(&cpu,true);
    assert(primary[0][0].addr_read==0x1000 && dsp_tlb_count==1);
    /* A resize makes old slot indexes invalid; never follow freed pointers. */
    cpu.fast[0].mask=0;lr_dsp_tlb_refresh(&cpu,true);assert(dsp_tlb_count==0);
    cpu.fast[0].mask=3;
    fill(&primary[0][1],&full[0][1],0x7000,0x7000);
    fill(&cpu.neg.tlb.d[0].vtable[0],&cpu.neg.tlb.d[0].vfulltlb[0],0x6000,0x2000);
    assert(victim_tlb_hit(&cpu,0,1,MMU_DATA_LOAD,0x6000));
    lr_dsp_tlb_refresh(&cpu,true);assert(primary[0][1].addr_read&TLB_FORCE_SLOW);
    lr_dsp_tlb_refresh(&cpu,false);assert(primary[0][1].addr_read==0x6000);
    for(unsigned i=0;i<=DSP_TLB_SLOTS;i++)dsp_tlb_record(&cpu,0,i,0x8000,0x2000);
    assert(dsp_tlb_overflow);
    lr_dsp_tlb_refresh(&cpu,true);assert(flushes==1 && !dsp_tlb_overflow && !dsp_tlb_count);
    puts("PASS: production sparse TLB guard arm/relax across 2000 epochs, virtual aliases, victim entries, dirty bits, guest/GPU watches, byte swapping, physical remap, resize and overflow fallback");
}
