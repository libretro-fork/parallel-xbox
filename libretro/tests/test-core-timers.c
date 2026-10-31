/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include "qemu/queue.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
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
typedef struct {unsigned depth;} QemuMutex;
typedef struct {bool set;} QemuEvent;
static unsigned allocations,mutexes,events,notifies[QEMU_CLOCK_MAX];
static bool counted;
static int64_t now[QEMU_CLOCK_MAX];
static __attribute__((unused)) void *allocate(size_t n,size_t size){allocations++;void*p=calloc(n,size);assert(p);return p;}
#define g_new0(type,n) ((type*)allocate(n,sizeof(type)))
#define g_free free
static void qemu_mutex_init(QemuMutex*m){m->depth=0;mutexes++;}
static __attribute__((unused)) void qemu_mutex_destroy(QemuMutex*m){assert(!m->depth);}
static void qemu_mutex_lock(QemuMutex*m){assert(!m->depth);m->depth=1;}
static void qemu_mutex_unlock(QemuMutex*m){assert(m->depth==1);m->depth=0;}
static QemuMutex *guard_enter(QemuMutex*m){qemu_mutex_lock(m);return m;}
static void guard_exit(QemuMutex**m){if(*m)qemu_mutex_unlock(*m);}
#define WITH_QEMU_LOCK_GUARD(m) for(QemuMutex *guard __attribute__((cleanup(guard_exit)))=guard_enter(m);guard;qemu_mutex_unlock(guard),guard=NULL)
static __attribute__((unused)) void qemu_event_init(QemuEvent*e,bool set){e->set=set;events++;}
static __attribute__((unused)) void qemu_event_reset(QemuEvent*e){e->set=false;}
static __attribute__((unused)) void qemu_event_set(QemuEvent*e){e->set=true;}
static __attribute__((unused)) void qemu_event_wait(QemuEvent*e){assert(e->set);}
static bool icount_enabled(void){return counted;}
static int64_t get_clock(void){return now[QEMU_CLOCK_REALTIME];}
static int64_t get_clock_realtime(void){return now[QEMU_CLOCK_HOST];}
static int64_t cpus_get_virtual_clock(void){return now[QEMU_CLOCK_VIRTUAL];}
static int64_t cpu_get_clock(void){return now[QEMU_CLOCK_VIRTUAL_RT];}
static int64_t qemu_soonest_timeout(int64_t a,int64_t b){return a<0?b:b<0?a:MIN(a,b);}
static void qemu_notify_event(void){}
int64_t qemu_clock_get_ns(QEMUClockType);
bool timer_pending(const QEMUTimer*);
#ifdef TIMER_BASELINE
typedef struct {QEMUTimerList *tl[QEMU_CLOCK_MAX];} QEMUTimerListGroup;
bool timerlist_has_timers(QEMUTimerList*);
void timerlist_notify(QEMUTimerList*);
#endif
/* PRODUCTION */
static void notify(void *opaque,QEMUClockType type){assert(!opaque);notifies[type]++;}
#ifdef TIMER_BASELINE
static void unused_notify(void *opaque,QEMUClockType type){}
#endif
static void initialize(QEMUTimer*t,QEMUClockType type,int attrs,QEMUTimerCB*cb,void*opaque){
#ifdef TIMER_BASELINE
 timer_init_full(t,NULL,type,SCALE_NS,attrs,cb,opaque);
#else
 timer_init_full(t,type,SCALE_NS,attrs,cb,opaque);
#endif
}
static int64_t poll_deadline(void){
#ifdef TIMER_BASELINE
 return timerlistgroup_deadline_ns(&main_loop_tlg);
#else
 return qemu_clock_deadline_ns();
#endif
}
#define N 32
static QEMUTimer timers[N];
static struct {int64_t expiry;uint64_t order;unsigned type,attrs;} ref[N];
static uint64_t serial;
static bool enabled[QEMU_CLOCK_MAX];
static unsigned actual[256],expected[256],actual_n,expected_n;
static void record(void*opaque){unsigned id=(unsigned)(uintptr_t)opaque;assert(!timer_pending(&timers[id]));assert(!timers[id].timer_list->active_timers_lock.depth);assert(actual_n<256);actual[actual_n++]=id;}
static void ref_mod(unsigned id,int64_t expiry){ref[id].expiry=MAX(expiry,0);ref[id].order=++serial;}
static int next(unsigned type,int mask){int best=-1;for(unsigned i=0;i<N;i++)if(ref[i].type==type&&ref[i].expiry>=0&&!(ref[i].attrs&~mask)&&(best<0||ref[i].expiry<ref[best].expiry||(ref[i].expiry==ref[best].expiry&&ref[i].order<ref[best].order)))best=i;return best;}
static int64_t deadline(unsigned type,int mask){int id=next(type,mask);return !enabled[type]||id<0?-1:MAX(0,ref[id].expiry-now[type]);}
static void run(unsigned type){
 actual_n=expected_n=0;int id;
 if(enabled[type])while((id=next(type,QEMU_TIMER_ATTR_ALL))>=0&&ref[id].expiry<=now[type]){expected[expected_n++]=id;ref[id].expiry=-1;}
 bool progress=qemu_clock_run_timers(type);assert(progress==(expected_n!=0));assert(actual_n==expected_n&&!memcmp(actual,expected,actual_n*sizeof(*actual)));
}
static void check(void){
 int64_t soonest=-1;
 for(unsigned type=0;type<QEMU_CLOCK_MAX;type++){
  int masks[]={QEMU_TIMER_ATTR_ALL,~QEMU_TIMER_ATTR_EXTERNAL,0,QEMU_TIMER_ATTR_EXTERNAL,4,~4};
  for(unsigned m=0;m<sizeof(masks)/sizeof(masks[0]);m++){int mask=masks[m];assert(qemu_clock_deadline_ns_all(type,mask)==deadline(type,mask));}
  int id=next(type,QEMU_TIMER_ATTR_ALL);assert(qemu_clock_expired(type)==(id>=0&&ref[id].expiry<=now[type]));
  if(!(counted&&type==QEMU_CLOCK_VIRTUAL))soonest=qemu_soonest_timeout(soonest,deadline(type,QEMU_TIMER_ATTR_ALL));
 }
 assert(poll_deadline()==soonest);
 for(unsigned i=0;i<N;i++){assert(timer_pending(&timers[i])==(ref[i].expiry>=0));assert(timer_expire_time_ns(&timers[i])==(uint64_t)ref[i].expiry);}
}
static uint32_t rng=0x3456789a;
static uint32_t random_value(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static QEMUTimer a,b,c;
static unsigned special[8],special_n;
static void special_c(void*opaque){special[special_n++]=3;}
static void special_a(void*opaque){assert(!timer_pending(&a));special[special_n++]=1;if(special_n==1){timer_mod_ns(&a,now[0]);timer_del(&b);timer_mod_ns(&c,now[0]);}}
static void special_b(void*opaque){special[special_n++]=2;}
static void free_self(void*opaque){QEMUTimer*t=opaque;assert(!timer_pending(t));free(t);special[special_n++]=4;}
int main(void){
#ifndef TIMER_BASELINE
 for(unsigned type=0;type<QEMU_CLOCK_MAX;type++){
  assert(!qemu_clock_expired(type));
  assert(qemu_clock_deadline_ns_all(type,QEMU_TIMER_ATTR_ALL)==-1);
 }
#endif

 qemu_init_clocks(notify);
#ifdef TIMER_BASELINE
 QEMUTimerListGroup unused[2];for(unsigned i=0;i<2;i++)timerlistgroup_init(&unused[i],unused_notify,NULL);
 assert(allocations==12&&mutexes==12&&events==12);
#else
 assert(!allocations&&mutexes==4&&events==EXPECTED_TIMER_EVENTS);
#endif
 printf("Clock setup with two AIO contexts: queue allocations=%u mutexes=%u events=%u\n",allocations,mutexes,events);
 for(unsigned type=0;type<QEMU_CLOCK_MAX;type++){now[type]=1000;qemu_clock_enable(type,true);enabled[type]=true;}
 for(unsigned i=0;i<N;i++){ref[i].type=i%QEMU_CLOCK_MAX;ref[i].attrs=(i%3?0:QEMU_TIMER_ATTR_EXTERNAL)|(i%7?0:4);ref[i].expiry=-1;initialize(&timers[i],ref[i].type,ref[i].attrs,record,(void*)(uintptr_t)i);timers[i].scale=i%2?SCALE_US:SCALE_NS;}
 for(unsigned step=0;step<20000;step++){
  unsigned id=random_value()%N,type=random_value()%QEMU_CLOCK_MAX,op=random_value()%8;
  int64_t expiry=(int64_t)(random_value()%7000)-20;
  switch(op){
  case 0:timer_mod_ns(&timers[id],expiry);ref_mod(id,expiry);break;
  case 1:timer_mod_anticipate_ns(&timers[id],expiry);if(ref[id].expiry<0||ref[id].expiry>expiry)ref_mod(id,expiry);break;
  case 2:timer_del(&timers[id]);ref[id].expiry=-1;break;
  case 3:now[type]+=random_value()%50;break;
  case 4:run(type);break;
  case 5:enabled[type]=!enabled[type];qemu_clock_enable(type,enabled[type]);break;
  case 6:counted=!counted;break;
  case 7:timer_mod(&timers[id],expiry);ref_mod(id,expiry*timers[id].scale);break;
  }
  check();
#ifdef TIMER_BASELINE
  for(unsigned i=0;i<2;i++){assert(timerlistgroup_deadline_ns(&unused[i])==-1);assert(!timerlistgroup_run_timers(&unused[i]));}
#endif
 }
 for(unsigned i=0;i<N;i++)timer_del(&timers[i]);
 qemu_clock_enable(0,true);now[0]=0;
 for(unsigned i=0;i<4;i++)initialize(&timers[i],0,i==0||i==3?QEMU_TIMER_ATTR_EXTERNAL:0,record,(void*)(uintptr_t)i);
 timer_mod_ns(&timers[0],10);timer_mod_ns(&timers[1],20);timer_mod_ns(&timers[2],20);timer_mod_ns(&timers[3],30);
 unsigned notifications=notifies[0];
 timer_mod_ns(&timers[0],15);assert(notifies[0]==notifications+1);
 assert(qemu_clock_deadline_ns_all(0,QEMU_TIMER_ATTR_ALL)==15);
 assert(qemu_clock_deadline_ns_all(0,~QEMU_TIMER_ATTR_EXTERNAL)==20);
 timer_mod_ns(&timers[1],20);timer_mod_ns(&timers[3],40);
 now[0]=100;actual_n=0;assert(qemu_clock_run_timers(0));
 assert(actual_n==4&&actual[0]==0&&actual[1]==2&&actual[2]==1&&actual[3]==3);
 for(unsigned i=0;i<4;i++){timer_del(&timers[i]);timer_del(&timers[i]);}
 now[0]=0;initialize(&timers[0],0,0,record,NULL);
 timer_mod_ns(&timers[0],10);timer_mod_ns(&timers[0],15);
 assert(qemu_clock_deadline_ns_all(0,QEMU_TIMER_ATTR_ALL)==15);
 assert(qemu_clock_deadline_ns_all(0,~QEMU_TIMER_ATTR_EXTERNAL)==15);
 timer_mod_ns(&timers[0],INT64_MAX);
 assert(qemu_clock_deadline_ns_all(0,QEMU_TIMER_ATTR_ALL)==INT64_MAX);
 timer_mod_ns(&timers[0],-1);assert(qemu_clock_deadline_ns_all(0,QEMU_TIMER_ATTR_ALL)==0);
 timer_del(&timers[0]);
 qemu_clock_enable(0,true);now[0]=10000;
 initialize(&a,0,0,special_a,NULL);initialize(&b,0,0,special_b,NULL);
 timer_mod_ns(&a,0);timer_mod_ns(&b,-1);timer_del(&b);
 assert(qemu_clock_deadline_ns_all(0,~QEMU_TIMER_ATTR_EXTERNAL)==0);timer_del(&a);
 assert(qemu_clock_deadline_ns_all(0,~QEMU_TIMER_ATTR_EXTERNAL)==-1);
 timer_mod_ns(&a,0);timer_mod_ns(&b,0);timer_del(&b);
 assert(qemu_clock_deadline_ns_all(0,~QEMU_TIMER_ATTR_EXTERNAL)==0);timer_del(&a);
 initialize(&a,0,0,special_a,NULL);initialize(&b,0,0,special_b,NULL);initialize(&c,0,0,special_c,NULL);
 timer_mod_ns(&a,now[0]);timer_mod_ns(&b,now[0]);assert(qemu_clock_run_timers(0));assert(special_n==3&&special[0]==1&&special[1]==1&&special[2]==3);
 QEMUTimer*self=calloc(1,sizeof(*self));assert(self);initialize(self,0,0,free_self,self);timer_mod_ns(self,now[0]);assert(qemu_clock_run_timers(0));assert(special_n==4&&special[3]==4);
#ifdef TIMER_BASELINE
 for(unsigned i=0;i<2;i++)timerlistgroup_deinit(&unused[i]);
 for(unsigned type=0;type<QEMU_CLOCK_MAX;type++)timerlist_free(main_loop_tlg.tl[type]);
#endif
 puts("Core timers: 20000 oracle steps, four clocks, attributes, deadlines, ordering, pause/resume, callback rearm/cancel and self-free PASS");
 return 0;
}
