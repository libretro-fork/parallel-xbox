/*
 * Seqlock implementation for QEMU
 *
 * Copyright Red Hat, Inc. 2013
 *
 * Author:
 *  Paolo Bonzini <pbonzini@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_SEQLOCK_H
#define QEMU_SEQLOCK_H

#include <retro_atomic.h>
#include "qemu/thread.h"
#include "qemu/lockable.h"

typedef struct QemuSeqLock QemuSeqLock;

/*
 * The fields a seqlock guards are themselves read and written with
 * relaxed atomics; the fences below order them against the sequence.
 */
struct QemuSeqLock {
    retro_atomic_int_t sequence;
};

static inline void seqlock_init(QemuSeqLock *sl)
{
    retro_atomic_int_init(&sl->sequence, 0);
}

/* Lock out other writers and update the count.  */
static inline void seqlock_write_begin(QemuSeqLock *sl)
{
    int seq = retro_atomic_load_relaxed_int(&sl->sequence);

    retro_atomic_store_relaxed_int(&sl->sequence, seq + 1);

    /* Write sequence before updating other fields.  */
    retro_atomic_thread_fence_release();
}

static inline void seqlock_write_end(QemuSeqLock *sl)
{
    int seq = retro_atomic_load_relaxed_int(&sl->sequence);

    /* Write other fields before finalizing sequence.  */
    retro_atomic_store_release_int(&sl->sequence, seq + 1);
}

/* Lock out other writers and update the count.  */
static inline void seqlock_write_lock_impl(QemuSeqLock *sl, QemuLockable *lock)
{
    qemu_lockable_lock(lock);
    seqlock_write_begin(sl);
}
#define seqlock_write_lock(sl, lock) \
    seqlock_write_lock_impl(sl, QEMU_MAKE_LOCKABLE(lock))

/* Update the count and release the lock.  */
static inline void seqlock_write_unlock_impl(QemuSeqLock *sl, QemuLockable *lock)
{
    seqlock_write_end(sl);
    qemu_lockable_unlock(lock);
}
#define seqlock_write_unlock(sl, lock) \
    seqlock_write_unlock_impl(sl, QEMU_MAKE_LOCKABLE(lock))


static inline unsigned seqlock_read_begin(const QemuSeqLock *sl)
{
    /* Always fail if a write is in progress.  Read sequence before
     * reading other fields.  */
    unsigned ret = retro_atomic_load_acquire_int(
        (retro_atomic_int_t *)&sl->sequence);

    return ret & ~1;
}

static inline int seqlock_read_retry(const QemuSeqLock *sl, unsigned start)
{
    /* Read other fields before reading final sequence.  */
    retro_atomic_thread_fence_acquire();
    return unlikely((unsigned)retro_atomic_load_relaxed_int(
        (retro_atomic_int_t *)&sl->sequence) != start);
}

#endif
