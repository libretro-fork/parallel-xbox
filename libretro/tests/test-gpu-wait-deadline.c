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
static bool disk_progress;
static int lr_disk_pending;
static bool lr_disk_poll(void){bool ran=disk_progress;disk_progress=false;lr_disk_pending=0;return ran;}
static bool bql,progress,prepare_race,icount,gpu_busy,cpu_halted;
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
static bool __attribute__((unused)) icount_enabled(void){return icount;}
/* The old second clock owner: even a host device wait moved guest time. */
static void __attribute__((unused)) icount_start_warp_timer(void){warps++;if(icount && cpu_halted && clock_deadline>0)virtual_now+=clock_deadline;}
static void qemu_clock_run_all_timers(void){assert(bql);timers++;}
static int retro_eventcount_prepare_wait(int *p){assert(p==&main_loop_ec && bql);prepared++;if(prepare_race)ioctx.notified=1;return 7;}
static void retro_eventcount_cancel_wait(int *p){assert(p==&main_loop_ec && bql);cancelled++;}
static void retro_eventcount_commit_wait(int *p,int key){assert(p==&main_loop_ec && key==7 && !bql);committed++;host_now+=elapsed;}
static bool retro_eventcount_commit_wait_timeout(int *p,int key,int64_t us){assert(p==&main_loop_ec && key==7 && !bql && us>=0);timed++;last_us=us;host_now+=elapsed;return false;}
static void bql_unlock(void){assert(bql);bql=false;}
static void bql_lock(void){assert(!bql);bql=true;}
bool nv2a_libretro_guest_busy(void){return gpu_busy;}

static void cpu_clock_skip_to(int64_t ns){assert(ns>=virtual_now);virtual_now=ns;skips++;}
typedef struct { bool runnable; } CPUState;
static bool lr_quiet, disk_busy;
static int nv2a_libretro_waiter;
static bool lr_devices_busy(void) {return gpu_busy || disk_busy;}
static bool lr_vcpu_runnable(CPUState *cpu) {return cpu->runnable;}
static void retro_atomic_store_relaxed_int(int *p,int v) {*p=v;}
static void retro_atomic_thread_fence_seq_cst(void) {}
#define LR_DSP_EXTERNAL_GUARD ((void)0)
#define LR_DSP_FENCE_POLL 1
static void lr_dsp_ownership_fence(unsigned reason){assert(reason==1 && bql);}
/* PRODUCTION */
static void reset(void){
 mainctx=(AioContext){0};ioctx=(AioContext){0};
 main_loop_poll_notifiers.notifiers.empty=true;
 disk_progress=false;lr_disk_pending=0;bql=true;progress=prepare_race=gpu_busy=cpu_halted=false;icount=true;
 prepared=cancelled=committed=timed=polls=timers=warps=skips=0;
 host_now=0;elapsed=500;virtual_now=1000;last_us=last_poll=-2;clock_deadline=-1;socket_ms=UINT32_MAX;
}
int main(void){
 unsigned cases=0;
 /* Host dispatch never owns the guest clock, including HALT plus a GPU
  * wait, descriptor polling or NAT polling. The frame owner advances it. */
 for(unsigned path=0;path<3;path++)for(unsigned nb=0;nb<2;nb++)for(unsigned halt=0;halt<2;halt++){
  reset();cpu_halted=!halt;gpu_busy=true;clock_deadline=500;
  if(path==1)ioctx.fds=true;
  if(path==2)main_loop_poll_notifiers.notifiers.empty=false;
  if(nb)main_loop_wait(true);else main_loop_wait_timeout(1000);
  if(virtual_now!=1000 || warps){fprintf(stderr,"Host dispatch moved guest clock from 1000 to %lld ns (%u warp calls, HALT=%d)\n",(long long)virtual_now,warps,cpu_halted);_Exit(1);}
  assert(timers==1);cases++;
 }
 /* Disk completions make progress on either dispatch path and must not park. */
 for(unsigned path=0;path<3;path++) {
  reset();disk_progress=true;lr_disk_pending=1;
  if(path==1)ioctx.fds=true;
  if(path==2)main_loop_poll_notifiers.notifiers.empty=false;
  main_loop_wait(false);assert(!disk_progress && !prepared && !committed && !timed);
  if(path)assert(polls==1 && last_poll==0);
  cases++;
 }
 const int64_t bounds[]={-1,0,1,999,1000,1001,1234567,INT64_MAX};
 for(unsigned n=0;n<8;n++)for(unsigned race=0;race<2;race++)for(unsigned due=0;due<2;due++)for(unsigned mode=0;mode<2;mode++){
  reset();prepare_race=race;progress=due;icount=mode;
  main_loop_wait_timeout(bounds[n]);assert(bql && timers==1 && warps==0);
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
 /* Even a slow host completion cannot consume a guest frame. */
 for(unsigned gpu=0;gpu<2;gpu++)for(unsigned w=0;w<6;w++){
  reset();gpu_busy=gpu;elapsed=waits[w];
  lr_wait_for_device();
  assert(!timed && committed==1 && !skips && virtual_now==1000);
  assert(host_now==waits[w] && bql && timers==1);cases++;
 }
 /* Completion between the quiet exit and frame-owner recheck. A runnable
  * guest must execute again; only HALT may skip directly to the deadline. */
 for(unsigned run=0;run<2;run++)for(unsigned gpu=0;gpu<2;gpu++)
 for(unsigned disk=0;disk<2;disk++) {
  reset();CPUState cpu={.runnable=run};gpu_busy=gpu;disk_busy=disk;
  lr_quiet=true;nv2a_libretro_waiter=0;
  lr_idle_wait(&cpu,1000,999);
  assert(!lr_quiet && !nv2a_libretro_waiter);
  if(gpu || disk) assert(virtual_now==1000 && !skips);
  else assert(virtual_now==(run?1000:1999) && skips==!run);
  cases++;
 }
 printf("GPU deadline waits: %u clock-ownership/HALT/timeout/rounding/prepare-race/notification/dispatch/poll/host-independent-clock cases passed\n",cases);
 return 0;
}
