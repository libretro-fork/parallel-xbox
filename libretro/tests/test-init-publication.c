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
    assert(layout->func==source->func&&layout->name==source->name);
    assert(layout->typemask==source->typemask&&layout->flags==source->flags);
    assert(layout->nr_in==source->typemask%8&&layout->nr_out==source->flags%3);
    assert(tcg_resolve_call_layout(layout)==layout);
    for(unsigned i=0;i<sizeof(layout->in)/sizeof(layout->in[0]);i++) {
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

typedef void (*pSetThreadDescription)(void);
#ifndef _WIN32
typedef void *HMODULE;
#endif
#define GetModuleHandleA mock_GetModuleHandleA
#define GetProcAddress mock_GetProcAddress
static retro_atomic_int_t lookups, module_lookups, api_calls;
static int api_mode;
static void describe(void) { retro_atomic_fetch_add_int(&api_calls,1); }
static HMODULE GetModuleHandleA(const char *name)
{
    assert(!strcmp(name,"kernel32.dll"));
    retro_atomic_fetch_add_int(&module_lookups,1);
    return api_mode==2?NULL:(void *)&api_mode;
}
static pSetThreadDescription GetProcAddress(HMODULE module,const char *name)
{
    assert(module&& !strcmp(name,"SetThreadDescription"));
    int first=retro_atomic_fetch_add_int(&lookups,1);
    if(!first){retro_atomic_store_release_int(&blocked,1);await(&release_stalled);}
    return api_mode==0?describe:NULL;
}
/* WIN32 */
static void api_worker(void *arg)
{
    for(unsigned i=0;i<10000;i++) {
        pSetThreadDescription fn=load_set_thread_description();
        assert(fn==(api_mode==0?describe:NULL));
        if(fn)fn();
    }
}
int main(void)
{
    sthread_t *threads[16];
    stalled_helper.typemask=0x3344;stalled_helper.flags=7;
    threads[0]=sthread_create(stall_layout,NULL);assert(threads[0]);await(&blocked);
    /* Completion here proves independence from the paused initializer. */
    check(&stalled_helper,tcg_resolve_call_layout(&stalled_helper));
    retro_atomic_store_release_int(&release_stalled,1);sthread_join(threads[0]);
    for(unsigned i=0;i<128;i++){helpers[i].typemask=i;helpers[i].flags=i%200;helpers[i].name="helper";}
    for(unsigned i=0;i<16;i++){threads[i]=sthread_create(layout_worker,(void *)(uintptr_t)i);assert(threads[i]);}
    for(unsigned i=0;i<16;i++)sthread_join(threads[i]);
    assert(retro_atomic_load_acquire_int(&allocations)-retro_atomic_load_acquire_int(&frees)==129);
    for(unsigned i=0;i<128;i++)discard(retro_atomic_load_acquire_ptr(&helpers[i].layout));
    discard(retro_atomic_load_acquire_ptr(&stalled_helper.layout));
    assert(retro_atomic_load_acquire_int(&allocations)==retro_atomic_load_acquire_int(&frees));
    for(api_mode=0;api_mode<3;api_mode++) {
        retro_atomic_store_relaxed_ptr(&thread_description_func,NULL);
        retro_atomic_store_relaxed_int(&lookups,0);retro_atomic_store_relaxed_int(&module_lookups,0);
        retro_atomic_store_relaxed_int(&blocked,0);retro_atomic_store_relaxed_int(&release_stalled,0);
        if(api_mode!=2) {
            threads[0]=sthread_create(api_worker,NULL);assert(threads[0]);await(&blocked);
            assert(load_set_thread_description()==(api_mode==0?describe:NULL));
            retro_atomic_store_release_int(&release_stalled,1);sthread_join(threads[0]);
        }
        for(unsigned i=0;i<16;i++){threads[i]=sthread_create(api_worker,NULL);assert(threads[i]);}
        for(unsigned i=0;i<16;i++)sthread_join(threads[i]);
        int before=retro_atomic_load_acquire_int(&module_lookups);
        for(unsigned i=0;i<10000;i++)assert(load_set_thread_description()==(api_mode==0?describe:NULL));
        assert(retro_atomic_load_acquire_int(&module_lookups)==before);
        if(api_mode==2)assert(!retro_atomic_load_acquire_int(&lookups));
    }
    puts("Initialization publication: stalled-initializer independence, canonical layouts, reclamation and optional Windows API caching PASS");
    return 0;
}
