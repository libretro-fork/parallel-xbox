# SPDX-License-Identifier: GPL-2.0-or-later
"""Production GPU idle waits use notification or readiness, not timed polling."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pfifo.c').read_text();a=s.index('static inline void pfifo_idle_wait(');b=s.index('\n/* Both locks held',a)
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct NV2AState {
 struct {int fifo_kick,park_requests,frame_end_req,frame_end_done,guest_done,guest_seen,fifo_ec,lock;} pfifo;
 struct {int host_flip_pending,reports_pending,scanout_changed;} pgraph;
 int exiting;
} NV2AState;
typedef struct {struct {int (*poll_pending_report)(NV2AState*);} ops;} PGRAPHRenderer;
static PGRAPHRenderer renderer;
static bool held;
static unsigned prepared,cancelled,committed,yielded,notified,polled,processed,race;
static int poll_status,nv2a_libretro_waiter;
static NV2AState *active;
static const PGRAPHRenderer *pgraph_renderer(void*p){(void)p;return &renderer;}
static int poll_report(NV2AState*d){assert(d==active&&!held);polled++;return poll_status;}
static void qemu_mutex_unlock(int*p){(void)p;assert(held);held=false;}
static void qemu_mutex_lock(int*p){(void)p;assert(!held);held=true;}
static int retro_atomic_load_acquire_int(int*p){return *p;}
static int retro_atomic_load_relaxed_int(int*p){return *p;}
static void retro_atomic_store_release_int(int*p,int n){*p=n;}
static void retro_atomic_store_relaxed_int(int*p,int n){*p=n;}
static void retro_atomic_thread_fence_seq_cst(void){}
static int retro_eventcount_prepare_wait(int*p){assert(p==&active->pfifo.fifo_ec&&held);prepared++;if(race==1)active->pfifo.fifo_kick=1;if(race==2)active->pfifo.park_requests=1;if(race==3)active->exiting=1;return 7;}
static void retro_eventcount_cancel_wait(int*p){assert(p==&active->pfifo.fifo_ec);cancelled++;}
static void retro_eventcount_commit_wait(int*p,int key){assert(p==&active->pfifo.fifo_ec&&key==7&&!held);committed++;}
static void sthread_yield(void){assert(!held);yielded++;}
static void qemu_notify_event(void){notified++;}
static void lr_dsp_bus_borrow(void){}
static void lr_dsp_bus_return(void){}
static void pgraph_process_pending(NV2AState*d){assert(d==active&&held);processed++;}
/* PRODUCTION */
int main(void){
 unsigned cases=0;
 renderer.ops.poll_pending_report=poll_report;
 for(int status=-1;status<=1;status++)for(unsigned kick=0;kick<2;kick++)
 for(unsigned inject=0;inject<4;inject++)for(unsigned waiter=0;waiter<2;waiter++){
  NV2AState d={.pfifo={.fifo_kick=kick,.guest_seen=42,.guest_done=7},.pgraph={.reports_pending=status>=0}};
  active=&d;held=true;poll_status=status;race=inject;nv2a_libretro_waiter=waiter;
  prepared=cancelled=committed=yielded=notified=polled=processed=0;
  pfifo_idle_wait(&d);assert(held);
  if(status==1||kick)assert(!prepared&&!cancelled&&!committed&&!yielded);
  else if(inject)assert(prepared==1&&cancelled==1&&!committed&&!yielded);
  else if(status==0)assert(prepared==1&&cancelled==1&&yielded==1&&!committed&&d.pfifo.guest_done==7&&!notified);
  else assert(prepared==1&&committed==1&&!cancelled&&!yielded&&d.pfifo.guest_done==42&&notified==waiter);
  assert(polled==(unsigned)(status>=0));cases++;
 }
 printf("GPU notification waits: %u readiness/kick/park/exit/wakeup schedules, no timed-wait API required PASS\n",cases);
}
"""
code=code.replace('/* PRODUCTION */',s[a:b])
with tempfile.TemporaryDirectory(prefix='pfifo-notifications-') as t:
 t=Path(t);(t/'test.c').write_text(code);binary=t/'test.exe'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',str(t/'test.c'),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True,timeout=30)
