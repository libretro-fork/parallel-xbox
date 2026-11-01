/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <retro_atomic.h>

typedef int CPUState;
typedef struct {retro_atomic_int_t watch_flags;} MemAccessCallback;
static CPUState cpu;
static MemAccessCallback callback, *own_callback;
static bool present, self;
static unsigned lookups, checks, retires;
static CPUState *qemu_get_cpu(unsigned index)
{assert(index==0);lookups++;return present?&cpu:NULL;}
static bool qemu_cpu_is_self(CPUState *owner)
{assert(owner==&cpu);checks++;return self;}
static void lr_dsp_tlb_refresh(CPUState *owner,bool arm)
{assert(owner==&cpu && self && !arm);assert(!retro_atomic_load_relaxed_int(&callback.watch_flags));retires++;}
/* PRODUCTION RETIRE */
int main(void)
{
    /* No callback exists before setup or after teardown. The inactive path
     * must neither look up the CPU nor dereference a callback. */
    for(unsigned i=0;i<2000;i++)ownership_relax_if_cpu();
    assert(!lookups && !checks && !retires);
    own_callback=&callback;
    for(unsigned i=0;i<2000;i++){
        retro_atomic_store_relaxed_int(&callback.watch_flags,3);
        retro_atomic_store_relaxed_int(&ownership_guards_armed,1);
        present=false;ownership_relax_if_cpu();
        present=true;self=false;ownership_relax_if_cpu();
        assert(retires==i && retro_atomic_load_relaxed_int(&callback.watch_flags)==3);
        assert(retro_atomic_load_relaxed_int(&ownership_guards_armed));
        self=true;ownership_relax_if_cpu();
        assert(retires==i+1 && !retro_atomic_load_relaxed_int(&ownership_guards_armed));
        unsigned before=lookups;
        for(unsigned n=0;n<32;n++)ownership_relax_if_cpu();
        assert(lookups==before && retires==i+1);
    }
    own_callback=NULL;ownership_relax_if_cpu();
    assert(lookups==6000 && checks==4000 && retires==2000);
    puts("PASS: 2000 guard epochs retire once on CPU owner; missing/external CPUs preserve guards, and 66001 inactive checks perform no CPU lookup or callback access");
}
