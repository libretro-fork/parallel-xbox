
#include <stdio.h>
#include <stdint.h>
#include <rthreads/rthreads.h>
#include "hw/xbox/mcpx/apu/dsp/bus-lease.h"
static DSPBusLease gate;
static retro_atomic_int_t inside, finished, reads;
static uint64_t value;
static void borrower(void *unused)
{
    (void)unused;
    for (unsigned i = 0; i < 100000; i++) {
        bus_lease_acquire(&gate);
        retro_atomic_fetch_add_int(&inside, 1);
        uint64_t before = value;
        retro_atomic_fetch_add_int(&reads, 1);
        assert(before == value);
        retro_atomic_fetch_sub_int(&inside, 1);
        bus_lease_release(&gate);
    }
    retro_atomic_fetch_add_int(&finished, 1);
}
int main(void)
{
    assert(bus_lease_init(&gate));
    retro_atomic_int_init(&inside,0);retro_atomic_int_init(&finished,0);retro_atomic_int_init(&reads,0);
    /* A successful import/map pins its entire lifetime, including nesting. */
    bus_lease_acquire(&gate);bus_lease_acquire(&gate);
    assert(!bus_lease_try_dsp(&gate));bus_lease_release(&gate);
    assert(!bus_lease_try_dsp(&gate));bus_lease_release(&gate);
    assert(bus_lease_try_dsp(&gate));bus_lease_dsp_done(&gate);
    sthread_t *a = sthread_create(borrower,NULL), *b = sthread_create(borrower,NULL);
    assert(a && b);
    unsigned dsp = 0;
    while (retro_atomic_load_acquire_int(&finished) != 2 || dsp < 100000) {
        if (bus_lease_try_dsp(&gate)) {
            assert(retro_atomic_load_relaxed_int(&inside) == 0);
            value++; dsp++;
            bus_lease_dsp_done(&gate);
        }
    }
    sthread_join(a);sthread_join(b);
    assert(retro_atomic_load_relaxed_int(&gate.state) == 0);
    assert(retro_atomic_load_relaxed_int(&reads) == 200000);
    retro_eventcount_free(&gate.changed);
    printf("PASS: 200000 external borrow/return lifetimes racing %u DSP claims, nested/import pins and no shared-RAM data race\n",dsp);
    return 0;
}
