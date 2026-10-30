/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#define TARGET_PAGE_BITS 12
#define QEMU_BUILD_BUG_ON(expr) _Static_assert(!(expr), #expr)
#include "exec/tlb-flags.h"
#define TARGET_PAGE_MASK (~(uintptr_t)4095)
#define MMU_DATA_STORE 1
#define NB_MMU_MODES 8
#define CPU_VTLB_SIZE 4
#define ctz32(x) __builtin_ctz(x)
typedef uintptr_t retro_atomic_size_t;
#define retro_atomic_store_relaxed_size(p,v) (*(p)=(v))
typedef uint32_t MMUIdxMap;
typedef uintptr_t vaddr;
typedef struct {uintptr_t addr_write,addend;} CPUTLBEntry;
typedef struct {unsigned slow_flags[3];uint8_t padding[80];} CPUTLBEntryFull;
typedef struct {CPUTLBEntry table[256];unsigned n;} CPUTLBDescFast;
typedef struct {CPUTLBEntryFull fulltlb[256],vfulltlb[4];CPUTLBEntry vtable[4];uint64_t dirty_write_tags[64];} CPUTLBDesc;
typedef struct {struct {struct {struct {unsigned lock;MMUIdxMap dirty;}c;CPUTLBDesc d[8];}tlb;}neg;CPUTLBDescFast fast[8];} CPUState;
static void qemu_spin_lock(unsigned*p){assert(*p==0);*p=1;}
static void qemu_spin_unlock(unsigned*p){assert(*p==1);*p=0;}
static CPUTLBDescFast*cpu_tlb_fast(CPUState*c,int i){return &c->fast[i];}
static unsigned tlb_n_entries(CPUTLBDescFast*f){return f->n;}
static void assert_cpu_is_self(CPUState*c){assert(c);}
static CPUTLBEntry*tlb_entry(CPUState*c,int m,vaddr addr){assert(c->neg.tlb.c.lock);return &c->fast[m].table[(addr>>12)%c->fast[m].n];}
typedef int MMUAccessType;
static uint64_t tlb_read_idx(const CPUTLBEntry*e,MMUAccessType a){assert(a==MMU_DATA_STORE);return e->addr_write;}
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
  for(unsigned m=0;m<8;m++)for(unsigned k=0;k<cpu.fast[m].n;k++)tlb_dirty_index_mark(&cpu.neg.tlb.d[m],&cpu.fast[m],k);
  before=cpu;tlb_reset_dirty(&cpu,start,len);assert(!cpu.neg.tlb.c.lock&&cpu.neg.tlb.c.dirty==before.neg.tlb.c.dirty);
  for(unsigned m=0;m<8;m++)for(unsigned k=0;k<20;k++){
   CPUTLBEntry *e=k<16?&cpu.fast[m].table[k]:&cpu.neg.tlb.d[m].vtable[k-16];
   CPUTLBEntry old=k<16?before.fast[m].table[k]:before.neg.tlb.d[m].vtable[k-16];
   CPUTLBEntryFull f=k<16?before.neg.tlb.d[m].fulltlb[k]:before.neg.tlb.d[m].vfulltlb[k-16];
   uintptr_t expected=k<16&&k>=cpu.fast[m].n?old.addr_write:oracle(old,f,start,len);
   assert(e->addr_write==expected&&e->addend==old.addend);count++;
  }
 }
 /* Promotion must update primary and victim aliases in every populated
  * mode, and preserve other flags and other virtual pages exactly. */
 for(unsigned trial=0;trial<10000;trial++){
  CPUState cpu={0},expected;cpu.neg.tlb.c.dirty=trial&255;
  uintptr_t addr=rnd()&TARGET_PAGE_MASK;
  for(unsigned m=0;m<8;m++){
   cpu.fast[m].n=1u<<(trial%5);
   for(unsigned k=0;k<20;k++){
    CPUTLBEntry*e=k<16?&cpu.fast[m].table[k]:&cpu.neg.tlb.d[m].vtable[k-16];
    e->addr_write=(k%3?addr:rnd()&TARGET_PAGE_MASK)|TLB_NOTDIRTY;
    if(k%5==0)e->addr_write|=TLB_WATCHPOINT;
    if(!(cpu.neg.tlb.c.dirty&(1u<<m)))e->addr_write=UINTPTR_MAX;
    e->addend=rnd();
   }
  }
  expected=cpu;
  /* Exhaustive reference, including all inactive modes. */
  for(unsigned m=0;m<8;m++){
   unsigned index=(addr>>12)%expected.fast[m].n;
   if(expected.fast[m].table[index].addr_write==(addr|TLB_NOTDIRTY))expected.fast[m].table[index].addr_write=addr;
   for(unsigned k=0;k<4;k++)if(expected.neg.tlb.d[m].vtable[k].addr_write==(addr|TLB_NOTDIRTY))expected.neg.tlb.d[m].vtable[k].addr_write=addr;
  }
  tlb_set_dirty(&cpu,addr|(trial&4095));
  for(unsigned m=0;m<8;m++){
   assert(!memcmp(cpu.fast[m].table,expected.fast[m].table,sizeof(cpu.fast[m].table)));
   assert(!memcmp(cpu.neg.tlb.d[m].vtable,expected.neg.tlb.d[m].vtable,sizeof(cpu.neg.tlb.d[m].vtable)));
  }
 }
 /* Larger groups, stale OR tags, collisions, overwrites, aliases and
  * repeated reprotection against the exhaustive range oracle. */
 static CPUState wide, old;
 wide.neg.tlb.c.dirty=1;wide.fast[0].n=256;
 memset(wide.fast[0].table,0xff,sizeof(wide.fast[0].table));
 memset(wide.neg.tlb.d[0].vtable,0xff,sizeof(wide.neg.tlb.d[0].vtable));
 for(unsigned trial=0;trial<20000;trial++){
  unsigned i=rnd()%256;
  CPUTLBEntry*e=&wide.fast[0].table[i];
  uintptr_t host=(uintptr_t)0x10000000+(rnd()%32768)*4096;
  e->addr_write=(i+1)*4096|(rnd()&511);
  if(trial%3)e->addr_write&=~(uintptr_t)511;
  e->addend=host-(e->addr_write&TARGET_PAGE_MASK);
  wide.neg.tlb.d[0].fulltlb[i].slow_flags[MMU_DATA_STORE]=rnd()&511;
  tlb_dirty_index_mark(&wide.neg.tlb.d[0],&wide.fast[0],i);
  if(trial%17==0)wide.fast[0].table[rnd()%256].addr_write=UINTPTR_MAX;
  uintptr_t start=trial%9?host:rnd(),len=trial%11?(1+rnd()%400)*4096:rnd();
  old=wide;tlb_reset_dirty(&wide,start,len);
  for(unsigned j=0;j<256;j++){
   CPUTLBEntry prior=old.fast[0].table[j];
   assert(wide.fast[0].table[j].addr_write==oracle(prior,old.neg.tlb.d[0].fulltlb[j],start,len));
   assert(wide.fast[0].table[j].addend==prior.addend);
  }
 }
 /* A victim hit restores a mapping whose tag was not in the primary
  * index; the complete production swap must publish it under the lock. */
 memset(&wide,0,sizeof(wide));wide.neg.tlb.c.dirty=1;wide.fast[0].n=256;
 memset(wide.fast[0].table,0xff,sizeof(wide.fast[0].table));
 memset(wide.neg.tlb.d[0].vtable,0xff,sizeof(wide.neg.tlb.d[0].vtable));
 wide.neg.tlb.d[0].vtable[0]=(CPUTLBEntry){.addr_write=0x9000,.addend=0x20000000-0x9000};
 assert(victim_tlb_hit(&wide,0,9,MMU_DATA_STORE,0x9000));
 tlb_reset_dirty(&wide,0x20000000,4096);
 assert(wide.fast[0].table[9].addr_write==(0x9000|TLB_NOTDIRTY));
 /* Range tags must conservatively cover every address in a range,
  * including unsigned wrap and the host-width shift boundary. */
 for(unsigned trial=0;trial<200000;trial++){
  uintptr_t start=rnd(),len=rnd();unsigned shift=trial%(sizeof(uintptr_t)*CHAR_BIT+1);
  uint64_t tags=tlb_dirty_range_tags(start,len,shift);
  if(len){uintptr_t host=start+(rnd()%len);assert(tags&tlb_dirty_host_tag(host,shift));}
  else assert(tags==0);
 }
 puts("PASS: 20000 indexed table mutations, victim restoration and 200000 wrapped tag-range cases");
 printf("PASS: %u reprotection cases and 10000 dirty-promotion states against full-scan oracles: fast/slow flags, aliases, wrapped ranges, active MMU modes, table/victim entries and empty ranges\n",count);
}
