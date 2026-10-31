/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <retro_timers.h>
#include <features/features_cpu.h>
#include "qemu/queue.h"
#define SCALE_NS 1
#define SCALE_US 1000
#define SCALE_MS 1000000
#define QEMU_TIMER_ATTR_EXTERNAL 1
#define QEMU_TIMER_ATTR_ALL (-1)
/* ENUM */
typedef struct QEMUTimer QEMUTimer;
typedef struct QEMUTimerList QEMUTimerList;
typedef void QEMUTimerCB(void *);
typedef void QEMUTimerListNotifyCB(void *,QEMUClockType);
/* TIMER */
typedef struct { slock_t *lock; } QemuMutex;
typedef struct { bool set; } QemuEvent;
static _Thread_local bool reader;
static void qemu_mutex_init(QemuMutex*m){m->lock=slock_new();assert(m->lock);}
static void qemu_mutex_lock(QemuMutex*m){
#if !defined(TIMER_READ_BASELINE) && !defined(TIMER_READ_BENCHMARK)
 assert(!reader); /* Fast deadline reads must never acquire a queue lock. */
#endif
 slock_lock(m->lock);
}
static void qemu_mutex_unlock(QemuMutex*m){slock_unlock(m->lock);}
static QemuMutex *guard_enter(QemuMutex*m){qemu_mutex_lock(m);return m;}
static void guard_exit(QemuMutex**m){if(*m)qemu_mutex_unlock(*m);}
#define WITH_QEMU_LOCK_GUARD(m) for(QemuMutex *guard __attribute__((cleanup(guard_exit)))=guard_enter(m);guard;qemu_mutex_unlock(guard),guard=NULL)
#define g_free free
static void qemu_event_init(QemuEvent*e,bool set){e->set=set;}
static void qemu_event_reset(QemuEvent*e){e->set=false;}
static void qemu_event_set(QemuEvent*e){e->set=true;}
static void qemu_event_wait(QemuEvent*e){assert(e->set);}
static bool icount_enabled(void){return false;}
static int64_t get_clock(void){return 0;}
static int64_t get_clock_realtime(void){return 0;}
static int64_t cpus_get_virtual_clock(void){return 0;}
static int64_t cpu_get_clock(void){return 0;}
static int64_t qemu_soonest_timeout(int64_t a,int64_t b){return a<0?b:b<0?a:MIN(a,b);}
static void qemu_notify_event(void){}
int64_t qemu_clock_get_ns(QEMUClockType);
bool timer_pending(const QEMUTimer*);
/* PRODUCTION */
static retro_atomic_int_t started, stop;
#define A ((1ll<<33)+7)
#define B ((1ll<<34)+9)
static void callback(void *opaque){(void)opaque;}
static void initialize(QEMUTimer*t,unsigned type,int attr){timer_init_full(t,type,1,attr,callback,NULL);}
static void check_readers(void *unused)
{
 (void)unused; reader=true;
 retro_atomic_fetch_add_int(&started,1);
 while(!retro_atomic_load_acquire_int(&stop)){
  for(unsigned type=0;type<QEMU_CLOCK_MAX;type++){
   int64_t all=qemu_clock_deadline_ns_all(type,QEMU_TIMER_ATTR_ALL);
   int64_t internal=qemu_clock_deadline_ns_all(type,~QEMU_TIMER_ATTR_EXTERNAL);
   assert(all==-1||all==A||all==A+1||all==B||all==B+1);
   assert(internal==-1||internal==A||internal==A+1||internal==B||internal==B+1);
   assert(!qemu_clock_expired(type));
  }
 }
}
static void publish_free(void)
{
 for(unsigned n=0;n<100000;n++){
  unsigned type=n%QEMU_CLOCK_MAX;
  QEMUTimer *t=calloc(1,sizeof(*t)), *u=calloc(1,sizeof(*u));assert(t&&u);
  initialize(t,type,(n&1)?QEMU_TIMER_ATTR_EXTERNAL:0);
  initialize(u,type,0);
  timer_mod_ns(t,A); timer_mod_ns(u,B);
  timer_mod_ns(t,A+1); timer_mod_ns(u,B+1);
  timer_mod_ns(t,B); timer_mod_anticipate_ns(t,A);
  if((n&127)==0){qemu_clock_enable(type,false);qemu_clock_enable(type,true);}
  timer_del(t);free(t);timer_del(u);free(u);
 }
}
static void stalled_writer(void)
{
#ifndef TIMER_READ_BASELINE
 QEMUTimer t;initialize(&t,0,0);timer_mod_ns(&t,A);
 QEMUTimerList *clock=qemu_clock_ptr(0);
 qemu_mutex_lock(&clock->active_timers_lock);
 QEMUTimer *saved=clock->active_timers;
 /* A reader must neither wait for the held lock nor touch a timer pointer.
  * This canary also detects attempts to use an atomic list head as a cache. */
 clock->active_timers=(QEMUTimer *)(uintptr_t)1;
 reader=true;
 assert(qemu_clock_deadline_ns_all(0,QEMU_TIMER_ATTR_ALL)==A);
 assert(qemu_clock_deadline_ns_all(0,~QEMU_TIMER_ATTR_EXTERNAL)==A);
 assert(!qemu_clock_expired(0));
 reader=false;
 clock->active_timers=saved;
 qemu_mutex_unlock(&clock->active_timers_lock);timer_del(&t);
#endif
}
static void benchmark(void)
{
 QEMUTimer t[32];unsigned count=300000;
 for(unsigned i=0;i<32;i++){initialize(&t[i],0,i<16?1:0);timer_mod_ns(&t[i],A+i);}
 for(unsigned sample=0;sample<3;sample++){
  int64_t sum=0;uint64_t start=cpu_features_get_time_usec();
  for(unsigned i=0;i<count;i++) sum+=qemu_clock_deadline_ns_all(0,QEMU_TIMER_ATTR_ALL);
  uint64_t end=cpu_features_get_time_usec();
  printf("all_ns=%.3f ",(end-start)*1000.0/count);
  start=cpu_features_get_time_usec();
  for(unsigned i=0;i<count;i++) sum+=qemu_clock_deadline_ns_all(0,~QEMU_TIMER_ATTR_EXTERNAL);
  end=cpu_features_get_time_usec();printf("filtered_ns=%.3f ",(end-start)*1000.0/count);
  start=cpu_features_get_time_usec();
  for(unsigned i=0;i<count;i++)timer_mod_ns(&t[31],B+i);
  end=cpu_features_get_time_usec();printf("modify_ns=%.3f checksum=%lld\n",(end-start)*1000.0/count,(long long)sum);
 }
 for(unsigned i=0;i<32;i++)timer_del(&t[i]);
}
static void benchmark_matrix(void)
{
 const unsigned sizes[]={1,8,32,128},count=100000;
 const char*names[]={"tail","middle","head","cross","ties","delete_arm"};
 for(unsigned size_i=0;size_i<4;size_i++)for(unsigned pattern=0;pattern<6;pattern++){
  unsigned size=sizes[size_i];QEMUTimer t[128];
  for(unsigned i=0;i<size;i++){initialize(&t[i],0,i<size/2?1:0);timer_mod_ns(&t[i],pattern==4?A:A+i*1000000ll);}
  unsigned index=pattern==0||pattern==5?size-1:pattern==1?size/2:0;
  for(unsigned sample=0;sample<3;sample++){
   uint64_t start=cpu_features_get_time_usec();
   for(unsigned i=0;i<count;i++){
    QEMUTimer *timer=&t[pattern==4?i%size:index];
    int64_t expiry=pattern==0||pattern==5?B+i:
                   pattern==1?A+index*1000000ll+(i&65535):
                   pattern==2?A+(i&65535):
                   pattern==3?(i&1?A-1:B+i):A;
    if(pattern==5)timer_del(timer);
    timer_mod_ns(timer,expiry);
   }
   uint64_t end=cpu_features_get_time_usec();
   printf("size=%u pattern=%s sample=%u ns=%.3f\n",size,names[pattern],sample,(end-start)*1000.0/count);
  }
  for(unsigned i=0;i<size;i++)timer_del(&t[i]);
 }
}
int main(int argc,char **argv)
{
 qemu_init_clocks(NULL);
 for(unsigned type=0;type<QEMU_CLOCK_MAX;type++)qemu_clock_enable(type,true);
 if(argc>1){
#ifndef TIMER_READ_BENCHMARK
  fputs("Build benchmarks with TIMER_READ_BENCHMARK=1 (or TIMER_READ_ARGS=bench/matrix).\n",stderr);return 2;
#endif
  puts("benchmark instrumentation=off");
  if(!strcmp(argv[1],"matrix"))benchmark_matrix();else benchmark();return 0;
 }
 stalled_writer();sthread_t*threads[6];
 for(unsigned i=0;i<6;i++){threads[i]=sthread_create(check_readers,NULL);assert(threads[i]);}
 while(retro_atomic_load_acquire_int(&started)<6){}
 publish_free();retro_atomic_store_release_int(&stop,1);
 for(unsigned i=0;i<6;i++)sthread_join(threads[i]);
 for(unsigned type=0;type<QEMU_CLOCK_MAX;type++){
  assert(qemu_clock_deadline_ns_all(type,QEMU_TIMER_ATTR_ALL)==-1);
  slock_free(qemu_clock_ptr(type)->active_timers_lock.lock);
 }
 puts("Timer deadlines: held-lock/invalid-head reads, six concurrent readers, 100000 modify/cancel/free cycles and pause/resume PASS");return 0;
}
