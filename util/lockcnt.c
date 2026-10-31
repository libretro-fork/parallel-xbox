/*
 * QemuLockCnt implementation
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Author:
 *   Paolo Bonzini <pbonzini@redhat.com>
 */
#include "qemu/osdep.h"
#include "qemu/lockcnt.h"
#include "qemu/thread.h"
#include "trace.h"

/*
 * Bits 0-1 retain the AIO exclusion gate, bits 2-31 are the
 * counter.
 * For the mutex algorithm see Ulrich Drepper's "Futexes Are Tricky" (ok,
 * this is not the most relaxing citation I could make...).  It is similar
 * to mutex2 in the paper.
 */

#define QEMU_LOCKCNT_STATE_MASK    3
#define QEMU_LOCKCNT_STATE_FREE    0   /* free, uncontended */
#define QEMU_LOCKCNT_STATE_LOCKED  1   /* locked, uncontended */
#define QEMU_LOCKCNT_STATE_WAITING 2   /* locked, contended */

#define QEMU_LOCKCNT_COUNT_STEP    4
#define QEMU_LOCKCNT_COUNT_SHIFT   2

/*
 * Compare-and-swap that returns the value it found, as QEMU's cmpxchg did:
 * @expected on success, otherwise a value that differs from it.
 */
static inline int lockcnt_cmpxchg(QemuLockCnt *lockcnt, int expected, int new)
{
    int cur;

    do {
        if (retro_atomic_cas_int(&lockcnt->count, expected, new)) {
            return expected;
        }
        cur = retro_atomic_load_acquire_int(&lockcnt->count);
    } while (cur == expected);
    return cur;
}


void qemu_lockcnt_init(QemuLockCnt *lockcnt)
{
    retro_atomic_int_init(&lockcnt->count, 0);
}

void qemu_lockcnt_destroy(QemuLockCnt *lockcnt)
{
}

/* *val is the current value of lockcnt->count.
 *
 * If the lock is free, try a cmpxchg from *val to new_if_free; return
 * true and set *val to the old value found by the cmpxchg in
 * lockcnt->count.
 *
 * If the lock is taken, wait for it to be released and return false
 * *without trying again to take the lock*.  Again, set *val to the
 * new value of lockcnt->count.
 *
 * If *waited is true on return, new_if_free's bottom two bits must not
 * be QEMU_LOCKCNT_STATE_LOCKED on subsequent calls, because the caller
 * does not know if there are other waiters.  Furthermore, after *waited
 * is set the caller has effectively acquired the lock.  If it returns
 * with the lock not taken, it must wake other eventcount waiters.
 */
static bool qemu_lockcnt_cmpxchg_or_wait(QemuLockCnt *lockcnt, int *val,
                                         int new_if_free, bool *waited,
                                         retro_eventcount_t **waiters)
{
    /* Fast path for when the lock is free.  */
    if ((*val & QEMU_LOCKCNT_STATE_MASK) == QEMU_LOCKCNT_STATE_FREE) {
        int expected = *val;

        trace_lockcnt_fast_path_attempt(lockcnt, expected, new_if_free);
        *val = lockcnt_cmpxchg(lockcnt, expected, new_if_free);
        if (*val == expected) {
            trace_lockcnt_fast_path_success(lockcnt, expected, new_if_free);
            *val = new_if_free;
            return true;
        }
    }

    /* The slow path moves from locked to waiting if necessary, then
     * does an eventcount wait.  Both steps can be repeated ad nauseam,
     * only getting out of the loop if we can have another shot at the
     * fast path.  Once we can, get out to compute the new destination
     * value for the fast path.
     */
    while ((*val & QEMU_LOCKCNT_STATE_MASK) != QEMU_LOCKCNT_STATE_FREE) {
        if ((*val & QEMU_LOCKCNT_STATE_MASK) == QEMU_LOCKCNT_STATE_LOCKED) {
            int expected = *val;
            int new = expected - QEMU_LOCKCNT_STATE_LOCKED + QEMU_LOCKCNT_STATE_WAITING;

            trace_lockcnt_futex_wait_prepare(lockcnt, expected, new);
            *val = lockcnt_cmpxchg(lockcnt, expected, new);
            if (*val == expected) {
                *val = new;
            }
            continue;
        }

        if ((*val & QEMU_LOCKCNT_STATE_MASK) == QEMU_LOCKCNT_STATE_WAITING) {
            *waited = true;
            trace_lockcnt_futex_wait(lockcnt, *val);
            if (!*waiters) {
                *waiters = qemu_eventcount_for_address(lockcnt);
            }
            int key = retro_eventcount_prepare_wait(*waiters);
            if (retro_atomic_load_acquire_int(&lockcnt->count) != *val) {
                retro_eventcount_cancel_wait(*waiters);
            } else {
                retro_eventcount_commit_wait(*waiters, key);
            }
            *val = retro_atomic_load_relaxed_int(&lockcnt->count);
            trace_lockcnt_futex_wait_resume(lockcnt, *val);
            continue;
        }

        abort();
    }
    return false;
}

static void lockcnt_wake(retro_eventcount_t *waiters)
{
    trace_lockcnt_futex_wake(waiters);
    retro_eventcount_notify(waiters);
}

void qemu_lockcnt_inc(QemuLockCnt *lockcnt)
{
    int val = retro_atomic_load_relaxed_int(&lockcnt->count);
    bool waited = false;
    retro_eventcount_t *waiters = NULL;

    for (;;) {
        if (val >= QEMU_LOCKCNT_COUNT_STEP) {
            int expected = val;
            val = lockcnt_cmpxchg(lockcnt, val,
                                  val + QEMU_LOCKCNT_COUNT_STEP);
            if (val == expected) {
                break;
            }
        } else {
            /* The fast path is (0, unlocked)->(1, unlocked).  */
            if (qemu_lockcnt_cmpxchg_or_wait(lockcnt, &val, QEMU_LOCKCNT_COUNT_STEP,
                                             &waited, &waiters)) {
                break;
            }
        }
    }

    /* If we were woken by another thread, we should also wake one because
     * we are effectively releasing the lock that was given to us.  This is
     * the case where qemu_lockcnt_lock would leave QEMU_LOCKCNT_STATE_WAITING
     * in the low bits, and qemu_lockcnt_inc_and_unlock would find it and
     * wake someone.
     */
    if (waited) {
        lockcnt_wake(waiters);
    }
}

void qemu_lockcnt_dec(QemuLockCnt *lockcnt)
{
    retro_atomic_fetch_sub_int(&lockcnt->count, QEMU_LOCKCNT_COUNT_STEP);
}

/* If the counter is one, decrement it and return locked.  Otherwise do
 * nothing.
 *
 * If the function returns true, it is impossible for the counter to
 * become nonzero until the next qemu_lockcnt_unlock.
 */
bool qemu_lockcnt_dec_if_lock(QemuLockCnt *lockcnt)
{
    int val = retro_atomic_load_relaxed_int(&lockcnt->count);
    int locked_state = QEMU_LOCKCNT_STATE_LOCKED;
    bool waited = false;
    retro_eventcount_t *waiters = NULL;

    while (val < 2 * QEMU_LOCKCNT_COUNT_STEP) {
        /* If count is going 1->0, take the lock. The fast path is
         * (1, unlocked)->(0, locked) or (1, unlocked)->(0, waiting).
         */
        if (qemu_lockcnt_cmpxchg_or_wait(lockcnt, &val, locked_state, &waited, &waiters)) {
            return true;
        }

        if (waited) {
            /* At this point we do not know if there are more waiters.  Assume
             * there are.
             */
            locked_state = QEMU_LOCKCNT_STATE_WAITING;
        }
    }

    /* If we were woken by another thread, but we're returning in unlocked
     * state, we should also wake a thread because we are effectively
     * releasing the lock that was given to us.  This is the case where
     * qemu_lockcnt_lock would leave QEMU_LOCKCNT_STATE_WAITING in the low
     * bits, and qemu_lockcnt_inc_and_unlock would find it and wake someone.
     */
    if (waited) {
        lockcnt_wake(waiters);
    }
    return false;
}

void qemu_lockcnt_lock(QemuLockCnt *lockcnt)
{
    int val = retro_atomic_load_relaxed_int(&lockcnt->count);
    int step = QEMU_LOCKCNT_STATE_LOCKED;
    bool waited = false;
    retro_eventcount_t *waiters = NULL;

    /* The third argument is only used if the low bits of val are 0
     * (QEMU_LOCKCNT_STATE_FREE), so just blindly mix in the desired
     * state.
     */
    while (!qemu_lockcnt_cmpxchg_or_wait(lockcnt, &val, val + step, &waited, &waiters)) {
        if (waited) {
            /* At this point we do not know if there are more waiters.  Assume
             * there are.
             */
            step = QEMU_LOCKCNT_STATE_WAITING;
        }
    }
}

void qemu_lockcnt_inc_and_unlock(QemuLockCnt *lockcnt)
{
    int expected, new, val;
    retro_eventcount_t *waiters = NULL;

    val = retro_atomic_load_relaxed_int(&lockcnt->count);
    do {
        expected = val;
        if (val & QEMU_LOCKCNT_STATE_WAITING) {
            waiters = qemu_eventcount_for_address(lockcnt);
        }
        new = (val + QEMU_LOCKCNT_COUNT_STEP) & ~QEMU_LOCKCNT_STATE_MASK;
        trace_lockcnt_unlock_attempt(lockcnt, val, new);
        val = lockcnt_cmpxchg(lockcnt, val, new);
    } while (val != expected);

    trace_lockcnt_unlock_success(lockcnt, val, new);
    if (val & QEMU_LOCKCNT_STATE_WAITING) {
        lockcnt_wake(waiters);
    }
}

void qemu_lockcnt_unlock(QemuLockCnt *lockcnt)
{
    int expected, new, val;
    retro_eventcount_t *waiters = NULL;

    val = retro_atomic_load_relaxed_int(&lockcnt->count);
    do {
        expected = val;
        if (val & QEMU_LOCKCNT_STATE_WAITING) {
            waiters = qemu_eventcount_for_address(lockcnt);
        }
        new = val & ~QEMU_LOCKCNT_STATE_MASK;
        trace_lockcnt_unlock_attempt(lockcnt, val, new);
        val = lockcnt_cmpxchg(lockcnt, val, new);
    } while (val != expected);

    trace_lockcnt_unlock_success(lockcnt, val, new);
    if (val & QEMU_LOCKCNT_STATE_WAITING) {
        lockcnt_wake(waiters);
    }
}

unsigned qemu_lockcnt_count(QemuLockCnt *lockcnt)
{
    return retro_atomic_load_relaxed_int(&lockcnt->count) >> QEMU_LOCKCNT_COUNT_SHIFT;
}
