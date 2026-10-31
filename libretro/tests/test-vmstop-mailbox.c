/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <retro_atomic.h>
#include <retro_timers.h>
#include <features/features_cpu.h>
#include <rthreads/rthreads.h>
#ifndef RETRO_ATOMIC_LOCK_FREE
#error "A real atomic backend is required"
#endif
/* ENUM */
static retro_atomic_int_t notifications,notifying,release_notify,gate,successes,done,ack;
static _Thread_local bool pause_notify;
static int payload;
static void await(retro_atomic_int_t *p,int value)
{
    int64_t deadline=cpu_features_get_time_usec()+20000000;
    while(retro_atomic_load_acquire_int(p)!=value){assert(cpu_features_get_time_usec()<deadline);retro_sleep(0);}
}
static void qemu_notify_event(void)
{
    if(pause_notify){retro_atomic_store_release_int(&notifying,1);await(&release_notify,1);}
    retro_atomic_fetch_add_int(&notifications,1);
}
/* PRODUCTION */
static void blocked_publisher(void *unused)
{
    pause_notify=true;
    qemu_system_vmstop_request(RUN_STATE_DEBUG);
}
static void take_once(void *unused)
{
    RunState reason;
    await(&gate,1);
    if(qemu_vmstop_requested(&reason)){
        assert(reason==RUN_STATE_PAUSED);
        retro_atomic_fetch_add_int(&successes,1);
    }else assert(reason==RUN_STATE__MAX);
}
#define PAYLOAD_ROUNDS 20000
static void publish_payload(void *unused)
{
    for(int i=1;i<=PAYLOAD_ROUNDS;i++){
        await(&ack,i-1);
        payload=i;
        qemu_system_vmstop_request(RUN_STATE_IO_ERROR);
        await(&ack,i);
    }
}
#define WRITERS 8
#define REQUESTS 10000
static void publish_many(void *opaque)
{
    unsigned id=(unsigned)(uintptr_t)opaque;
    await(&gate,1);
    for(unsigned i=0;i<REQUESTS;i++)qemu_system_vmstop_request((RunState)((i+id)%RUN_STATE__MAX));
    retro_atomic_fetch_add_int(&done,1);
}
int main(void)
{
    RunState reason;
    assert(!qemu_vmstop_requested(&reason)&&reason==RUN_STATE__MAX);
    for(int state=0;state<RUN_STATE__MAX;state++){
        qemu_system_vmstop_request((RunState)state);
        assert(qemu_vmstop_requested(&reason)&&reason==(RunState)state);
        assert(!qemu_vmstop_requested(&reason)&&reason==RUN_STATE__MAX);
    }
    qemu_system_vmstop_request(RUN_STATE_IO_ERROR);
    qemu_system_vmstop_request(RUN_STATE_PAUSED);
    assert(qemu_vmstop_requested(&reason)&&reason==RUN_STATE_PAUSED);
    assert(!qemu_vmstop_requested(&reason));

    sthread_t *paused=sthread_create(blocked_publisher,NULL);assert(paused);
    await(&notifying,1);
    assert(qemu_vmstop_requested(&reason)&&reason==RUN_STATE_DEBUG);
    qemu_system_vmstop_request(RUN_STATE_PAUSED);
    assert(qemu_vmstop_requested(&reason)&&reason==RUN_STATE_PAUSED);
    retro_atomic_store_release_int(&release_notify,1);sthread_join(paused);
    assert(!qemu_vmstop_requested(&reason));

    qemu_system_vmstop_request(RUN_STATE_PAUSED);
    sthread_t *readers[16];
    for(unsigned i=0;i<16;i++){readers[i]=sthread_create(take_once,NULL);assert(readers[i]);}
    retro_atomic_store_release_int(&gate,1);
    for(unsigned i=0;i<16;i++)sthread_join(readers[i]);
    assert(retro_atomic_load_relaxed_int(&successes)==1);

    sthread_t *writer=sthread_create(publish_payload,NULL);assert(writer);
    for(int i=1;i<=PAYLOAD_ROUNDS;i++){
        int64_t deadline=cpu_features_get_time_usec()+20000000;
        while(!qemu_vmstop_requested(&reason)){assert(cpu_features_get_time_usec()<deadline);retro_sleep(0);}
        assert(reason==RUN_STATE_IO_ERROR&&payload==i);
        retro_atomic_store_release_int(&ack,i);
    }
    sthread_join(writer);

    retro_atomic_store_relaxed_int(&gate,0);
    retro_atomic_store_relaxed_int(&notifications,0);
    sthread_t *writers[WRITERS];
    for(unsigned i=0;i<WRITERS;i++){writers[i]=sthread_create(publish_many,(void *)(uintptr_t)i);assert(writers[i]);}
    retro_atomic_store_release_int(&gate,1);
    do{
        bool present=qemu_vmstop_requested(&reason);
        assert(present?(unsigned)reason<RUN_STATE__MAX:reason==RUN_STATE__MAX);
    }while(retro_atomic_load_acquire_int(&done)!=WRITERS);
    for(unsigned i=0;i<WRITERS;i++)sthread_join(writers[i]);
    assert(retro_atomic_load_relaxed_int(&notifications)==WRITERS*REQUESTS);
    qemu_system_vmstop_request(RUN_STATE_PAUSED);
    assert(qemu_vmstop_requested(&reason)&&reason==RUN_STATE_PAUSED);
    assert(!qemu_vmstop_requested(&reason)&&reason==RUN_STATE__MAX);
    puts("VM-stop mailbox: every reason including zero, coalescing, delayed notification, single consumption, payload publication and 80000 concurrent requests PASS");
    return 0;
}
