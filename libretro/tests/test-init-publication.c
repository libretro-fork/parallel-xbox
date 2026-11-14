/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include <retro_timers.h>
#include <rthreads/rthreads.h>
typedef struct TCGHelperInfo TCGHelperInfo;
#include "tcg/helper-info.h"
static retro_atomic_int_t allocations, frees, stalled_calls, blocked, release_stalled;
static void *allocate(size_t size)
{ void *p=calloc(1,size);assert(p);retro_atomic_fetch_add_int(&allocations,1);return p; }
static void discard(void *p)
{ if(p){retro_atomic_fetch_add_int(&frees,1);free(p);} }
#define g_new0(type,n) ((type *)allocate(sizeof(type)*(n)))
#define g_free discard
static void await(retro_atomic_int_t *value)
{ while(!retro_atomic_load_acquire_int(value))retro_sleep(0); }
static void init_call_layout(TCGHelperInfo *info)
{
    if(info->typemask==0x3344 && !retro_atomic_fetch_add_int(&stalled_calls,1)) {
        retro_atomic_store_release_int(&blocked,1);await(&release_stalled);
    }
    info->nr_in=info->typemask%8;info->nr_out=info->flags%3;
    info->out_kind=TCG_CALL_RET_NORMAL;
    for(unsigned i=0;i<sizeof(info->in)/sizeof(info->in[0]);i++) {
        info->in[i].kind=TCG_CALL_ARG_NORMAL;
        info->in[i].arg_slot=(info->typemask+i)%200;
        info->in[i].ref_slot=(info->flags+i)%200;
        info->in[i].arg_idx=i%8;info->in[i].tmp_subindex=i%4;
    }
}
/* TCG */
static TCGHelperInfo helpers[128], stalled_helper;
static void check(TCGHelperInfo *source,TCGHelperInfo *layout)
{
    assert(layout==retro_atomic_load_acquire_ptr(&source->layout));
    assert(layout->func==source->func);
    assert(layout->typemask==source->typemask&&layout->flags==source->flags);
    assert(layout->nr_in==source->typemask%8&&layout->nr_out==source->flags%3);
    assert(layout->out_kind==TCG_CALL_RET_NORMAL);
    assert(tcg_resolve_call_layout(layout)==layout);
    for(unsigned i=0;i<sizeof(layout->in)/sizeof(layout->in[0]);i++) {
        assert(layout->in[i].kind==TCG_CALL_ARG_NORMAL);
        assert(layout->in[i].arg_idx==i%8&&layout->in[i].tmp_subindex==i%4);
        assert(layout->in[i].arg_slot==(source->typemask+i)%200);
        assert(layout->in[i].ref_slot==(source->flags+i)%200);
    }
}
static void stall_layout(void *arg) { check(&stalled_helper,tcg_resolve_call_layout(&stalled_helper)); }
static void layout_worker(void *arg)
{
    for(unsigned repeat=0;repeat<128;repeat++)for(unsigned i=0;i<128;i++) {
        unsigned n=(i+(unsigned)(uintptr_t)arg)%128;
        check(&helpers[n],tcg_resolve_call_layout(&helpers[n]));
    }
}

int main(void)
{
    sthread_t *threads[16];
    stalled_helper.func=&stalled_helper;stalled_helper.typemask=0x3344;stalled_helper.flags=7;
    threads[0]=sthread_create(stall_layout,NULL);assert(threads[0]);await(&blocked);
    /* Completion here proves independence from the paused initializer. */
    check(&stalled_helper,tcg_resolve_call_layout(&stalled_helper));
    retro_atomic_store_release_int(&release_stalled,1);sthread_join(threads[0]);
    for(unsigned i=0;i<128;i++){helpers[i].typemask=i;helpers[i].flags=i%200;helpers[i].func=&helpers[i];}
    for(unsigned i=0;i<16;i++){threads[i]=sthread_create(layout_worker,(void *)(uintptr_t)i);assert(threads[i]);}
    for(unsigned i=0;i<16;i++)sthread_join(threads[i]);
    assert(retro_atomic_load_acquire_int(&allocations)-retro_atomic_load_acquire_int(&frees)==129);
    for(unsigned i=0;i<128;i++)discard(retro_atomic_load_acquire_ptr(&helpers[i].layout));
    discard(retro_atomic_load_acquire_ptr(&stalled_helper.layout));
    assert(retro_atomic_load_acquire_int(&allocations)==retro_atomic_load_acquire_int(&frees));
    puts("Initialization publication: stalled-initializer independence, canonical layouts, reclamation PASS");
    return 0;
}
