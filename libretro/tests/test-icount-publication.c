/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <features/features_cpu.h>
#define LR_ICOUNT_NS_NUM 1977
#define LR_ICOUNT_NS_DEN 1000
#define unlikely(x) (x)
typedef struct { retro_atomic_int_t sequence; } QemuSeqLock;
typedef struct { unsigned depth; } QemuSpin;
typedef struct QEMUTimer QEMUTimer;
/* TIMERS */
TimersState timers_state;
typedef struct CPUState {
 int64_t icount_budget,icount_extra;
 retro_atomic_int_t running;
 struct { bool can_do_io;struct {struct {retro_atomic_u16_t low;}u16;}icount_decr; } neg;
} CPUState;
static sthread_tls_t cpu_key;
#define current_cpu ((CPUState *)sthread_tls_get(&cpu_key))
static void set_cpu(CPUState *cpu){assert(sthread_tls_set(&cpu_key,cpu));}
static void seqlock_write_lock(QemuSeqLock *s,QemuSpin *lock){assert(!lock->depth++);retro_atomic_fetch_add_int(&s->sequence,1);}
static void seqlock_write_unlock(QemuSeqLock *s,QemuSpin *lock){retro_atomic_fetch_add_int(&s->sequence,1);assert(lock->depth--==1);}
static __attribute__((unused)) unsigned seqlock_read_begin(QemuSeqLock *s){return (unsigned)retro_atomic_load_acquire_int(&s->sequence)&~1u;}
static __attribute__((unused)) bool seqlock_read_retry(QemuSeqLock*s,unsigned start){retro_atomic_thread_fence_acquire();return (unsigned)retro_atomic_load_relaxed_int(&s->sequence)!=start;}
#define TS_LOAD64(f) retro_atomic_load_relaxed_64((retro_atomic_64_t *)&timers_state.f)
#define TS_STORE64(f,v) retro_atomic_store_relaxed_64((retro_atomic_64_t *)&timers_state.f,(v))
static int64_t cpu_get_host_ticks(void){return 200;}
static int64_t get_clock(void){return 100;}
int64_t icount_to_ns(int64_t);
void icount_publish_time(void);
/* PRODUCTION */
#ifdef ICOUNT_BASELINE
void icount_publish_time(void){}
#endif
static int64_t reference(int64_t count,int64_t bias){return bias+count*1977/1000;}
static void restore(int64_t count,int64_t bias)
{
 TS_STORE64(qemu_icount,count);TS_STORE64(qemu_icount_bias,bias);
#ifndef ICOUNT_BASELINE
 assert(!cpu_timers_post_load(&timers_state,2));
#endif
}
static void owner_cases(void)
{
 CPUState cpu={0};restore(1000,100);set_cpu(&cpu);
 retro_atomic_store_release_int(&cpu.running,1);cpu.neg.can_do_io=true;
 cpu.icount_budget=100;retro_atomic_store_relaxed_u16(&cpu.neg.icount_decr.u16.low,75);
 assert(icount_get()==reference(1025,100));assert(cpu.icount_budget==75);
 assert(icount_get()==reference(1025,100));
 cpu.neg.can_do_io=false;retro_atomic_store_relaxed_u16(&cpu.neg.icount_decr.u16.low,50);
 assert(icount_get()==reference(1025,100));assert(cpu.icount_budget==75);
 cpu.neg.can_do_io=true;icount_update(&cpu);assert(icount_get()==reference(1050,100));
 set_cpu(NULL);icount_skip_to(9000);assert(icount_get()==9000);
 icount_skip_to(8000);assert(icount_get()==9000);
 restore(100,7);assert(icount_get()==reference(100,7));
 set_cpu(&cpu);cpu.icount_budget=10000;retro_atomic_store_relaxed_u16(&cpu.neg.icount_decr.u16.low,10000);
 for(unsigned i=0;i<10000;i++){
  retro_atomic_store_relaxed_u16(&cpu.neg.icount_decr.u16.low,9999-i);
  assert(icount_get()==reference(101+i,7));
 }
 set_cpu(NULL);
 /* A failed/partial load must be republished before execution resumes. */
 TS_STORE64(qemu_icount,300);TS_STORE64(qemu_icount_bias,9);
 cpu_enable_ticks();assert(icount_get()==reference(300,9));
 restore(1000,100);
}
#ifndef ICOUNT_BASELINE
static retro_atomic_int_t pause_ready,release_writer,stop_readers,started;
#define A ((1ll<<34)+7)
#define B ((1ll<<35)+9)
static void paused_writer(void *unused)
{
 (void)unused;seqlock_write_lock(&timers_state.vm_clock_seqlock,&timers_state.vm_clock_lock);
 TS_STORE64(qemu_icount,9000); /* Deliberately inconsistent raw fields. */
 retro_atomic_store_release_int(&pause_ready,1);
 while(!retro_atomic_load_acquire_int(&release_writer)){}
 TS_STORE64(qemu_icount_bias,B-icount_to_ns(9000));icount_publish_time();
 seqlock_write_unlock(&timers_state.vm_clock_seqlock,&timers_state.vm_clock_lock);
}
static void reader(void *unused)
{
 (void)unused;retro_atomic_fetch_add_int(&started,1);
 while(!retro_atomic_load_acquire_int(&stop_readers)){
  int64_t value=icount_get();assert(value==A||value==B);
 }
}
static void concurrency(void)
{
 restore(1000,A-icount_to_ns(1000));
 sthread_t *writer=sthread_create(paused_writer,NULL);assert(writer);
 while(!retro_atomic_load_acquire_int(&pause_ready)){}
 for(unsigned i=0;i<10000;i++)assert(icount_get()==A);
 retro_atomic_store_release_int(&release_writer,1);sthread_join(writer);assert(icount_get()==B);
 sthread_t *readers[6];for(unsigned i=0;i<6;i++){readers[i]=sthread_create(reader,NULL);assert(readers[i]);}
 while(retro_atomic_load_acquire_int(&started)<6){}
 for(unsigned i=0;i<100000;i++){
  int64_t count=(i&1)?1000:9000,value=(i&1)?A:B;
  seqlock_write_lock(&timers_state.vm_clock_seqlock,&timers_state.vm_clock_lock);
  TS_STORE64(qemu_icount,count);TS_STORE64(qemu_icount_bias,value-icount_to_ns(count));icount_publish_time();
  seqlock_write_unlock(&timers_state.vm_clock_seqlock,&timers_state.vm_clock_lock);
 }
 retro_atomic_store_release_int(&stop_readers,1);for(unsigned i=0;i<6;i++)sthread_join(readers[i]);
}
#endif
static void benchmark(void)
{
 CPUState cpu={0};restore(1000,100);unsigned count=1000000;
 retro_atomic_store_release_int(&cpu.running,1);cpu.neg.can_do_io=true;
 for(unsigned sample=0;sample<3;sample++){
  set_cpu(NULL);int64_t sum=0;uint64_t start=cpu_features_get_time_usec();
  for(unsigned i=0;i<count;i++)sum+=icount_get();
  uint64_t end=cpu_features_get_time_usec();printf("observer_ns=%.3f ",(end-start)*1000.0/count);
  set_cpu(&cpu);cpu.icount_budget=100;retro_atomic_store_relaxed_u16(&cpu.neg.icount_decr.u16.low,100);
  start=cpu_features_get_time_usec();for(unsigned i=0;i<count;i++)sum+=icount_get();
  end=cpu_features_get_time_usec();printf("owner_idle_ns=%.3f ",(end-start)*1000.0/count);
  start=cpu_features_get_time_usec();for(unsigned i=0;i<count;i++){
   cpu.icount_budget=100;retro_atomic_store_relaxed_u16(&cpu.neg.icount_decr.u16.low,99);icount_update(&cpu);
  }
  end=cpu_features_get_time_usec();printf("update_ns=%.3f checksum=%lld\n",(end-start)*1000.0/count,(long long)sum);
 }
 set_cpu(NULL);
}
int main(int argc,char **argv)
{
 (void)argv;assert(sthread_tls_create(&cpu_key));
 if(argc>1){benchmark();return 0;}
 owner_cases();
#ifndef ICOUNT_BASELINE
 concurrency();
#endif
 assert(sthread_tls_delete(&cpu_key));
#ifdef ICOUNT_BASELINE
 puts("Instruction clock baseline: owner accounting, conversion, I/O boundaries, warp and restore PASS");
#else
 puts("Instruction clock: owner accounting, fractional conversion, I/O boundaries, warp, restore, stalled writer and six snapshot readers PASS");
#endif
 return 0;
}
