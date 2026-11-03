# SPDX-License-Identifier: GPL-2.0-or-later
"""Core thread waits must hand off without querying or calibrating host clocks."""
from pathlib import Path
import subprocess,sys,os,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
code=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
static BOOL forbidden_qpc(LARGE_INTEGER*p){(void)p;fputs("forbidden host clock\n",stderr);exit(86);return FALSE;}
#define QueryPerformanceCounter forbidden_qpc
#define QueryPerformanceFrequency forbidden_qpc
#else
static int forbidden_clock(clockid_t id,struct timespec*t){(void)id;(void)t;fputs("forbidden host clock\n",stderr);exit(86);return -1;}
#define clock_gettime forbidden_clock
#endif
/* THREADS */
/* EVENTS */
static retro_eventcount_t request_ec,reply_ec;
static retro_atomic_int_t request,done;
static scond_t *request_cond,*reply_cond;
static slock_t *lock;
static unsigned cond_request,cond_done;
static void event_worker(void*unused){(void)unused;
 for(int i=1;i<=500;i++){
  while(retro_atomic_load_acquire_int(&request)<i){int key=retro_eventcount_prepare_wait(&request_ec);if(retro_atomic_load_acquire_int(&request)>=i)retro_eventcount_cancel_wait(&request_ec);else retro_eventcount_commit_wait(&request_ec,key);}
  retro_atomic_store_release_int(&done,i);retro_eventcount_notify(&reply_ec);
 }
}
static void cond_worker(void*unused){(void)unused;slock_lock(lock);
 for(unsigned i=1;i<=500;i++){
  while(cond_request<i)scond_wait(request_cond,lock);
  cond_done=i;scond_signal(reply_cond);
 }
 slock_unlock(lock);
}
int main(void){
 assert(retro_eventcount_init(&request_ec)&&retro_eventcount_init(&reply_ec));
 sthread_t *thread=sthread_create(event_worker,NULL);assert(thread);
 for(int i=1;i<=500;i++){
  retro_atomic_store_release_int(&request,i);retro_eventcount_notify(&request_ec);
  while(retro_atomic_load_acquire_int(&done)<i){int key=retro_eventcount_prepare_wait(&reply_ec);if(retro_atomic_load_acquire_int(&done)>=i)retro_eventcount_cancel_wait(&reply_ec);else retro_eventcount_commit_wait(&reply_ec,key);}
 }
 sthread_join(thread);
 unsigned spins=retro_eventcount_spin_iters();assert(spins==0||spins==256);
 retro_eventcount_free(&request_ec);retro_eventcount_free(&reply_ec);
 lock=slock_new();request_cond=scond_new();reply_cond=scond_new();assert(lock&&request_cond&&reply_cond);
 thread=sthread_create(cond_worker,NULL);assert(thread);slock_lock(lock);
 for(unsigned i=1;i<=500;i++){cond_request=i;scond_signal(request_cond);while(cond_done<i)scond_wait(reply_cond,lock);}
 slock_unlock(lock);sthread_join(thread);
#if defined(USE_WIN32_THREADS)
 assert(scond_g.spin==SCOND_SPIN_NONE||scond_g.spin==SCOND_SPIN_PAUSE);
 assert(scond_g.spin_iters==256);
#endif
 scond_free(request_cond);scond_free(reply_cond);slock_free(lock);
 puts("Clockless thread waits: 500 eventcount and 500 condition handoffs passed with host-clock calls trapped");
}
"""
code=code.replace('/* THREADS */','#include "'+(r/'libretro-common/rthreads/rthreads.c').as_posix()+'"').replace('/* EVENTS */','#include "'+(r/'libretro-common/rthreads/retro_eventcount.c').as_posix()+'"')
env=os.environ.copy();env.pop('RETRO_EVENTCOUNT_SPIN',None);env['RETRO_EVENTCOUNT_SPIN_US']='500';env['RTHREADS_SCOND_SPIN']='umwait';env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='clockless-waits-') as t:
 t=Path(t);(t/'test.c').write_text(code);binary=t/'test.exe'
 cmd=[cc,'-O2','-std=gnu11','-w','-D_GNU_SOURCE','-DHAVE_THREADS','-DHAVE_THREAD_STORAGE','-DRETRO_EVENTCOUNT_FIXED_SPIN=256','-DRTHREADS_SCOND_FIXED_SPIN=256','-I'+str(r/'libretro-common/include'),str(t/'test.c')]
 cmd += [str(r/name) for name in ['libretro-common/compat/compat_strl.c','libretro-common/features/features_cpu.c','libretro-common/time/rtime.c']]
 cmd += ['-o',str(binary),'-pthread']
 if os.name=='nt':cmd+=['-static']
 subprocess.run(cmd,env=env,check=True)
 subprocess.run([str(binary)],env=env,check=True,timeout=30)
 negative=[x for x in cmd if not x.startswith('-DRETRO_EVENTCOUNT_FIXED_SPIN=') and not x.startswith('-DRTHREADS_SCOND_FIXED_SPIN=')]
 subprocess.run(negative,env=env,check=True)
 result=subprocess.run([str(binary)],env=env,capture_output=True,timeout=30)
 assert result.returncode==86 and b'forbidden host clock' in result.stderr
 print('Negative control detected host-clock calibration when fixed-spin flags were removed')
