/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define QEMU_BUILD_BUG_ON(expr) _Static_assert(!(expr), #expr)
#include "exec/tlb-flags.h"
#define TARGET_PAGE_MASK (~(uintptr_t)4095)
#define MMU_DATA_STORE 1
#define NB_MMU_MODES 8
#define CPU_VTLB_SIZE 4
#define ctz32(x) __builtin_ctz(x)
#define qatomic_set(p,v) (*(p)=(v))
typedef uint32_t MMUIdxMap;
typedef struct {uintptr_t addr_write,addend;} CPUTLBEntry;
typedef struct {unsigned slow_flags[3];uint8_t padding[80];} CPUTLBEntryFull;
typedef struct {CPUTLBEntry table[16];unsigned n;} CPUTLBDescFast;
typedef struct {CPUTLBEntryFull fulltlb[16],vfulltlb[4];CPUTLBEntry vtable[4];} CPUTLBDesc;
typedef struct {struct {struct {struct {unsigned lock;MMUIdxMap dirty;}c;CPUTLBDesc d[8];}tlb;}neg;CPUTLBDescFast fast[8];} CPUState;
static void qemu_spin_lock(unsigned*p){assert(*p==0);*p=1;}
static void qemu_spin_unlock(unsigned*p){assert(*p==1);*p=0;}
static CPUTLBDescFast*cpu_tlb_fast(CPUState*c,int i){assert(c->neg.tlb.c.lock);return &c->fast[i];}
static unsigned tlb_n_entries(CPUTLBDescFast*f){return f->n;}
/* PRODUCTION */
static uint64_t seed=42;
static uintptr_t rnd(void){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return (uintptr_t)seed;}
static uintptr_t oracle(CPUTLBEntry e,CPUTLBEntryFull f,uintptr_t start,uintptr_t len)
{unsigned mask=TLB_INVALID_MASK|TLB_MMIO|TLB_DISCARD_WRITE|TLB_NOTDIRTY;return !((e.addr_write|f.slow_flags[MMU_DATA_STORE])&mask)&&(((e.addr_write&TARGET_PAGE_MASK)+e.addend-start)<len)?e.addr_write|TLB_NOTDIRTY:e.addr_write;}
int main(void)
{
 unsigned count=0;
 for(unsigned trial=0;trial<1000000;trial++){
  CPUTLBEntry e={rnd(),rnd()};CPUTLBEntryFull f={{0,(unsigned)rnd()&511,0},{0}};
  if(trial&1)e.addr_write&=~(uintptr_t)511;
  uintptr_t host=(e.addr_write&TARGET_PAGE_MASK)+e.addend;
  uintptr_t start=trial%3?host-(trial&4095):rnd(),len=trial%5?4096:rnd();
  uintptr_t expected=oracle(e,f,start,len);tlb_reset_dirty_range_locked(&f,&e,start,len);assert(e.addr_write==expected);count++;
 }
 for(unsigned trial=0;trial<4096;trial++){
  CPUState cpu={0},before;cpu.neg.tlb.c.dirty=trial&255;
  uintptr_t start=(rnd()&0xfffff)&TARGET_PAGE_MASK,len=trial%7?4096*(1+(trial%8)):0;
  for(unsigned m=0;m<8;m++){
   cpu.fast[m].n=1+(trial%16);
   for(unsigned k=0;k<20;k++){
    CPUTLBEntry*e=k<16?&cpu.fast[m].table[k]:&cpu.neg.tlb.d[m].vtable[k-16];
    CPUTLBEntryFull*f=k<16?&cpu.neg.tlb.d[m].fulltlb[k]:&cpu.neg.tlb.d[m].vfulltlb[k-16];
    e->addr_write=(rnd()&0xfffff000)|(rnd()&511);e->addend=(uintptr_t)(start+4096*(k%5))-(e->addr_write&TARGET_PAGE_MASK);
    f->slow_flags[MMU_DATA_STORE]=rnd()&31;
    if(!(cpu.neg.tlb.c.dirty&(1u<<m)))e->addr_write=UINTPTR_MAX;
   }
  }
  before=cpu;tlb_reset_dirty(&cpu,start,len);assert(!cpu.neg.tlb.c.lock&&cpu.neg.tlb.c.dirty==before.neg.tlb.c.dirty);
  for(unsigned m=0;m<8;m++)for(unsigned k=0;k<20;k++){
   CPUTLBEntry *e=k<16?&cpu.fast[m].table[k]:&cpu.neg.tlb.d[m].vtable[k-16];
   CPUTLBEntry old=k<16?before.fast[m].table[k]:before.neg.tlb.d[m].vtable[k-16];
   CPUTLBEntryFull f=k<16?before.neg.tlb.d[m].fulltlb[k]:before.neg.tlb.d[m].vfulltlb[k-16];
   uintptr_t expected=k<16&&k>=cpu.fast[m].n?old.addr_write:oracle(old,f,start,len);
   assert(e->addr_write==expected&&e->addend==old.addend);count++;
  }
 }
 printf("PASS: %u reprotection cases against full-scan oracle: fast/slow flags, aliases, wrapped ranges, active MMU modes, table/victim entries and empty ranges\n",count);
}
