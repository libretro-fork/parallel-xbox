/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <retro_mpsc.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include "qemu/queue.h"
#include "qemu/rcu_queue.h"
typedef uint64_t hwaddr,ram_addr_t,vaddr;
#define RAM_ADDR_INVALID UINT64_MAX
#define BP_MEM_READ 1
#define BP_MEM_WRITE 2
#define LR_DSP_EXTERNAL_GUARD ((void)0)
typedef struct {uint64_t base;} MemoryRegion;
typedef void (*MemAccessCallbackFunc)(void*,MemoryRegion*,hwaddr,hwaddr,bool);
struct rcu_head;
typedef void RCUCBFunc(struct rcu_head *);
/* CALLBACK TYPE */
typedef struct {QTAILQ_RCU_HEAD(,MemAccessCallback) mem_access_callbacks;} CPUState;
typedef union {void *host_ptr;} run_on_cpu_data;
static retro_atomic_int_t readers,entered,released,stop,started,calls;
static retro_eventcount_t entered_event,release_event;
static __attribute__((unused)) void test_read_cleanup(int *held){assert(*held);retro_atomic_dec_int(&readers);}
#define RCU_READ_LOCK_GUARD() int test_read_guard __attribute__((cleanup(test_read_cleanup)))=(retro_atomic_inc_int(&readers),1)
static MemAccessCallback *retired[40000];static unsigned retired_count;
static __attribute__((unused)) void test_retire(MemAccessCallback *cb){assert(retired_count<40000);retired[retired_count++]=cb;}
#define g_free_rcu(cb,field) test_retire(cb)
static uint64_t memory_region_get_ram_addr(MemoryRegion *mr){return mr->base;}
static void await_value(retro_atomic_int_t *flag,int value,retro_eventcount_t *event) {
 while(retro_atomic_load_acquire_int(flag)<value) {
  int key=retro_eventcount_prepare_wait(event);
  if(retro_atomic_load_acquire_int(flag)>=value)retro_eventcount_cancel_wait(event);
  else retro_eventcount_commit_wait(event,key);
 }
}
typedef struct {MemAccessCallback cb;uint64_t magic;bool hold;} Node;
static void record(void *arg,MemoryRegion *mr,hwaddr addr,hwaddr len,bool write) {
 Node *n=arg;assert(n->magic==UINT64_C(0x17812389af12bc90));assert(retro_atomic_load_acquire_int(&readers)>0);
 assert(addr>=16&&len&&addr+len<=48);(void)mr;(void)write;
 if(n->hold) {
  retro_atomic_store_release_int(&entered,1);retro_eventcount_notify(&entered_event);
  await_value(&released,1,&release_event);assert(n->magic==UINT64_C(0x17812389af12bc90));
 }
 retro_atomic_inc_int(&calls);
}
static bool lr_dsp_ownership_match(MemoryRegion *mr,hwaddr a,hwaddr n){(void)mr;(void)a;(void)n;return true;}
static void lr_dsp_ownership_callback(void *arg,MemoryRegion *mr,hwaddr a,hwaddr n,bool w){record(arg,mr,a,n,w);}
/* PRODUCTION */
static CPUState cpu;static MemoryRegion memory;
static Node *node(bool hold) {
 Node *n=calloc(1,sizeof(*n));assert(n);n->magic=UINT64_C(0x17812389af12bc90);n->hold=hold;
 n->cb.mr=&memory;n->cb.addr=16;n->cb.len=32;n->cb.func=record;n->cb.opaque=n;
 retro_atomic_int_init(&n->cb.watch_flags,3);return n;
}
static void held_reader(void *arg){(void)arg;mem_check_access_callback_ramaddr(&cpu,0,24,BP_MEM_READ);}
static void churn_reader(void *arg) {
 uintptr_t id=(uintptr_t)arg;retro_atomic_inc_int(&started);retro_eventcount_notify(&entered_event);
 while(!retro_atomic_load_acquire_int(&stop)) {
  int flags=mem_access_callback_address_matches(&cpu,0,64);assert(flags>=0&&flags<=3);
  mem_check_access_callback_ramaddr(&cpu,8+id,32,BP_MEM_READ);
 }
}
int main(void) {
 assert(offsetof(MemAccessCallback,rcu)==0);
 assert(offsetof(MemAccessCallback,entry.tqe_next)+sizeof(void*)<=64);
 assert(offsetof(MemAccessCallback,len)+sizeof(hwaddr)<=64);
 assert(offsetof(MemAccessCallback,func)+sizeof(MemAccessCallbackFunc)<=64);QTAILQ_INIT_RCU(&cpu.mem_access_callbacks);
 assert(retro_eventcount_init(&entered_event)&&retro_eventcount_init(&release_event));
 Node *first=node(true);do_mem_access_callback_insert(&cpu,(run_on_cpu_data){.host_ptr=&first->cb});
 sthread_t *reader=sthread_create(held_reader,NULL);assert(reader);await_value(&entered,1,&entered_event);
 do_mem_access_callback_remove_by_ref(&cpu,(run_on_cpu_data){.host_ptr=&first->cb});
 assert(retired_count==1&&retro_atomic_load_acquire_int(&readers)>0&&first->magic==UINT64_C(0x17812389af12bc90));
 assert(QTAILQ_EMPTY_RCU(&cpu.mem_access_callbacks));
 /* Neither unlink nor a disjoint DMA walk waits for the held reader. */
 assert(!mem_access_callback_address_matches(&cpu,0,64));
 retro_atomic_store_release_int(&released,1);retro_eventcount_notify(&release_event);
 sthread_join(reader);assert(!retro_atomic_load_acquire_int(&readers));
 free(retired[0]);retired_count=0;
 sthread_t *threads[4];for(unsigned i=0;i<4;i++){threads[i]=sthread_create(churn_reader,(void*)(uintptr_t)i);assert(threads[i]);}
 await_value(&started,4,&entered_event);
 Node *current=node(false);do_mem_access_callback_insert(&cpu,(run_on_cpu_data){.host_ptr=&current->cb});
 for(unsigned i=0;i<30000;i++) {
  Node *next=node(false);do_mem_access_callback_insert(&cpu,(run_on_cpu_data){.host_ptr=&next->cb});
  retro_atomic_store_relaxed_int(&current->cb.watch_flags,i&3);
  do_mem_access_callback_remove_by_ref(&cpu,(run_on_cpu_data){.host_ptr=&current->cb});current=next;
 }
 do_mem_access_callback_remove_by_ref(&cpu,(run_on_cpu_data){.host_ptr=&current->cb});
 retro_atomic_store_release_int(&stop,1);for(unsigned i=0;i<4;i++)sthread_join(threads[i]);
 assert(!retro_atomic_load_acquire_int(&readers)&&retired_count==30001&&QTAILQ_EMPTY_RCU(&cpu.mem_access_callbacks));
 for(unsigned i=0;i<retired_count;i++)free(retired[i]);
 retro_eventcount_free(&entered_event);retro_eventcount_free(&release_event);
 printf("PASS:held callback survives unlink,30000 replacements/four concurrent readers,atomic flag changes,deferred frees and exact spans (%d calls)\n",retro_atomic_load_acquire_int(&calls));
}
