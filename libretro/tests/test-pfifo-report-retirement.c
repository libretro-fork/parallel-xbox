/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct {struct {bool host_flip_pending,reports_pending;int scanout_changed;}pgraph;
 struct {int lock,fifo_kick,fifo_ec,park_requests,frame_end_req,frame_end_done,guest_done,guest_seen;}pfifo;int exiting;}NV2AState;
typedef struct {struct {int (*poll_pending_report)(NV2AState*);}ops;}PGRAPHRenderer;
static PGRAPHRenderer renderer;
static int nv2a_libretro_waiter,status,race;
static bool locked,prepared;
static unsigned checks,cancels,parks,timed,notifies,displays;
static NV2AState *active;
static const PGRAPHRenderer *pgraph_renderer(void *p){(void)p;return &renderer;}
static int poll_report(NV2AState*d){assert(d==active&&!locked);checks++;return status;}
static int retro_atomic_load_acquire_int(int*p){return *p;}
static int retro_atomic_load_relaxed_int(int*p){return *p;}
static void retro_atomic_store_release_int(int*p,int v){*p=v;}
static void retro_atomic_store_relaxed_int(int*p,int v){*p=v;}
static void retro_atomic_thread_fence_seq_cst(void){}
static void qemu_mutex_unlock(int*p){assert(p==&active->pfifo.lock&&locked);locked=false;}
static void qemu_mutex_lock(int*p){assert(p==&active->pfifo.lock&&!locked);locked=true;}
static int retro_eventcount_prepare_wait(int*p){assert(p==&active->pfifo.fifo_ec&&locked&&!prepared);prepared=true;
 if(race==1)active->pfifo.fifo_kick=1;
 if(race==2)active->pfifo.park_requests=1;
 if(race==3)active->exiting=1;
 if(race==4)active->pfifo.frame_end_req++;
 return 7;
}
static void retro_eventcount_cancel_wait(int*p){assert(p==&active->pfifo.fifo_ec&&locked&&prepared);prepared=false;cancels++;}
static void retro_eventcount_commit_wait(int*p,int key){assert(p==&active->pfifo.fifo_ec&&key==7&&!locked&&prepared);prepared=false;parks++;}
static bool retro_eventcount_commit_wait_timeout(int*p,int key,int64_t us){assert(p==&active->pfifo.fifo_ec&&key==7&&!locked&&prepared&&us==100);assert(active->pfifo.guest_done!=active->pfifo.guest_seen);prepared=false;timed++;return false;}
static void qemu_notify_event(void){notifies++;}
static void lr_dsp_bus_borrow(void){}
static void lr_dsp_bus_return(void){}
static void pgraph_process_pending(NV2AState*d){assert(d==active&&locked);displays++;}
/* PRODUCTION */
static void setup(NV2AState*d){memset(d,0,sizeof(*d));active=d;d->pfifo.guest_seen=17;d->pfifo.guest_done=16;
 locked=true;prepared=false;checks=cancels=parks=timed=notifies=displays=0;nv2a_libretro_waiter=1;renderer.ops.poll_pending_report=poll_report;}
int main(void){NV2AState d;unsigned cases=0;
 for(unsigned reports=0;reports<2;reports++)for(unsigned flip=0;flip<2;flip++)for(status=-1;status<=1;status++)for(race=0;race<5;race++){
  setup(&d);d.pgraph.reports_pending=reports;d.pgraph.host_flip_pending=flip;
  pfifo_idle_wait(&d);assert(locked&&!prepared);bool checked=reports&&!flip;assert(checks==checked);
  if(checked&&status==1){assert(!cancels&&!parks&&!timed&&!displays&&d.pfifo.guest_done==16);}
  else if(race){assert(cancels==1&&!parks&&!timed&&d.pfifo.guest_done==16);assert(displays==(race==4));if(race==4)assert(d.pfifo.frame_end_done==d.pfifo.frame_end_req&&notifies==1);}
  else if(checked&&status==0){assert(timed==1&&!parks&&!notifies&&d.pfifo.guest_done==16);}
  else{assert(parks==1&&!timed&&d.pfifo.guest_done==17&&notifies==1);}
  cases++;
 }
 /* A report completes on a timeout without another guest submission. The
  * next owner pass serves it before guest work is acknowledged. */
 setup(&d);race=0;status=0;d.pgraph.reports_pending=true;
 for(unsigned i=0;i<1000;i++){pfifo_idle_wait(&d);assert(d.pfifo.guest_done==16);}
 status=1;pfifo_idle_wait(&d);assert(timed==1000&&checks==1001&&!parks);
 d.pgraph.reports_pending=false;pfifo_idle_wait(&d);assert(d.pfifo.guest_done==17&&parks==1);
 /* A frame request during a pending query is serviced without GPU readiness. */
 setup(&d);status=0;race=4;d.pgraph.reports_pending=true;pfifo_idle_wait(&d);assert(displays==1&&d.pfifo.frame_end_done==1&&!timed&&d.pfifo.guest_done==16);
 printf("PFIFO report retirement: %u kick/park/shutdown/frame races; 1000 delayed retries, completion without guest kicks, frame acknowledgement and outstanding-work ownership passed\n",cases);
}
