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
#define MAX_IDE_DEVS 2
#define unlikely(x) (x)
#define g_free free
#define g_malloc(n) malloc(n)
#define g_new(t,n) ((t *)allocate(sizeof(t)*(n)))
static unsigned allocations;
static void *allocate(size_t n){allocations++;void *p=malloc(n);assert(p);return p;}
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


#define qemu_bh_new(cb,arg) aio_bh_new_full(&context,cb,arg,#cb,NULL)
/* FLAGS */
typedef struct IDEBus IDEBus;
typedef struct IDEState IDEState;
typedef struct IDEDMA IDEDMA;
struct IDEDMA {struct {void (*restart)(IDEDMA *);} *ops;};
struct IDEState {bool atapi_pio_pending;void *pio_aiocb;IDEBus *bus;void (*end_transfer_func)(IDEState *);};
struct IDEBus {QEMUBH *bh;int error_status;IDEDMA *dma;IDEState s;IDEState ifs[2];};
typedef int RunState;
enum {IDE_DMA_READ,IDE_DMA_WRITE,IDE_DMA_TRIM};
static unsigned calls;static int operation;static bool fail_again;
static IDEBus *current;
static IDEState *ide_bus_active_if(IDEBus *b){return &b->s;}
static void operation_done(int op){assert(current&&!current->bh&&!current->error_status);calls++;operation=op;if(fail_again)current->error_status=IDE_RETRY_FLUSH;}
static void ide_atapi_cmd(IDEState *s){(void)s;}
static void ide_restart_dma(IDEState *s,int op){(void)s;operation_done(op);}
static void ide_sector_read(IDEState *s){(void)s;operation_done(3);}
static void ide_sector_write(IDEState *s){(void)s;operation_done(4);}
static void ide_flush_cache(IDEState *s){(void)s;operation_done(5);}
static void ide_atapi_dma_restart(IDEState *s){(void)s;operation_done(6);}
static void restart_hba(IDEDMA *d){(void)d;operation_done(7);}
static unsigned pio_resumes;
static void ide_atapi_pio_resume(IDEState *s){
 assert(s->atapi_pio_pending && !s->pio_aiocb);s->pio_aiocb=s;pio_resumes++;
}
/* RESTART FUNCTIONS */
static void (*volatile resume_cb)(void *,bool,RunState)=ide_restart_cb;
static void poll_all(void){unsigned n=0;do{assert(n++<8);aio_notify_accept(&context);aio_bh_poll(&context);}while(!QSLIST_EMPTY_RCU(&context.bh_list));}
#ifdef _WIN32
#include <windows.h>
static double now(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
int main(int argc,char **argv){
 assert(retro_eventcount_init(&wake_ec));QSLIST_INIT_RCU(&context.bh_list);QSIMPLEQ_INIT(&context.bh_slice_list);
 typeof(*((IDEDMA *)0)->ops) ops={restart_hba};IDEDMA dma={&ops};IDEBus b={.dma=&dma};b.s.bus=&b;b.s.end_transfer_func=ide_atapi_cmd;current=&b;
 b.ifs[0].atapi_pio_pending=b.ifs[1].atapi_pio_pending=true;b.ifs[1].pio_aiocb=&b;
 resume_cb(&b,false,0);assert(!pio_resumes && !b.ifs[0].pio_aiocb);
 resume_cb(&b,true,0);assert(pio_resumes==1 && b.ifs[0].pio_aiocb && !b.bh);
 resume_cb(&b,true,0);assert(pio_resumes==1);
 b.ifs[0].atapi_pio_pending=b.ifs[1].atapi_pio_pending=false;
 b.ifs[0].pio_aiocb=b.ifs[1].pio_aiocb=NULL;
 if(argc>1&&!strcmp(argv[1],"--bench")){
  for(unsigned mode=0;mode<3;mode++){
   unsigned loops=500000;calls=allocations=0;double start=now();
   for(unsigned n=0;n<loops;n++){b.error_status=mode==2?IDE_RETRY_FLUSH:0;resume_cb(&b,mode!=1,0);poll_all();}
   assert(calls==(mode==2?loops:0));
   printf("mode=%u ns=%.3f allocations=%u callbacks=%u\n",mode,(now()-start)/loops,allocations,calls);
  }
 }else{
  const int status[]={IDE_RETRY_DMA|IDE_RETRY_READ,IDE_RETRY_DMA,IDE_RETRY_DMA|IDE_RETRY_TRIM,IDE_RETRY_PIO|IDE_RETRY_READ,IDE_RETRY_PIO,IDE_RETRY_FLUSH,IDE_RETRY_ATAPI,IDE_RETRY_HBA};
  for(unsigned n=0;n<8;n++)for(unsigned repeat=0;repeat<100;repeat++){
   b.error_status=status[n];calls=0;unsigned before=allocations;ide_restart_cb(&b,false,0);assert(!calls&&!b.bh&&allocations==before);
   ide_restart_cb(&b,true,0);assert(!calls&&b.bh&&allocations==before+1);ide_restart_cb(&b,true,0);assert(allocations==before+1);
   poll_all();assert(calls==1&&operation==(int)n&&!b.error_status&&!b.bh);
  }
  unsigned before=allocations;ide_restart_cb(&b,true,0);
#ifndef LEGACY
  assert(allocations==before&&!b.bh);
#else
  (void)before;
#endif
  poll_all();
  b.error_status=IDE_RETRY_FLUSH;fail_again=true;ide_restart_cb(&b,true,0);poll_all();assert(b.error_status==IDE_RETRY_FLUSH);fail_again=false;ide_restart_cb(&b,true,0);poll_all();assert(!b.error_status);
  b.error_status=IDE_RETRY_FLUSH;calls=0;ide_restart_cb(&b,true,0);b.error_status=0;poll_all();assert(!calls&&!b.bh);
  puts("IDE restart: idle resume, stopped VM, eight retry paths, coalescing, clear-before-retry and repeated errors PASS");
 }
 retro_eventcount_free(&wake_ec);return 0;
}
