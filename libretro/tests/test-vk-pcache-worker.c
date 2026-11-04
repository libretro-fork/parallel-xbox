/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <retro_timers.h>
#include <rthreads/rthreads.h>
static unsigned starts;
static bool fail_start;
static sthread_t *test_create(void (*fn)(void *),void *arg) {
    starts++;return fail_start?NULL:sthread_create(fn,arg);
}
#define sthread_create test_create
#include "hw/xbox/nv2a/pgraph/vk/pcache-worker.h"
#undef sthread_create
static retro_atomic_int_t entered,release_save;
static retro_eventcount_t entered_event,release_event;
static unsigned saves;
static void wait_flag(retro_atomic_int_t *flag,retro_eventcount_t *event) {
    while(!retro_atomic_load_acquire_int(flag)) {
        int key=retro_eventcount_prepare_wait(event);
        if(retro_atomic_load_acquire_int(flag))retro_eventcount_cancel_wait(event);
        else retro_eventcount_commit_wait(event,key);
    }
}
typedef struct { VkPCacheWorker *pcache_worker;unsigned owner_cookie; } PGRAPHVkState;
typedef struct { PGRAPHVkState *vk_renderer_state; } PGRAPHState;
static void pcache_save_now(void *arg) {
    PGRAPHVkState *r=arg;assert(r->owner_cookie==0x12345678);
    if(!saves) {
        retro_atomic_store_release_int(&entered,1);retro_eventcount_notify(&entered_event);
        wait_flag(&release_save,&release_event);
    }
    saves++;
}
/* PRODUCTION */
int main(void) {
    assert(retro_eventcount_init(&entered_event)&&retro_eventcount_init(&release_event));
    retro_atomic_int_init(&entered,0);retro_atomic_int_init(&release_save,0);
    PGRAPHVkState r={.owner_cookie=0x12345678};PGRAPHState pg={&r};
    r.pcache_worker=vk_pcache_worker_create(pcache_save_now,&r);assert(r.pcache_worker&&starts==1);
    pgraph_vk_save_pipeline_cache(&pg,false);wait_flag(&entered,&entered_event);
    /* No writer progress is possible here. All marks must return immediately
     * and coalesce without creating threads, copying payloads or joining. */
    for(unsigned i=0;i<10000;i++)pgraph_vk_save_pipeline_cache(&pg,false);
    assert(starts==1&&saves==0&&retro_atomic_load_acquire_int(&r.pcache_worker->pending));
    retro_atomic_store_release_int(&release_save,1);retro_eventcount_notify(&release_event);
    while(retro_atomic_load_acquire_int(&r.pcache_worker->pending))retro_sleep(1);
    assert(saves==1);
    for(unsigned i=1;i<1000;i++) {
        pgraph_vk_save_pipeline_cache(&pg,false);
        while(retro_atomic_load_acquire_int(&r.pcache_worker->pending))retro_sleep(1);
        assert(saves==i+1&&starts==1);
    }
    pgraph_vk_save_pipeline_cache(&pg,false);
    pgraph_vk_save_pipeline_cache(&pg,true);assert(!r.pcache_worker&&saves==1002);
    for(unsigned i=0;i<32;i++) {
        r.pcache_worker=vk_pcache_worker_create(pcache_save_now,&r);assert(r.pcache_worker);
        pgraph_vk_save_pipeline_cache(&pg,false);pgraph_vk_save_pipeline_cache(&pg,true);
    }
    assert(saves==1066&&starts==33);
    fail_start=true;r.pcache_worker=vk_pcache_worker_create(pcache_save_now,&r);assert(!r.pcache_worker);
    pgraph_vk_save_pipeline_cache(&pg,false);pgraph_vk_save_pipeline_cache(&pg,true);assert(saves==1067);
    vk_pcache_worker_destroy(NULL);
    retro_eventcount_free(&entered_event);retro_eventcount_free(&release_event);
    puts("PASS:10000 held-writer marks,1000 serial saves/one thread,pending drain,32 reinitializations and startup failure fallback");
}
