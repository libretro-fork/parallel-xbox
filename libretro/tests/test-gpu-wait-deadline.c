/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define SCALE_MS 1000000
#define MAIN_LOOP_POLL_FILL 0
#define MAIN_LOOP_POLL_ERR 1
#define MAIN_LOOP_POLL_OK 2
#define QLIST_EMPTY(p) ((p)->empty)
typedef struct {int notified;bool fds;} AioContext;
static AioContext mainctx,ioctx,*qemu_aio_context=&mainctx;
static struct {struct {bool empty;} notifiers;} main_loop_poll_notifiers;
typedef struct {int state;uint32_t timeout;void *pollfds;} MainLoopPoll;
static int poll_storage,*gpollfds=&poll_storage,main_loop_ec;
static bool bql,progress,prepare_race,icount,opt_deterministic,gpu_busy;
static unsigned prepared,cancelled,committed,timed,polls,timers,warps,skips;
static int64_t host_now,elapsed,virtual_now,last_us,last_poll,clock_deadline;
static uint32_t socket_ms;
static AioContext *iohandler_get_aio_context(void){return &ioctx;}
static bool aio_has_fd_handlers(AioContext *a){return a->fds;}
static int retro_atomic_load_relaxed_int(int *p){return *p;}
static void aio_notify_accept(AioContext *a){a->notified=0;}
static int aio_bh_poll(AioContext *a){(void)a;bool p=progress;progress=false;return p;}
static void g_array_set_size(int *p,int n){assert(p==gpollfds && !n);}
static void notifier_list_notify(void *list,MainLoopPoll *p){assert(list==&main_loop_poll_notifiers);if(p->state==MAIN_LOOP_POLL_FILL && !main_loop_poll_notifiers.notifiers.empty && socket_ms<p->timeout)p->timeout=socket_ms;}
static int64_t qemu_soonest_timeout(int64_t a,int64_t b){return a<0?b:b<0?a:MIN(a,b);}
static int64_t qemu_clock_deadline_ns(void){return clock_deadline;}
static int os_host_main_loop_wait(int64_t ns){assert(bql);polls++;last_poll=ns;host_now+=elapsed;return 0;}
static bool icount_enabled(void){return icount;}
static void icount_start_warp_timer(void){warps++;}
static void qemu_clock_run_all_timers(void){assert(bql);timers++;}
static int retro_eventcount_prepare_wait(int *p){assert(p==&main_loop_ec && bql);prepared++;if(prepare_race)ioctx.notified=1;return 7;}
static void retro_eventcount_cancel_wait(int *p){assert(p==&main_loop_ec && bql);cancelled++;}
static void retro_eventcount_commit_wait(int *p,int key){assert(p==&main_loop_ec && key==7 && !bql);committed++;host_now+=elapsed;}
static bool retro_eventcount_commit_wait_timeout(int *p,int key,int64_t us){assert(p==&main_loop_ec && key==7 && !bql && us>=0);timed++;last_us=us;host_now+=elapsed;return false;}
static void bql_unlock(void){assert(bql);bql=false;}
static void bql_lock(void){assert(!bql);bql=true;}
bool nv2a_libretro_guest_busy(void){return gpu_busy;}
static int64_t get_clock(void){return host_now;}
static void cpu_clock_skip_to(int64_t ns){assert(ns>=virtual_now);virtual_now=ns;skips++;}
/* PRODUCTION */
static void reset(void){
 mainctx=(AioContext){0};ioctx=(AioContext){0};
 main_loop_poll_notifiers.notifiers.empty=true;
 bql=true;progress=prepare_race=opt_deterministic=gpu_busy=false;icount=true;
 prepared=cancelled=committed=timed=polls=timers=warps=skips=0;
 host_now=0;elapsed=500;virtual_now=1000;last_us=last_poll=-2;clock_deadline=-1;socket_ms=UINT32_MAX;
}
int main(void){
 unsigned cases=0;
 const int64_t bounds[]={-1,0,1,999,1000,1001,1234567,INT64_MAX};
 for(unsigned n=0;n<8;n++)for(unsigned race=0;race<2;race++)for(unsigned due=0;due<2;due++)for(unsigned mode=0;mode<2;mode++){
  reset();prepare_race=race;progress=due;icount=mode;
  main_loop_wait_timeout(bounds[n]);assert(bql && timers==1 && warps==mode);
  if(due)assert(!prepared && !timed && !committed && !cancelled);
  else if(race)assert(prepared==1 && cancelled==1 && !timed && !committed);
  else if(bounds[n]<0)assert(prepared==1 && committed==1 && !timed);
  else assert(prepared==1 && timed==1 && !committed && last_us==bounds[n]/1000+(bounds[n]%1000!=0));
  assert(prepared==cancelled+committed+timed);cases++;
 }
 for(unsigned nonblock=0;nonblock<2;nonblock++){
  reset();main_loop_wait(nonblock);assert(timers==1 && !timed && committed==!nonblock);cases++;
 }
 const int64_t deadlines[]={-1,0,500,2500000};
 for(unsigned n=0;n<8;n++)for(unsigned d=0;d<4;d++)for(unsigned fd=0;fd<2;fd++)for(unsigned nonblock=0;nonblock<2;nonblock++){
  reset();clock_deadline=deadlines[d];socket_ms=2;
  if(fd)ioctx.fds=true;else main_loop_poll_notifiers.notifiers.empty=false;
  if(nonblock)main_loop_wait(true);else main_loop_wait_timeout(bounds[n]);
  int64_t want=qemu_soonest_timeout(nonblock?0:fd?-1:2000000,clock_deadline);
  if(!nonblock)want=qemu_soonest_timeout(want,bounds[n]);
  assert(polls==1 && last_poll==want && !prepared && timers==1);cases++;
 }
 const int64_t waits[]={0,1,999,1001,50000,20000000};
 for(unsigned det=0;det<2;det++)for(unsigned gpu=0;gpu<2;gpu++)for(unsigned n=2;n<7;n++)for(unsigned w=0;w<6;w++){
  reset();opt_deterministic=det;gpu_busy=gpu;elapsed=waits[w];
  lr_wait_for_device(virtual_now,bounds[n]);
  if(gpu && !det){assert(timed==1 && !committed && skips==1);assert(virtual_now==1000+MIN(waits[w],bounds[n]));}
  else assert(!timed && committed==1 && !skips && virtual_now==1000);
  assert(bql && timers==1);cases++;
 }
 printf("GPU deadline waits: %u timeout/rounding/prepare-race/notification/dispatch/poll/deterministic/elapsed-clock cases passed\n",cases);
 return 0;
}
