#include "block/libretro-disk-provider.h"
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
/* BH FUNCTIONS */
typedef struct BlockBackend BlockBackend;
typedef struct {size_t aiocb_size;} AIOCBInfo;
typedef struct {const AIOCBInfo *aiocb_info;void (*cb)(void *,int);void *opaque;int refcnt;} BlockAIOCB;
typedef void BlockCompletionFunc(void *,int);
/* REQUEST TYPE */
struct BlockBackend {int in_flight;LRDiskReq req[LR_DISK_REQS];QTAILQ_ENTRY(BlockBackend) link;};
static QTAILQ_HEAD(,BlockBackend) lr_disks=QTAILQ_HEAD_INITIALIZER(lr_disks);
static int lr_disk_pending;
#ifdef HAVE_DISK_PROVIDER
static int lr_disk_waiting;
#endif
static const char *blk_name(BlockBackend *b){(void)b;return "bench";}
#define error_report(...) fprintf(stderr,__VA_ARGS__)
/* REQUEST FUNCTIONS */
static BlockBackend drives[3];
static QEMUBH bhs[3][LR_DISK_REQS];
static volatile unsigned completed;
static void done(void *opaque,int ret){(void)opaque;assert(ret==7);completed++;}
static void __attribute__((noinline)) submit(BlockBackend *b){lr_disk_submit(b,7,done,NULL);}
static void __attribute__((noinline)) poll_owner(void){
#ifndef LEGACY
 if(lr_disk_pending)lr_disk_poll();
#endif
 aio_notify_accept(&context);aio_bh_poll(&context);
}
#ifdef _WIN32
#include <windows.h>
static double now(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
int main(void){
 assert(retro_eventcount_init(&wake_ec));QSLIST_INIT_RCU(&context.bh_list);QSIMPLEQ_INIT(&context.bh_slice_list);
 for(unsigned d=0;d<3;d++){
  QTAILQ_INSERT_TAIL(&lr_disks,&drives[d],link);
  for(unsigned i=0;i<LR_DISK_REQS;i++){
   drives[d].req[i].blk=&drives[d];
   bhs[d][i].ctx=&context;bhs[d][i].cb=lr_disk_req_done;bhs[d][i].opaque=&drives[d].req[i];
#ifdef LEGACY
   drives[d].req[i].bh=&bhs[d][i];
#endif
  }
 }
 for(unsigned mode=0;mode<5;mode++){
  unsigned drive_count=mode==4?3:1,batch=mode==0?0:mode==1?1:mode==2?4:8,loops=200000;
  completed=0;double start=now();
  for(unsigned n=0;n<loops;n++){for(unsigned d=0;d<drive_count;d++)for(unsigned i=0;i<batch;i++)submit(&drives[d]);poll_owner();}
  assert(completed==loops*drive_count*batch && !lr_disk_pending);
  printf("mode=%u ns=%.3f callbacks=%u\n",mode,(now()-start)/loops,completed);
 }
 retro_eventcount_free(&wake_ec);
}
