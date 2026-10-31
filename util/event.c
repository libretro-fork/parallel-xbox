/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "qemu/osdep.h"
#include "qemu/thread.h"
#include "qemu/barrier.h"
#include "qemu-event-internal.h"

/* Manual-reset predicate with SET -> FREE -> BUSY transitions. Reset never
 * changes BUSY to FREE, so a concurrent reset cannot erase waiter presence.
 * Eventcount registration and the predicate recheck close the sleep race. */
/* A signal may publish SET and then be descheduled while its waiter retires
 * the event. Wake operations therefore must not touch the event afterward.
 * Bounded, process-lifetime channels preserve futex-style address-key lifetime;
 * hash collisions merely cause spurious wakes and predicates are rechecked. */
#define QEMU_PARK_BUCKETS 64
static retro_atomic_ptr_t qemu_park_channels[QEMU_PARK_BUCKETS];

static inline retro_atomic_ptr_t *qemu_park_slot(const void *address)
{
    uintptr_t key = (uintptr_t)address;
    return &qemu_park_channels[((key >> 3) ^ (key >> 11) ^ (key >> 19)) &
                              (QEMU_PARK_BUCKETS - 1)];
}

static inline retro_eventcount_t *qemu_park_channel(const void *address)
{
    retro_atomic_ptr_t *slot = qemu_park_slot(address);
    retro_eventcount_t *channel = retro_atomic_load_acquire_ptr(slot);
    if (!channel) {
        retro_eventcount_t *candidate = calloc(1, sizeof(*candidate));
        if (!candidate || !retro_eventcount_init(candidate)) {
            retro_eventcount_free(candidate);
            free(candidate);
            abort();
        }
        if (retro_atomic_cas_ptr(slot, NULL, candidate)) {
            channel = candidate;
        } else {
            retro_eventcount_free(candidate);
            free(candidate);
            channel = retro_atomic_load_acquire_ptr(slot);
        }
    }
    return channel;
}

retro_eventcount_t *qemu_eventcount_for_address(const void *address)
{
    return qemu_park_channel(address);
}

#ifdef CONFIG_POSIX
void qemu_eventcount_after_fork(void)
{
    /* Parent waiters and their stack links do not survive in this process.
     * Do not destroy inherited parking primitives: a vanished worker may
     * have held one. Reinitialize them before starting child workers, as for
     * RCU's own eventcounts, retaining the bounded channel allocations. */
    for (unsigned i = 0; i < QEMU_PARK_BUCKETS; i++) {
        retro_eventcount_t *channel =
            retro_atomic_load_acquire_ptr(&qemu_park_channels[i]);
        if (channel && !retro_eventcount_init(channel)) {
            abort();
        }
    }
}
#endif

#define EV_SET   0
#define EV_FREE  1
#define EV_BUSY -1

void qemu_event_init(QemuEvent *ev, bool init)
{
    retro_atomic_int_init(&ev->value, init ? EV_SET : EV_FREE);
    ev->initialized = true;
}

void qemu_event_destroy(QemuEvent *ev)
{
    assert(ev->initialized);
    ev->initialized = false;
}

void qemu_event_set(QemuEvent *ev)
{
    assert(ev->initialized);
    /* Preserve the full ordering paired with reset's external predicate
     * check, including when the event was already SET. */
    retro_atomic_thread_fence_seq_cst();
    if (retro_atomic_load_relaxed_int(&ev->value) != EV_SET) {
        retro_eventcount_t *waiters = qemu_park_channel(ev);
        int old = retro_atomic_exchange_int(&ev->value, EV_SET);
        qemu_mb_after_rmw();
        if (old == EV_BUSY) {
            retro_eventcount_notify(waiters);
        }
    }
}

void qemu_event_reset(QemuEvent *ev)
{
    assert(ev->initialized);
    retro_atomic_fetch_or_int(&ev->value, EV_FREE);
    qemu_mb_after_rmw();
}

void qemu_event_wait(QemuEvent *ev)
{
    assert(ev->initialized);
    if (retro_atomic_load_acquire_int(&ev->value) == EV_SET) {
        return;
    }
    retro_eventcount_t *waiters = qemu_park_channel(ev);
    for (;;) {
        int value = retro_atomic_load_acquire_int(&ev->value);
        if (value == EV_SET) {
            return;
        }
        if (value == EV_FREE &&
            !retro_atomic_cas_int(&ev->value, EV_FREE, EV_BUSY) &&
            retro_atomic_load_acquire_int(&ev->value) == EV_SET) {
            return;
        }
        int key = retro_eventcount_prepare_wait(waiters);
        if (retro_atomic_load_acquire_int(&ev->value) != EV_BUSY) {
            retro_eventcount_cancel_wait(waiters);
        } else {
            retro_eventcount_commit_wait(waiters, key);
        }
    }
}
