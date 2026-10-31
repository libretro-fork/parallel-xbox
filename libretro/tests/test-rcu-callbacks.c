/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <retro_atomic.h>
#include <retro_mpsc.h>
#include <retro_timers.h>
#include <rthreads/retro_eventcount.h>
#include <rthreads/rthreads.h>
#define container_of(p,t,f) ((t *)((char *)(p)-offsetof(t,f)))
struct rcu_head;
typedef void RCUCBFunc(struct rcu_head *);
/* HEAD */
typedef struct { retro_atomic_int_t set; retro_eventcount_t event; } QemuEvent;
static void qemu_event_reset(QemuEvent *e) { retro_atomic_store_release_int(&e->set,0); }
static void qemu_event_set(QemuEvent *e) { retro_atomic_store_release_int(&e->set,1);retro_eventcount_notify(&e->event); }
static void qemu_event_wait(QemuEvent *e) {
 while(!retro_atomic_load_acquire_int(&e->set)) {
  int key=retro_eventcount_prepare_wait(&e->event);
  if(retro_atomic_load_acquire_int(&e->set))retro_eventcount_cancel_wait(&e->event);
  else retro_eventcount_commit_wait(&e->event,key);
 }
}
static retro_atomic_int_t stop,period,release_period,completed,registered;
static void await(retro_atomic_int_t *value,int n) {
 for(unsigned i=0;i<10000000;i++) {
  if(retro_atomic_load_acquire_int(value)>=n)return;
  retro_sleep(0);
 }
 abort();
}
static void rcu_register_thread(void) {retro_atomic_inc_int(&registered);}
static void bql_lock(void) {}
static void bql_unlock(void) {}
static void synchronize_rcu(void) {
 int n=retro_atomic_fetch_add_int(&period,1)+1;
 if(n<=2)await(&release_period,n);
}
/* CALLBACKS */
typedef struct {
 struct rcu_head rcu;
 unsigned producer,sequence,payload;
} Job;
enum { PRODUCERS=8, JOBS=25000 };
static unsigned sequences[PRODUCERS];
static void callback(struct rcu_head *head) {
 Job *job=container_of(head,Job,rcu);
 assert(job->payload==(job->sequence^0x5aaf17c3u));
 if(job->producer<PRODUCERS)assert(sequences[job->producer]++==job->sequence);
 else assert(retro_atomic_load_acquire_int(&period)>=(int)job->sequence);
 free(job);
 retro_atomic_inc_int(&completed);
}
static void publish(unsigned producer,unsigned sequence) {
 Job *job=malloc(sizeof(*job));assert(job);
 job->producer=producer;job->sequence=sequence;job->payload=sequence^0x5aaf17c3u;
 call_rcu1(&job->rcu,callback);
}
static void producer(void *opaque) {
 unsigned id=(unsigned)(uintptr_t)opaque;
 for(unsigned i=0;i<JOBS;i++)publish(id,i);
}
int main(void) {
 sthread_t *workers[16],*producers[PRODUCERS];
 assert(retro_eventcount_init(&rcu_call_ready_event.event));
 publish(PRODUCERS,1);
 for(unsigned i=0;i<16;i++)workers[i]=sthread_create(call_rcu_thread,NULL);
 await(&period,1);
 assert(!retro_atomic_load_acquire_int(&completed));
 publish(PRODUCERS,2); /* Must not share the already detached batch's period. */
 retro_atomic_store_release_int(&release_period,1);
 await(&period,2);await(&completed,1);
 assert(retro_atomic_load_acquire_int(&completed)==1);
 retro_atomic_store_release_int(&release_period,2);await(&completed,2);
 for(unsigned i=0;i<PRODUCERS;i++)producers[i]=sthread_create(producer,(void *)(uintptr_t)i);
 for(unsigned i=0;i<PRODUCERS;i++)sthread_join(producers[i]);
 await(&completed,2+PRODUCERS*JOBS);
 retro_atomic_store_release_int(&stop,1);qemu_event_set(&rcu_call_ready_event);
 for(unsigned i=0;i<16;i++)sthread_join(workers[i]);
 assert(retro_atomic_load_acquire_int(&registered)==1);
 assert(retro_mpsc_empty(&rcu_callbacks));
 retro_eventcount_free(&rcu_call_ready_event.event);
 puts("RCU callbacks: acquired batches, separate grace periods, 200000 FIFO callbacks and sole consumer PASS");
 return 0;
}
