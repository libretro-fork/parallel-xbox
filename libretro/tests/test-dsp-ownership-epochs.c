/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include "hw/xbox/mcpx/apu/dsp/bus-lease.h"
#define TARGET_PAGE_SIZE 4096
#define LR_DSP_FENCE_RAM 4
typedef struct {void *ram;unsigned max_sge;} MCPXAPUState;
static uint8_t ram[16*4096];
static MCPXAPUState device={ram,0xc},*ownership_device=&device;
static retro_atomic_int_t ownership_ready,ownership_posted,ownership_done,ownership_stop,release_work;
static retro_eventcount_t ownership_work,ownership_completion;
static DSPBusLease bus_lease;
static __thread bool ownership_is_worker;
static int ownership_div;
static uint64_t table_mask[1]={1ull<<1};
static const uint64_t *ownership_readonly_pages=table_mask;
static uint32_t observed;
static unsigned jobs;
static uint64_t memory_region_get_ram_addr(void*p){assert(p==ram);return 0;}
static uint64_t memory_region_size(void*p){assert(p==ram);return sizeof(ram);}
static int ownership_page_live(uint64_t page){return page<16 && (((1ull<<1)|(1ull<<2)|(1ull<<3))&(1ull<<page));}
static void ownership_relax_if_cpu(void){}
#define rcu_register_thread() ((void)0)
#define rcu_unregister_thread() ((void)0)
#define rcu_read_lock() ((void)0)
#define rcu_read_unlock() ((void)0)
static void gpu_release_dsp_pages(void){}
static void dsp_run_ep(MCPXAPUState*d,int div){
 while(!retro_atomic_load_acquire_int(&release_work)){}
 uint32_t p[2];memcpy(p,ram+0x3000,sizeof(p));assert(p[0]/4096<=d->max_sge);assert(p[1]==(uint32_t)div);observed=p[1];
 if(div&1){p[1]^=0xa5000000u;memcpy(ram+0x3000,p,sizeof(p));}
 jobs++;
}
/* PRODUCTION WAIT */
/* PRODUCTION ACCESS */
static void qemu_thread_run_atexit(void) {}
/* PRODUCTION WORKER */
static void worker_start(void*p){ownership_worker(p);}
int main(void){
 assert(bus_lease_init(&bus_lease));assert(retro_eventcount_init(&ownership_work));assert(retro_eventcount_init(&ownership_completion));
 retro_atomic_int_init(&ownership_ready,1);retro_atomic_int_init(&ownership_posted,0);retro_atomic_int_init(&ownership_done,0);retro_atomic_int_init(&ownership_stop,0);retro_atomic_int_init(&release_work,0);
 sthread_t*t=sthread_create(worker_start,&device);assert(t);
 for(unsigned phase=0;phase<2;phase++)for(unsigned i=1;i<=10000;i++){
  assert(retro_atomic_load_acquire_int(&ownership_posted)==retro_atomic_load_acquire_int(&ownership_done));
  uint32_t p[2]={0x520,i};memcpy(ram+0x3000,p,sizeof(p));device.max_sge=0xc;ownership_div=(int)i;observed=0;
  retro_atomic_store_release_int(&release_work,0);assert(bus_lease_try_dsp(&bus_lease));
  uint32_t stamp=(phase?UINT32_MAX-5000u:INT32_MAX-5000u)+i;
  retro_atomic_store_release_int(&ownership_posted,(int)stamp);retro_eventcount_notify(&ownership_work);
  lr_dsp_ownership_read(0x1000,4);lr_dsp_ownership_read(0x4000,4);
  assert(retro_atomic_load_acquire_int(&ownership_done)!=(int)stamp);
  retro_atomic_store_release_int(&release_work,1);
  if(i%3==0)lr_dsp_ownership_read(0x1fff,2);else lr_dsp_ownership_ram(0x3000,sizeof(p));
  assert(retro_atomic_load_acquire_int(&ownership_done)==(int)stamp && observed==i);
  memcpy(p,ram+0x3000,sizeof(p));assert(p[1]==((i&1)?(i^0xa5000000u):i));
  p[0]=0x12520;memcpy(ram+0x3000,p,sizeof(p));device.max_sge=0x6c;
 }
 retro_atomic_store_release_int(&ownership_stop,1);retro_eventcount_notify(&ownership_work);sthread_join(t);
 assert(jobs==20000 && retro_atomic_load_acquire_int(&bus_lease.state)==0);
 retro_eventcount_free(&ownership_work);retro_eventcount_free(&ownership_completion);retro_eventcount_free(&bus_lease.changed);
 puts("EP epochs: 20000 delayed jobs, shared reads, write hazards, scratch parameter growth, signed/zero wrap and stop/join passed");
}
