/*
 * QEMU coroutine implementation
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Stefan Hajnoczi    <stefanha@linux.vnet.ibm.com>
 *  Kevin Wolf         <kwolf@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 *
 */

#ifndef QEMU_COROUTINE_H
#define QEMU_COROUTINE_H

#include "qemu/coroutine-core.h"
#include "qemu/queue.h"
#include "qemu/timer.h"

/**
 * Coroutines are a mechanism for stack switching and can be used for
 * cooperative userspace threading.  These functions provide a simple but
 * useful flavor of coroutines that is suitable for writing sequential code,
 * rather than callbacks, for operations that need to give up control while
 * waiting for events to complete.
 *
 * These functions are re-entrant and may be used outside the BQL.
 *
 * Functions that execute in coroutine context cannot be called
 * directly from normal functions.  Use @coroutine_fn to mark such
 * functions.  For example:
 *
 *   static void coroutine_fn foo(void) {
 *       ....
 *   }
 *
 * In the future it would be nice to have the compiler or a static
 * checker catch misuse of such functions.  This annotation might make
 * it possible and in the meantime it serves as documentation.
 */

/**
 * Provides a mutex that can be used to synchronise coroutines
 */
struct CoWaitRecord;
struct CoMutex {
    /* Count of pending lockers; 0 for a free mutex, 1 for an
     * uncontended mutex.
     */
    unsigned locked;

    /* Context that is holding the lock.  Useful to avoid spinning
     * when two coroutines on the same AioContext try to get the lock. :)
     */
    AioContext *ctx;

    /* A queue of waiters.  Elements are added atomically in front of
     * from_push.  to_pop is only populated, and popped from, by whoever
     * is in charge of the next wakeup.  This can be an unlocker or,
     * through the handoff protocol, a locker that is about to go to sleep.
     */
    QSLIST_HEAD(, CoWaitRecord) from_push, to_pop;

    unsigned handoff, sequence;

    Coroutine *holder;
};

#include "qemu/lockable.h"

/**
 * CoQueues are a mechanism to queue coroutines in order to continue executing
 * them later.  They are similar to condition variables, but they need help
 * from an external mutex in order to maintain thread-safety.
 */
typedef struct CoQueue {
    QSIMPLEQ_HEAD(, Coroutine) entries;
} CoQueue;

/**
 * Initialise a CoQueue. This must be called before any other operation is used
 * on the CoQueue.
 */
void qemu_co_queue_init(CoQueue *queue);

typedef enum {
    /*
     * Enqueue at front instead of back. Use this to re-queue a request when
     * its wait condition is not satisfied after being woken up.
     */
    CO_QUEUE_WAIT_FRONT = 0x1,
} CoQueueWaitFlags;

/**
 * Adds the current coroutine to the CoQueue and transfers control to the
 * caller of the coroutine.  The mutex is unlocked during the wait and
 * locked again afterwards.
 */
#define qemu_co_queue_wait(queue, lock) \
    qemu_co_queue_wait_impl(queue, QEMU_MAKE_LOCKABLE(lock), 0)
#define qemu_co_queue_wait_flags(queue, lock, flags) \
    qemu_co_queue_wait_impl(queue, QEMU_MAKE_LOCKABLE(lock), (flags))
void coroutine_fn qemu_co_queue_wait_impl(CoQueue *queue, QemuLockable *lock,
                                          CoQueueWaitFlags flags);

/**
 * Removes the next coroutine from the CoQueue, and queue it to run after
 * the currently-running coroutine yields.
 * Returns true if a coroutine was removed, false if the queue is empty.
 * Used from coroutine context, use qemu_co_enter_next outside.
 */

/**
 * Empties the CoQueue and queues the coroutine to run after
 * the currently-running coroutine yields.
 * Used from coroutine context, use qemu_co_enter_all outside.
 */

/**
 * Removes the next coroutine from the CoQueue, and wake it up.  Unlike
 * qemu_co_queue_next, this function releases the lock during aio_co_wake
 * because it is meant to be used outside coroutine context; in that case, the
 * coroutine is entered immediately, before qemu_co_enter_next returns.
 *
 * If used in coroutine context, qemu_co_enter_next is equivalent to
 * qemu_co_queue_next.
 */
#define qemu_co_enter_next(queue, lock) \
    qemu_co_enter_next_impl(queue, QEMU_MAKE_LOCKABLE(lock))
bool qemu_co_enter_next_impl(CoQueue *queue, QemuLockable *lock);

/**
 * Empties the CoQueue, waking the waiting coroutine one at a time.  Unlike
 * qemu_co_queue_all, this function releases the lock during aio_co_wake
 * because it is meant to be used outside coroutine context; in that case, the
 * coroutine is entered immediately, before qemu_co_enter_all returns.
 *
 * If used in coroutine context, qemu_co_enter_all is equivalent to
 * qemu_co_queue_all.
 */
#define qemu_co_enter_all(queue, lock) \
    qemu_co_enter_all_impl(queue, QEMU_MAKE_LOCKABLE(lock))
void qemu_co_enter_all_impl(CoQueue *queue, QemuLockable *lock);

/**
 * Checks if the CoQueue is empty.
 */
bool qemu_co_queue_empty(CoQueue *queue);


typedef struct CoRwTicket CoRwTicket;
typedef struct CoRwlock {
    CoMutex mutex;

    /* Number of readers, or -1 if owned for writing.  */
    int owners;

    /* Waiting coroutines.  */
    QSIMPLEQ_HEAD(, CoRwTicket) tickets;
} CoRwlock;

/**
 * Initialises a CoRwlock. This must be called before any other operation
 * is used on the CoRwlock
 */

/**
 * Read locks the CoRwlock. If the lock cannot be taken immediately because
 * of a parallel writer, control is transferred to the caller of the current
 * coroutine.
 */

/**
 * Write Locks the mutex. If the lock cannot be taken immediately because
 * of a parallel reader, control is transferred to the caller of the current
 * coroutine.
 */

/**
 * Unlocks the read/write lock and schedules the next coroutine that was
 * waiting for this lock to be run.
 */

typedef struct QemuCoSleep {
    Coroutine *to_wake;
} QemuCoSleep;

/**
 * Yield the coroutine for a given duration. Initializes @w so that,
 * during this yield, it can be passed to qemu_co_sleep_wake() to
 * terminate the sleep.
 */
void coroutine_fn qemu_co_sleep_ns_wakeable(QemuCoSleep *w,
                                            QEMUClockType type, int64_t ns);

/**
 * Yield the coroutine until the next call to qemu_co_sleep_wake.
 */

static inline void coroutine_fn qemu_co_sleep_ns(QEMUClockType type, int64_t ns)
{
    QemuCoSleep w = { 0 };
    qemu_co_sleep_ns_wakeable(&w, type, ns);
}

typedef void CleanupFunc(void *opaque);
/**
 * Run entry in a coroutine and start timer. Wait for entry to finish or for
 * timer to elapse, what happen first. If entry finished, return 0, if timer
 * elapsed earlier, return -ETIMEDOUT.
 *
 * Be careful, entry execution is not canceled, user should handle it somehow.
 * If @clean is provided, it's called after coroutine finish if timeout
 * happened.
 */

/**
 * Yield until a file descriptor becomes readable
 *
 * Note that this function clobbers the handlers for the file descriptor.
 */

/**
 * Increase coroutine pool size
 */

/**
 * Decrease coroutine pool size
 */

/**
 * Sends a (part of) iovec down a socket, yielding when the socket is full, or
 * Receives data into a (part of) iovec from a socket,
 * yielding when there is no data in the socket.
 * The same interface as qemu_sendv_recvv(), with added yielding.
 * XXX should mark these as coroutine_fn
 */
ssize_t coroutine_fn qemu_co_sendv_recvv(int sockfd, struct iovec *iov,
                                         unsigned iov_cnt, size_t offset,
                                         size_t bytes, bool do_send);
#define qemu_co_recvv(sockfd, iov, iov_cnt, offset, bytes) \
  qemu_co_sendv_recvv(sockfd, iov, iov_cnt, offset, bytes, false)
#define qemu_co_sendv(sockfd, iov, iov_cnt, offset, bytes) \
  qemu_co_sendv_recvv(sockfd, iov, iov_cnt, offset, bytes, true)

/**
 * The same as above, but with just a single buffer
 */
ssize_t coroutine_fn qemu_co_send_recv(int sockfd, void *buf, size_t bytes,
                                       bool do_send);
#define qemu_co_recv(sockfd, buf, bytes) \
  qemu_co_send_recv(sockfd, buf, bytes, false)
#define qemu_co_send(sockfd, buf, bytes) \
  qemu_co_send_recv(sockfd, buf, bytes, true)

#endif /* QEMU_COROUTINE_H */
