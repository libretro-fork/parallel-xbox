/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned ctz32(uint32_t v) { assert(v);return __builtin_ctz(v); }
#include "hw/xbox/nv2a/nv2a_regs.h"
#define NV2A_DPRINTF(...) ((void)0)
#define NV2A_NUM_CHANNELS 32
#define nv2a_reg_log_write(...) ((void)0)
typedef uint64_t hwaddr;
typedef struct { uint32_t regs[0x2000/4];int pending_interrupts,enabled_interrupts,scanout_changed; } PGRAPHState;
typedef struct NV2AState {
 PGRAPHState pgraph;
 struct { int thread_running,halt,guest_kicks,guest_done,fifo_kick,fifo_ec;
          int regs[0x2000],pending_interrupts,enabled_interrupts; } pfifo;
 struct { uint32_t pending_interrupts,enabled_interrupts,start; } pcrtc;
 unsigned ramin;uint8_t ramin_ptr[4096],vram_ptr[256];
} NV2AState;
static NV2AState *g_nv2a;
static unsigned wakes;static int expect_guest;
static int retro_atomic_load_acquire_int(const int *p) {return *p;}
static int retro_atomic_load_relaxed_int(const int *p) {return *p;}
static void retro_atomic_store_release_int(int *p,int v) {*p=v;}
static void retro_atomic_store_relaxed_int(int *p,int v) {*p=v;}
static int retro_atomic_fetch_add_seq_cst_int(int *p,int v) {int old=*p;*p+=v;return old;}
static int retro_atomic_fetch_and_int(int *p,int v) {int old=*p;*p&=v;return old;}
static void retro_eventcount_notify(int *ec) {
 assert(ec==&g_nv2a->pfifo.fifo_ec && g_nv2a->pfifo.fifo_kick);
 /* Publication must precede notification, even if the worker wakes now. */
 if(expect_guest>=0)assert(g_nv2a->pfifo.guest_kicks==expect_guest);
 wakes++;
}
static uint32_t pfifo_reg_r(NV2AState *d,unsigned addr) {return (uint32_t)d->pfifo.regs[addr];}
static void pfifo_reg_w(NV2AState *d,unsigned addr,uint32_t v) {d->pfifo.regs[addr]=(int)v;}
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned addr) {return pg->regs[addr/4];}
#define PG_GET_MASK(addr,mask) GET_MASK(pgraph_reg_r(pg,addr),mask)
static void pgraph_reg_set_mask_atomic(PGRAPHState *pg,unsigned addr,uint32_t mask,uint32_t value) { SET_MASK(pg->regs[addr/4],mask,value); }
static void pgraph_reg_w_guest(PGRAPHState *pg,unsigned addr,uint32_t value) {pg->regs[addr/4]=value;}
static void pgraph_rdi_write(PGRAPHState *pg,unsigned select,unsigned address,uint64_t value) {(void)pg;(void)select;(void)address;(void)value;}
static void nv2a_profile_increment(void) {}
static size_t memory_region_size(const unsigned *m) {(void)m;return 4096;}
static uint32_t ldl_le_p(const uint32_t *p) {return *p;}
static void nv2a_update_irq(NV2AState *d) {(void)d;}
typedef struct { int unused; } CPUState;
static bool lr_quiet;
static int lr_disk_pending;
static int64_t virtual_time,lr_next_stop_vt=10000;
static unsigned skips,cpu_exits;
#define QEMU_CLOCK_VIRTUAL 0
#define QEMU_TIMER_ATTR_ALL 0
static int64_t qemu_clock_get_ns(int clock) {(void)clock;return virtual_time;}
static int64_t qemu_clock_deadline_ns_all(int clock,int attrs) {(void)clock;(void)attrs;return 1000;}
static void icount_skip_to(int64_t vt) {virtual_time=vt;skips++;}
static void lr_vcpu_exit(CPUState *cpu) {(void)cpu;cpu_exits++;}
/* PRODUCTION */
static void guest_write(NV2AState *d,unsigned kind) {
 expect_guest=d->pfifo.guest_kicks+1;unsigned old=wakes;
 switch(kind) {
 case 0:pgraph_write(d,NV_PGRAPH_INCREMENT,NV_PGRAPH_INCREMENT_READ_3D,4);break;
 case 1:pgraph_write(d,NV_PGRAPH_INTR,NV_PGRAPH_INTR_ERROR,4);break;
 case 2:pgraph_write(d,NV_PGRAPH_FIFO,NV_PGRAPH_FIFO_ACCESS,4);break;
 case 3:pcrtc_write(d,NV_PCRTC_START,0x08000080,4);assert(d->pcrtc.start==0x80);break;
 case 4:pfifo_write(d,NV_PFIFO_INTR_0,1,4);break;
 case 5:pfifo_write(d,NV_PFIFO_INTR_EN_0,1,4);break;
 case 6:pfifo_write(d,NV_PFIFO_CACHE1_DMA_PUT,0x1234,4);assert(pfifo_reg_r(d,NV_PFIFO_CACHE1_DMA_PUT)==0x1234);break;
 default: {
  unsigned channel=(unsigned)d->pfifo.guest_kicks&15;
  pfifo_reg_w(d,NV_PFIFO_MODE,1u<<channel);
  uint32_t push=0;SET_MASK(push,NV_PFIFO_CACHE1_PUSH1_CHID,channel);
  pfifo_reg_w(d,NV_PFIFO_CACHE1_PUSH1,push);
  const unsigned user_regs[]={NV_USER_DMA_PUT,NV_USER_DMA_GET,NV_USER_REF};
  const unsigned fifo_regs[]={NV_PFIFO_CACHE1_DMA_PUT,NV_PFIFO_CACHE1_DMA_GET,NV_PFIFO_CACHE1_REF};
  unsigned index=kind-7;
  user_write(d,((hwaddr)channel<<16)|user_regs[index],0x5678,4);
  assert(pfifo_reg_r(d,fifo_regs[index])==0x5678);
  break;
 }

 }
 assert(wakes==old+1 && d->pfifo.guest_kicks==expect_guest);
 assert(nv2a_libretro_guest_busy());
 CPUState cpu={0};unsigned old_skips=skips,old_exits=cpu_exits;
 int64_t old_time=virtual_time;lr_quiet=false;
 lr_quiet_exit(&cpu);
 assert(lr_quiet && skips==old_skips && virtual_time==old_time && cpu_exits==old_exits+1);
}
int main(void) {
 unsigned cases=0;
 for(unsigned order=0;order<10000;order++) {
  NV2AState d={0};g_nv2a=&d;d.pfifo.thread_running=1;virtual_time=0;
  d.pfifo.guest_kicks=d.pfifo.guest_done=(int)order*8;
  SET_MASK(d.pgraph.regs[NV_PGRAPH_SURFACE/4],NV_PGRAPH_SURFACE_MODULO_3D,3);
  d.pgraph.pending_interrupts=NV_PGRAPH_INTR_ERROR;
  assert(!nv2a_libretro_guest_busy());
  /* DOAX shape: an empty GPU queue resumes after a guest flip release.
   * It must stay busy during resumed work, including coalesced wakeups. */
  guest_write(&d,order%10);
  d.pfifo.fifo_kick=0;int seen=d.pfifo.guest_kicks;
  assert(nv2a_libretro_guest_busy());
  guest_write(&d,(order+3)%10);
  d.pfifo.guest_done=seen;assert(nv2a_libretro_guest_busy());
  seen=d.pfifo.guest_kicks;d.pfifo.fifo_kick=0;
  assert(nv2a_libretro_guest_busy());
  d.pfifo.guest_done=seen;assert(!nv2a_libretro_guest_busy());
  /* The core's redraw/reset kicks do not invent guest work. */
  expect_guest=seen;pfifo_kick(&d);
  assert(!nv2a_libretro_guest_busy() && d.pfifo.guest_kicks==seen);
  CPUState cpu={0};lr_quiet=false;unsigned old_skips=skips;
  lr_quiet_exit(&cpu);
  assert(!lr_quiet && skips==old_skips+1 && virtual_time==1000);
  unsigned before=wakes;
  pgraph_write(&d,NV_PGRAPH_INCREMENT,0,4);
  pgraph_write(&d,NV_PGRAPH_INTR_EN,1,4);
  pcrtc_write(&d,NV_PCRTC_INTR_0,1,4);
  assert(wakes==before && d.pfifo.guest_kicks==seen);
  d.pfifo.guest_kicks++;d.pfifo.halt=1;assert(!nv2a_libretro_guest_busy());
  d.pfifo.halt=0;d.pfifo.thread_running=0;assert(!nv2a_libretro_guest_busy());
  cases++;
 }
 g_nv2a=NULL;assert(!nv2a_libretro_guest_busy());
 printf("DOAX GPU progress regression: %u guest flip/ack/FIFO/scanout/DMA schedules passed; publish-before-wake, unfinished work, coalesced kicks, idle clock-skip protection and core-only wake exclusion\n",cases);
 return 0;
}
