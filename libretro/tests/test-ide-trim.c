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
typedef struct BlockBackend BlockBackend;
typedef struct BlockAIOCB BlockAIOCB;
typedef struct {size_t aiocb_size;void (*cancel_async)(BlockAIOCB *);} AIOCBInfo;
struct BlockAIOCB {const AIOCBInfo *aiocb_info;void (*cb)(void *,int);void *opaque;int refcnt;};
typedef void BlockCompletionFunc(void *,int);
/* REQUEST TYPE */
struct BlockBackend {int in_flight;LRDiskReq req[LR_DISK_REQS];QTAILQ_ENTRY(BlockBackend) link;};
static QTAILQ_HEAD(,BlockBackend) lr_disks=QTAILQ_HEAD_INITIALIZER(lr_disks);
static int lr_disk_pending;
static const AIOCBInfo lr_disk_aiocb_info={.aiocb_size=sizeof(LRDiskReq)};
static const char *blk_name(BlockBackend *b){(void)b;return "bench";}
#define error_report(...) fprintf(stderr,__VA_ARGS__)
/* REQUEST FUNCTIONS */

#define BDRV_SECTOR_BITS 9
#define le64_to_cpu(v) (v)
typedef struct {struct iovec *iov;int niov;} QEMUIOVector;
#include <sys/types.h>
#ifndef _WIN32
#include <sys/uio.h>
#else
struct iovec {void *iov_base;size_t iov_len;};
#endif
typedef struct {MemReentrancyGuard mem_reentrancy_guard;} IDEDevice;
typedef struct {IDEDevice *master,*slave;} IDEBus;
typedef struct {BlockBackend *blk;IDEBus *bus;int unit;} IDEState;
#define DEVICE(x) (x)
static unsigned allocations,frees,discards;
static int io_error;
static void *blk_aio_get(const AIOCBInfo *info,BlockBackend *blk,BlockCompletionFunc *cb,void *opaque){
 (void)blk;BlockAIOCB *a=calloc(1,info->aiocb_size);assert(a);a->aiocb_info=info;a->cb=cb;a->opaque=opaque;allocations++;return a;
}
static void qemu_aio_unref(void *acb){frees++;free(acb);}
static bool ide_sect_range_ok(IDEState *s,uint64_t sector,uint64_t count){(void)s;return sector<=1024&&count<=1024-sector;}
static BlockAIOCB *blk_aio_pdiscard(BlockBackend *b,int64_t off,int64_t len,BlockCompletionFunc *cb,void *opaque){assert(off>=0&&len>0);discards++;return lr_disk_submit(b,io_error,cb,opaque);}
#define qemu_bh_new_guarded(cb,opaque,guard) aio_bh_new_full(&context,cb,opaque,#cb,guard)
/* TRIM FUNCTIONS */
static BlockBackend drive;
static IDEDevice dev;
static IDEBus bus={&dev,&dev};
static IDEState state={&drive,&bus,0};
static BlockAIOCB *active;
static unsigned calls;
static int expected;
static bool recurse;
static void poll_owner(void){if(lr_disk_pending)lr_disk_poll();aio_notify_accept(&context);aio_bh_poll(&context);}
static void done(void *opaque,int ret){
 assert(opaque==&state&&ret==expected&&dev.mem_reentrancy_guard.engaged_in_io);
 assert(drive.in_flight>0);calls++;
 if(recurse){recurse=false;blk_aio_cancel_async(active);poll_owner();}
}
static void drain(void){for(unsigned n=0;drive.in_flight&&n<100;n++)poll_owner();assert(!drive.in_flight&&!lr_disk_pending);poll_owner();}
static void check(uint64_t *entries,int count,int cancel_pass,int error,bool engaged){
 struct iovec iov={entries,(size_t)count*8};QEMUIOVector q={&iov,1};
 if(count<0){q.iov=NULL;q.niov=0;}
 unsigned before=discards;calls=0;io_error=error;expected=cancel_pass>=0?-ECANCELED:error;
 if(count>0&&!error&&cancel_pass<0)for(int i=0;i<count;i++)if((entries[i]>>48)&&!ide_sect_range_ok(&state,entries[i]&0xffffffffffffULL,entries[i]>>48)){expected=-EINVAL;break;}
 dev.mem_reentrancy_guard.engaged_in_io=engaged;
 active=ide_issue_trim(0,&q,done,&state,&state);assert(!calls);
 for(int n=0;n<cancel_pass;n++){assert(!calls);poll_owner();}
 if(cancel_pass>=0){assert(!calls);blk_aio_cancel_async(active);blk_aio_cancel_async(active);}
 drain();assert(calls==1&&dev.mem_reentrancy_guard.engaged_in_io==engaged&&allocations==frees);
 if(count<=0)assert(discards==before);
}
#ifdef _WIN32
#include <windows.h>
static double now(void){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return (double)t.QuadPart*1e9/f.QuadPart;}
#else
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e9+t.tv_nsec;}
#endif
int main(int argc,char **argv){
 assert(retro_eventcount_init(&wake_ec));QSLIST_INIT_RCU(&context.bh_list);QSIMPLEQ_INIT(&context.bh_slice_list);
 QTAILQ_INSERT_TAIL(&lr_disks,&drive,link);
 for(unsigned i=0;i<LR_DISK_REQS;i++){drive.req[i].blk=&drive;drive.req[i].common.aiocb_info=&lr_disk_aiocb_info;}
 uint64_t entries[32];for(unsigned i=0;i<32;i++)entries[i]=((uint64_t)1<<48)+i;
 if(argc>1&&!strcmp(argv[1],"--bench")){
  struct iovec iov={entries,0};QEMUIOVector q={&iov,1};unsigned loops=200000;
  for(unsigned mode=0;mode<3;mode++){
   iov.iov_len=mode==0?0:mode==1?8:32*8;calls=0;expected=0;
   double start=now();for(unsigned n=0;n<loops;n++){active=ide_issue_trim(0,&q,done,&state,&state);drain();}
   assert(calls==loops&&allocations==frees);printf("mode=%u ns=%.3f callbacks=%u\n",mode,(now()-start)/loops,calls);
  }
 }else{
#ifndef LEGACY
  check(NULL,-1,0,0,false);check(NULL,0,0,0,false);
#endif
  for(unsigned n=0;n<2;n++){
   check(NULL,-1,-1,0,n);check(NULL,0,-1,0,n);check(entries,32,-1,0,n);
   check(entries,32,0,0,n);check(entries,32,3,0,n);check(entries,32,-1,-EIO,n);
   check(entries,1,1,0,n);recurse=true;check(entries,32,-1,0,n);
   entries[0]=((uint64_t)1<<48)+1024;check(entries,1,-1,0,n);check(entries,1,0,0,n);entries[0]=(uint64_t)1<<48;
  }
  for(unsigned n=0;n<10000;n++)check(entries,1+(n%32),n%3==0?0:-1,0,n%2);
  puts("IDE TRIM: deferred completion, ranges/errors, cancellation, nested dispatch, guards and 10000 lifecycles PASS");
 }
 retro_eventcount_free(&wake_ec);return 0;
}
