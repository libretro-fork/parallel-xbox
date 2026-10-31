/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <features/features_cpu.h>
typedef struct {retro_atomic_int_t sequence;} QemuSeqLock;
typedef struct {unsigned depth;} QemuSpin;
typedef struct QEMUTimer QEMUTimer;
/* TIMERS */
TimersState timers_state;
static int64_t host_now;
static bool use_count;
static int64_t forwarded;
static unsigned publications;
static int64_t get_clock(void){return host_now;}
static int64_t cpu_get_host_ticks(void){return host_now*3+200;}
static bool icount_enabled(void){return use_count;}
static void icount_skip_to(int64_t vt){forwarded=vt;}
static void icount_publish_time(void){publications++;}
static __attribute__((unused)) void seqlock_write_lock(QemuSeqLock*s,QemuSpin*l){assert(!l->depth++);retro_atomic_fetch_add_int(&s->sequence,1);}
static __attribute__((unused)) void seqlock_write_unlock(QemuSeqLock*s,QemuSpin*l){retro_atomic_fetch_add_int(&s->sequence,1);assert(l->depth--==1);}
static __attribute__((unused)) unsigned seqlock_read_begin(QemuSeqLock*s){return (unsigned)retro_atomic_load_acquire_int(&s->sequence)&~1u;}
static __attribute__((unused)) bool seqlock_read_retry(QemuSeqLock*s,unsigned n){retro_atomic_thread_fence_acquire();return (unsigned)retro_atomic_load_relaxed_int(&s->sequence)!=n;}
/* PRODUCTION */
static uint32_t seed=0xaab39453;
static unsigned random32(void){seed^=seed<<13;seed^=seed>>17;return seed^=seed<<5;}
static void oracle(void)
{
 int64_t offset=0,ticks=0,saved_offset=0,saved_ticks=0;bool enabled=false;
 for(unsigned step=0;step<50000;step++){
  host_now+=random32()%1000;
  int64_t now=offset+(enabled?host_now:0);
  switch(random32()%6){
  case 0:
   if(!enabled){offset-=host_now;ticks-=cpu_get_host_ticks();enabled=true;}
   cpu_enable_ticks();break;
  case 1:
   if(enabled){offset+=host_now;ticks+=cpu_get_host_ticks();enabled=false;}
   cpu_disable_ticks();break;
  case 2:{
   int64_t vt=now+(int)(random32()%2000)-1000;
   if(vt>now)offset+=vt-now;
   cpu_clock_skip_to(vt);break;
  }
  case 3:
   if(enabled){offset+=host_now;ticks+=cpu_get_host_ticks();enabled=false;}
   cpu_disable_ticks();saved_offset=offset;saved_ticks=ticks;break;
  case 4:
   if(enabled){offset+=host_now;ticks+=cpu_get_host_ticks();enabled=false;}
   cpu_disable_ticks();offset=saved_offset;ticks=saved_ticks;
   timers_state.cpu_clock_offset=offset;timers_state.cpu_ticks_offset=ticks;break;
  case 5:{
   int64_t old=timers_state.cpu_clock_offset;use_count=true;forwarded=-1;
   cpu_clock_skip_to(98765);assert(forwarded==98765&&timers_state.cpu_clock_offset==old);use_count=false;break;
  }
  }
  assert(cpu_get_clock()==offset+(enabled?host_now:0));
  assert(cpu_ticks_enabled()==enabled);
  assert(timers_state.cpu_ticks_offset==ticks&&timers_state.cpu_clock_offset==offset);
 }
 assert(publications>0);
 puts("Owner elapsed clock: 50000 independent pause/resume/skip/save/restore steps and icount forwarding PASS");
}
static void benchmark(void)
{
 unsigned count=1000000;int64_t sum=0;host_now=987654;timers_state.cpu_clock_offset=123456;
 for(unsigned sample=0;sample<3;sample++){
  cpu_disable_ticks();uint64_t start=cpu_features_get_time_usec();
  for(unsigned i=0;i<count;i++)sum+=cpu_get_clock();
  uint64_t end=cpu_features_get_time_usec();printf("paused_ns=%.3f ",(end-start)*1000.0/count);
  cpu_enable_ticks();start=cpu_features_get_time_usec();
  for(unsigned i=0;i<count;i++)sum+=cpu_get_clock();
  end=cpu_features_get_time_usec();printf("running_ns=%.3f checksum=%lld\n",(end-start)*1000.0/count,(long long)sum);
 }
}
int main(int argc,char**argv){(void)argv;if(argc>1)benchmark();else oracle();return 0;}
