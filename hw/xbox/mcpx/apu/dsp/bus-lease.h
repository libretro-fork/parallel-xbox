/* Private fail-closed lifetime gate. A lease spans the entire borrowed-pointer
 * lifetime. A DSP attempt never waits for a lease: it falls back synchronously.
 * New borrowers wait for an already published DSP operation to complete.
 * This deliberately conservative gate covers all RAM, not individual pages. */
#ifndef MCPX_BUS_LEASE_PROTOTYPE_H
#define MCPX_BUS_LEASE_PROTOTYPE_H
#include <assert.h>
#include <stdbool.h>
#include <limits.h>
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
typedef struct DSPBusLease {
    retro_atomic_int_t state; /* -1: DSP owns; >=0: live external borrowers */
    retro_eventcount_t changed;
} DSPBusLease;
static inline bool bus_lease_init(DSPBusLease *b)
{
    retro_atomic_int_init(&b->state, 0);
    return retro_eventcount_init(&b->changed);
}
static inline bool bus_lease_try_dsp(DSPBusLease *b)
{
    return retro_atomic_cas_int(&b->state, 0, -1);
}
static inline void bus_lease_dsp_done(DSPBusLease *b)
{
    assert(retro_atomic_load_relaxed_int(&b->state) == -1);
    retro_atomic_store_release_int(&b->state, 0);
    retro_eventcount_notify(&b->changed);
}
static inline void bus_lease_acquire(DSPBusLease *b)
{
    for (;;) {
        int n = retro_atomic_load_acquire_int(&b->state);
        if (n >= 0) {
            assert(n < INT_MAX);
            if (retro_atomic_cas_int(&b->state, n, n + 1)) return;
        } else {
            int key = retro_eventcount_prepare_wait(&b->changed);
            if (retro_atomic_load_acquire_int(&b->state) >= 0)
                retro_eventcount_cancel_wait(&b->changed);
            else retro_eventcount_commit_wait(&b->changed, key);
        }
    }
}
static inline void bus_lease_release(DSPBusLease *b)
{
    int old = retro_atomic_fetch_sub_int(&b->state, 1);
    assert(old > 0);
    /* A DSP never waits on borrowers, so no notify on this fast path. */
}
#endif
