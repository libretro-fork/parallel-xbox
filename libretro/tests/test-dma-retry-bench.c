/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include "qemu/rcu_queue.h"
#define LR_DISK_REQS 8
#define unlikely(x) (x)
#define g_free free
#define g_malloc(n) malloc(n)
#define g_new(t,n) ((t *)malloc(sizeof(t)*(n)))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#include <stddef.h>
#include <errno.h>
#include <inttypes.h>
#define trace_reentrant_aio(...) ((void)0)
#define LR_DSP_EXTERNAL_GUARD ((void)0)
#define LR_DSP_FENCE_POLL 1
static void lr_dsp_ownership_fence(unsigned reason){(void)reason;}
typedef struct AioContext AioContext;
typedef struct QEMUBH QEMUBH;
typedef void QEMUBHFunc(void *);
typedef struct {bool engaged_in_io;} MemReentrancyGuard;
/* BH TYPES */
typedef QSLIST_RCU_HEAD(,QEMUBH) BHList;
typedef struct BHListSlice {BHList bh_list;QSIMPLEQ_ENTRY(BHListSlice) next;} BHListSlice;
struct AioContext {BHList bh_list;QSIMPLEQ_HEAD(,BHListSlice) bh_slice_list;retro_atomic_int_t notify_me,notified;int notifier;};
static AioContext context;
static retro_eventcount_t wake_ec;
typedef struct {retro_atomic_int_t exit_request;struct {struct {struct {retro_atomic_u16_t low,high;} u16;} icount_decr;} neg;} CPUState;
static CPUState cpu,*first_cpu=&cpu;
/* CPU FUNCTIONS */
static void main_notify(void){lr_vcpu_exit(first_cpu);}
static void (*aio_main_notify_hook)(void)=main_notify;
static AioContext *qemu_get_aio_context(void){return &context;}
static void qemu_main_loop_wake(void){retro_eventcount_notify(&wake_ec);}
static void event_notifier_set(int *n){(void)n;assert(!"unexpected kernel notifier in running-owner benchmark");}
static volatile int deterministic=1;
static bool icount_enabled(void){return deterministic;}
static void icount_notify_exit(void){lr_vcpu_exit(first_cpu);}
static void qemu_bh_delete(QEMUBH *);
/* BH FUNCTIONS */

#include <rthreads/rthreads.h>
#define qemu_bh_new(cb,arg) aio_bh_new_full(&context,cb,arg,#cb,NULL)
/* CLIENT */
typedef struct AddressSpace {
 uintptr_t map_client_owner;retro_atomic_ptr_t map_client_bh;
 size_t max_bounce_buffer_size;retro_atomic_64_t bounce_buffer_size;
 QLIST_HEAD(,AddressSpaceMapClient) map_client_list;
} AddressSpace;
/* RETRY FUNCTIONS */
static AddressSpace as;
static unsigned calls;
typedef struct {
#ifdef LEGACY
 QEMUBH *bh;
#else
 AddressSpaceMapClient node;
#endif
} Retry;
static void retry(void *opaque){
#ifdef LEGACY
 Retry *r=opaque;qemu_bh_delete(r->bh);
#else
 (void)opaque;
#endif
 calls++;
}
static void register_retry(Retry *r){
#ifdef LEGACY
 r->bh=qemu_bh_new(retry,r);address_space_register_map_client(&as,r->bh);
#else
 address_space_register_map_client(&as,&r->node,retry,r);
#endif
}
static void poll_all(void){
 for(unsigned n=0;!QSLIST_EMPTY_RCU(&context.bh_list);n++){
  assert(n<10);aio_notify_accept(&context);aio_bh_poll(&context);
 }
}
#ifdef _WIN32
#include <windows.h>
static double now(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
int main(void){
 assert(retro_eventcount_init(&wake_ec));QSLIST_INIT_RCU(&context.bh_list);QSIMPLEQ_INIT(&context.bh_slice_list);
 as.map_client_owner=sthread_get_current_thread_id();as.max_bounce_buffer_size=4;
 Retry r[32]={0};register_retry(&r[0]);poll_all();assert(calls==1); /* warm lazy notifier */
 for(unsigned mode=0;mode<6;mode++){
  unsigned batch=mode%3==0?1:mode%3==1?8:32,loops=100000;
  calls=0;double start=now();
  for(unsigned n=0;n<loops;n++){
   retro_atomic_store_relaxed_64(&as.bounce_buffer_size,mode<3?4:0);
   for(unsigned i=0;i<batch;i++)register_retry(&r[i]);
   if(mode<3){retro_atomic_store_relaxed_64(&as.bounce_buffer_size,0);address_space_notify_map_clients(&as);}
   poll_all();
  }
  assert(calls==loops*batch&&QLIST_EMPTY(&as.map_client_list));
  printf("mode=%u ns=%.3f callbacks=%u\n",mode,(now()-start)/loops,calls);
 }
 QEMUBH *notify=retro_atomic_exchange_ptr(&as.map_client_bh,NULL);if(notify)qemu_bh_delete(notify);poll_all();
 retro_eventcount_free(&wake_ec);return 0;
}
