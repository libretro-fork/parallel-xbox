/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <retro_atomic.h>
#include <retro_timers.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include "qemu/barrier.h"
#ifdef _WIN32
#define CONFIG_WIN32
#else
#include <unistd.h>
#include <pthread.h>
#ifdef __linux__
#define CONFIG_LINUX
#endif
#endif
/* FUTEX */
/* EVENT TYPE */
typedef struct QemuLockCnt QemuLockCnt;
/* GATE TYPE */
static void *hook_target;
static int hook_mode;
static retro_atomic_int_t hook_hit,hook_release;
static void await(retro_atomic_int_t *value)
{while(!retro_atomic_load_acquire_int(value))retro_sleep(0);}
static void test_park_hook(void *object,int phase)
{
    if(object==hook_target&&phase==hook_mode&&
       !retro_atomic_exchange_int(&hook_hit,1))await(&hook_release);
}
/* EVENT */
/* GATE */

static int64_t micros(void)
{
#ifdef _WIN32
    LARGE_INTEGER value,freq;QueryPerformanceCounter(&value);QueryPerformanceFrequency(&freq);
    return value.QuadPart*1000000/freq.QuadPart;
#else
    struct timespec value;clock_gettime(CLOCK_MONOTONIC,&value);
    return (int64_t)value.tv_sec*1000000+value.tv_nsec/1000;
#endif
}
static retro_atomic_int_t ready,finished;
static int payload;
static void wait_event(void *arg)
{
    retro_atomic_fetch_add_int(&ready,1);qemu_event_wait(arg);
    assert(payload==0x12345678);retro_atomic_fetch_add_int(&finished,1);
}
static void set_event(void *arg) {qemu_event_set(arg);}
static void reset_hooks(void *target,int mode)
{
    hook_target=target;hook_mode=mode;
    retro_atomic_store_relaxed_int(&hook_hit,0);retro_atomic_store_relaxed_int(&hook_release,0);
}
#ifndef PARKING_BASELINE
static retro_atomic_int_t pool_ready,pool_start;
typedef struct PoolArg {const void *key;retro_eventcount_t *result;} PoolArg;
static void pool_worker(void *arg)
{
    PoolArg *a=arg;retro_atomic_fetch_add_int(&pool_ready,1);await(&pool_start);
    a->result=qemu_eventcount_for_address(a->key);
}
static void test_shared_channels(void)
{
    QemuEvent events[QEMU_PARK_BUCKETS*2];sthread_t *threads[16];PoolArg args[16];
    for(unsigned i=0;i<QEMU_PARK_BUCKETS*2;i++)qemu_event_init(&events[i],false);
    for(unsigned i=0;i<16;i++) {
        args[i]=(PoolArg){&events[0],NULL};threads[i]=sthread_create(pool_worker,&args[i]);assert(threads[i]);
    }
    while(retro_atomic_load_acquire_int(&pool_ready)!=16)retro_sleep(0);
    retro_atomic_store_release_int(&pool_start,1);
    for(unsigned i=0;i<16;i++){sthread_join(threads[i]);assert(args[i].result==args[0].result);}
    unsigned collision=0;
    for(unsigned i=1;i<QEMU_PARK_BUCKETS*2;i++)
        if(qemu_eventcount_for_address(&events[i])==args[0].result){collision=i;break;}
    assert(collision);retro_atomic_store_relaxed_int(&ready,0);retro_atomic_store_relaxed_int(&finished,0);
    payload=0x12345678;
    threads[0]=sthread_create(wait_event,&events[0]);threads[1]=sthread_create(wait_event,&events[collision]);
    assert(threads[0]&&threads[1]);
    while(retro_atomic_load_acquire_int(&events[0].value)!=EV_BUSY||
          retro_atomic_load_acquire_int(&events[collision].value)!=EV_BUSY)retro_sleep(0);
    qemu_event_set(&events[0]);sthread_join(threads[0]);
    assert(retro_atomic_load_acquire_int(&finished)==1);
    qemu_event_set(&events[collision]);sthread_join(threads[1]);
    assert(retro_atomic_load_acquire_int(&finished)==2);
    for(unsigned i=0;i<QEMU_PARK_BUCKETS*2;i++)qemu_event_destroy(&events[i]);
}
#endif
static QemuEvent request,reply;
static int request_payload,reply_payload;
enum { HANDOFFS=20000, SPACED_HANDOFFS=256, GATE_LOOPS=5000 };
static void respond(void *unused)
{
    for(int i=1;i<=HANDOFFS+SPACED_HANDOFFS;i++) {
        qemu_event_wait(&request);qemu_event_reset(&request);
        assert(request_payload==i);reply_payload=i^0x56789;
        qemu_event_set(&reply);
    }
}
static QemuLockCnt gate;
static unsigned protected_count;
static void gate_writer(void *unused)
{
    for(unsigned i=0;i<GATE_LOOPS;i++) {
        qemu_lockcnt_lock(&gate);protected_count++;qemu_lockcnt_unlock(&gate);
    }
}
static void gate_reader(void *unused)
{
    for(unsigned i=0;i<GATE_LOOPS;i++) {
        qemu_lockcnt_inc(&gate);assert(qemu_lockcnt_count(&gate)>=1);qemu_lockcnt_dec(&gate);
    }
}
static void increment_once(void *unused) {qemu_lockcnt_inc(&gate);}
static int compare_latency(const void *a,const void *b)
{int64_t x=*(const int64_t *)a,y=*(const int64_t *)b;return (x>y)-(x<y);}
int main(void)
{
    retro_eventcount_t probe;assert(retro_eventcount_init(&probe));retro_eventcount_free(&probe);
#ifndef PARKING_BASELINE
    test_shared_channels();
#endif
    QemuEvent event;qemu_event_init(&event,true);qemu_event_wait(&event);qemu_event_set(&event);
    qemu_event_destroy(&event);
    int64_t start=micros();qemu_event_init(&event,false);
    for(unsigned i=0;i<1000000;i++){qemu_event_reset(&event);qemu_event_set(&event);qemu_event_wait(&event);}
    printf("event_uncontended_ns=%.2f event_bytes=%zu gate_bytes=%zu backend=%s\n",
           (micros()-start)*1000.0/1000000,sizeof(event),sizeof(gate),retro_eventcount_backend_name());
    qemu_event_destroy(&event);
    /* Notifications before registration, after registration and just before sleep. */
#ifdef PARKING_BASELINE
    int first_mode=3;
#else
    int first_mode=1;
#endif
    for(int phase=first_mode;phase<=3;phase++) {
        qemu_event_init(&event,false);reset_hooks(&event,phase);payload=0x12345678;
        sthread_t *thread=sthread_create(wait_event,&event);assert(thread);await(&hook_hit);
        qemu_event_set(&event);retro_atomic_store_release_int(&hook_release,1);sthread_join(thread);
        qemu_event_wait(&event);qemu_event_destroy(&event);
    }
    reset_hooks(NULL,0);
    /* Wake every waiter and publish payload, including early-set arrivals. */
    for(unsigned round=0;round<8;round++) {
        sthread_t *threads[16];qemu_event_init(&event,false);
        retro_atomic_store_relaxed_int(&ready,0);retro_atomic_store_relaxed_int(&finished,0);
        payload=0x12345678;
        for(unsigned i=0;i<16;i++){threads[i]=sthread_create(wait_event,&event);assert(threads[i]);}
        while(retro_atomic_load_acquire_int(&ready)!=16)retro_sleep(0);
        qemu_event_reset(&event);qemu_event_reset(&event);qemu_event_set(&event);
        for(unsigned i=0;i<16;i++)sthread_join(threads[i]);
        assert(retro_atomic_load_acquire_int(&finished)==16);qemu_event_destroy(&event);
    }
    /* A waiter may retire its event before the notifier finishes waking.
     * Force SET publication before the notifier touches its parking channel. */
    QemuEvent *retired=malloc(sizeof(*retired));assert(retired);qemu_event_init(retired,false);
    retro_atomic_store_relaxed_int(&retired->value,EV_BUSY);reset_hooks(retired,5);
    sthread_t *notifier=sthread_create(set_event,retired);assert(notifier);await(&hook_hit);
    qemu_event_wait(retired);qemu_event_destroy(retired);free(retired);
    retro_atomic_store_release_int(&hook_release,1);sthread_join(notifier);reset_hooks(NULL,0);
    qemu_event_init(&request,false);qemu_event_init(&reply,false);
    sthread_t *thread=sthread_create(respond,NULL);assert(thread);start=micros();
    for(int i=1;i<=HANDOFFS;i++) {
        qemu_event_reset(&reply);request_payload=i;qemu_event_set(&request);
        qemu_event_wait(&reply);assert(reply_payload==(i^0x56789));
    }
    int64_t hot_elapsed=micros()-start,latencies[SPACED_HANDOFFS];
    for(int i=1;i<=SPACED_HANDOFFS;i++) {
        qemu_event_reset(&reply);request_payload=HANDOFFS+i;retro_sleep(1);start=micros();
        qemu_event_set(&request);qemu_event_wait(&reply);
        latencies[i-1]=micros()-start;assert(reply_payload==((HANDOFFS+i)^0x56789));
    }
    sthread_join(thread);qsort(latencies,SPACED_HANDOFFS,sizeof(latencies[0]),compare_latency);
    printf("event_roundtrip_us=%.3f handoffs=%u spaced_p50_us=%lld spaced_p95_us=%lld\n",
           hot_elapsed/(double)HANDOFFS,HANDOFFS,(long long)latencies[SPACED_HANDOFFS/2],
           (long long)latencies[SPACED_HANDOFFS*95/100]);
    qemu_event_destroy(&request);qemu_event_destroy(&reply);
    qemu_lockcnt_init(&gate);qemu_lockcnt_inc(&gate);qemu_lockcnt_inc(&gate);
    assert(!qemu_lockcnt_dec_if_lock(&gate)&&qemu_lockcnt_count(&gate)==2);
    qemu_lockcnt_dec(&gate);assert(qemu_lockcnt_dec_if_lock(&gate));
    reset_hooks(&gate,4);thread=sthread_create(increment_once,NULL);assert(thread);await(&hook_hit);
    qemu_lockcnt_unlock(&gate);retro_atomic_store_release_int(&hook_release,1);sthread_join(thread);
    assert(qemu_lockcnt_count(&gate)==1);reset_hooks(NULL,0);
    assert(qemu_lockcnt_dec_if_lock(&gate));qemu_lockcnt_inc_and_unlock(&gate);
    assert(qemu_lockcnt_count(&gate)==1);qemu_lockcnt_dec(&gate);
    sthread_t *threads[16];
    for(unsigned i=0;i<16;i++){threads[i]=sthread_create(i<8?gate_writer:gate_reader,NULL);assert(threads[i]);}
    for(unsigned i=0;i<16;i++)sthread_join(threads[i]);
    assert(protected_count==8*GATE_LOOPS&&!qemu_lockcnt_count(&gate));qemu_lockcnt_destroy(&gate);
    puts("QEMU parking: manual reset, wake-all, predicate visibility, forced sleep races, handoffs and mixed AIO gate contention PASS");
    return 0;
}
