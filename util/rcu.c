/*
 * urcu-mb.c
 *
 * Userspace RCU library with explicit memory barriers
 *
 * Copyright (c) 2009 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 * Copyright (c) 2009 Paul E. McKenney, IBM Corporation.
 * Copyright 2015 Red Hat, Inc.
 *
 * Ported to QEMU by Paolo Bonzini  <pbonzini@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * IBM's contributions to this file may be relicensed under LGPLv2 or later.
 */

#include "qemu/osdep.h"
#include "qemu/rcu.h"
#include "qemu/thread.h"
#include "qemu/main-loop.h"
#include <retro_mpsc.h>
#include <rthreads/rthreads.h>

/*
 * Global grace period counter.  Bit 0 is always one in rcu_gp_ctr.
 * Bits 1 and above are defined in synchronize_rcu.
 */
#define RCU_GP_LOCKED           (1UL << 0)
#define RCU_GP_CTR              (1UL << 1)


retro_atomic_size_t rcu_gp_ctr = RCU_GP_LOCKED;

/* A single owner edits the registry and advances grace periods. Requests
 * carry stack storage until acknowledged; neither request nor reader TLS
 * may be touched after its acknowledgement is published. */
retro_eventcount_t rcu_gp_event;
static retro_eventcount_t rcu_request_complete;
static retro_mpsc_t rcu_requests;

QEMU_DEFINE_CO_TLS(struct rcu_reader_data, rcu_reader)
QEMU_DEFINE_STATIC_CO_TLS(bool, rcu_gp_owner)

typedef QLIST_HEAD(, rcu_reader_data) ThreadList;
static ThreadList registry = QLIST_HEAD_INITIALIZER(registry);

typedef enum RcuRequestKind {
    RCU_REGISTER, RCU_UNREGISTER, RCU_ADD_NOTIFIER, RCU_SYNCHRONIZE
} RcuRequestKind;

typedef struct RcuRequest {
    retro_mpsc_node_t node;
    struct RcuRequest *next_sync;
    RcuRequestKind kind;
    struct rcu_reader_data *reader;
    Notifier *notifier;
    retro_atomic_int_t done;
} RcuRequest;

static RcuRequest *rcu_sync_pending;
static RcuRequest **rcu_sync_tail = &rcu_sync_pending;

static void rcu_complete_request(RcuRequest *request)
{
    retro_atomic_store_release_int(&request->done, 1);
    /* The caller may have destroyed request already. Notify only globals. */
    retro_eventcount_notify(&rcu_request_complete);
}

static void rcu_submit_request(RcuRequestKind kind,
                               struct rcu_reader_data *reader, Notifier *notifier)
{
    RcuRequest request = { .kind = kind, .reader = reader, .notifier = notifier };

    /* Force-RCU callbacks execute on the owner and may kick readers, but
     * must not synchronously submit work back to their own executor. */
    assert(!get_rcu_gp_owner());
    retro_atomic_int_init(&request.done, 0);
    retro_mpsc_push(&rcu_requests, &request.node);
    retro_eventcount_notify(&rcu_gp_event);
    while (!retro_atomic_load_acquire_int(&request.done)) {
        int key = retro_eventcount_prepare_wait(&rcu_request_complete);
        if (retro_atomic_load_acquire_int(&request.done)) {
            retro_eventcount_cancel_wait(&rcu_request_complete);
        } else {
            retro_eventcount_commit_wait(&rcu_request_complete, key);
        }
    }
}

/* Service lifecycle requests even while a reader holds up a grace period.
 * Otherwise a reader waiting for unregister/notifier acknowledgement could
 * prevent the owner from making progress. New synchronize requests belong
 * to the NEXT period: the current epoch may predate their pointer updates. */
static void rcu_service_requests(void)
{
    retro_mpsc_node_t *node = retro_mpsc_take_all(&rcu_requests);

    while (node) {
        RcuRequest *request = container_of(node, RcuRequest, node);
        struct rcu_reader_data *reader = request->reader;
        node = node->next;
        switch (request->kind) {
        case RCU_REGISTER:
            assert(retro_atomic_load_relaxed_size(&reader->ctr) == 0);
            QLIST_INSERT_HEAD(&registry, reader, node);
            break;
        case RCU_UNREGISTER:
            assert(retro_atomic_load_relaxed_size(&reader->ctr) == 0);
            QLIST_REMOVE(reader, node);
            reader->node.le_prev = NULL;
            retro_atomic_store_relaxed_int(&reader->waiting, 0);
            notifier_list_init(&reader->force_rcu);
            break;
        case RCU_ADD_NOTIFIER:
            notifier_list_add(&reader->force_rcu, request->notifier);
            if (retro_atomic_load_relaxed_int(&reader->waiting)) {
                request->notifier->notify(request->notifier, NULL);
            }
            break;
        case RCU_SYNCHRONIZE:
            request->next_sync = NULL;
            *rcu_sync_tail = request;
            rcu_sync_tail = &request->next_sync;
            continue;
        }
        rcu_complete_request(request);
    }
}

static bool rcu_gp_ongoing(struct rcu_reader_data *reader)
{
    size_t ctr = retro_atomic_load_acquire_size(&reader->ctr);
    return ctr && ctr != retro_atomic_load_relaxed_size(&rcu_gp_ctr);
}

/* The owner alone reads registry links and invokes force-RCU notifiers.
 * It never retains a TLS pointer across servicing unregister requests. */
static void wait_for_readers(void)
{
    bool forced = false;

    for (;;) {
        bool ongoing = false;
        struct rcu_reader_data *reader;
        int key = retro_eventcount_prepare_wait(&rcu_gp_event);

        rcu_service_requests();
        if (!forced) {
            forced = true;
            QLIST_FOREACH(reader, &registry, node) {
                retro_atomic_store_relaxed_int(&reader->waiting, 1);
                notifier_list_notify(&reader->force_rcu, NULL);
            }
        }
        /* Pairs with read-side placeholders: waiting publication precedes
         * the reader counter checks, which precede reclamation. */
        smp_mb_global();
        QLIST_FOREACH(reader, &registry, node) {
            if (rcu_gp_ongoing(reader)) {
                ongoing = true;
            } else {
                retro_atomic_store_relaxed_int(&reader->waiting, 0);
            }
        }
        if (!ongoing) {
            retro_eventcount_cancel_wait(&rcu_gp_event);
            return;
        }
        retro_eventcount_commit_wait(&rcu_gp_event, key);
    }
}

static void synchronize_rcu_owned(void)
{
    smp_mb_global();
    if (!QLIST_EMPTY(&registry)) {
        size_t gp = retro_atomic_load_relaxed_size(&rcu_gp_ctr);

        if (sizeof(size_t) < 8) {
            /* Preserve the two-phase parity scheme on 32-bit targets. */
            retro_atomic_store_release_size(&rcu_gp_ctr, gp ^ RCU_GP_CTR);
            wait_for_readers();
            retro_atomic_store_release_size(&rcu_gp_ctr, gp);
        } else {
            retro_atomic_store_release_size(&rcu_gp_ctr, gp + RCU_GP_CTR);
        }
        wait_for_readers();
    }
}

static void rcu_gp_worker(void *opaque)
{
    sthread_setname("rcu-grace");
    set_rcu_gp_owner(true);
    for (;;) {
        int key = retro_eventcount_prepare_wait(&rcu_gp_event);
        rcu_service_requests();
        if (rcu_sync_pending) {
            RcuRequest *batch = rcu_sync_pending;
            rcu_sync_pending = NULL;
            rcu_sync_tail = &rcu_sync_pending;
            retro_eventcount_cancel_wait(&rcu_gp_event);
            synchronize_rcu_owned();
            while (batch) {
                RcuRequest *next = batch->next_sync;
                rcu_complete_request(batch);
                batch = next;
            }
        } else {
            retro_eventcount_commit_wait(&rcu_gp_event, key);
        }
    }
}

void synchronize_rcu(void)
{
    assert(get_ptr_rcu_reader()->depth == 0);
    rcu_submit_request(RCU_SYNCHRONIZE, NULL, NULL);
}

/* Multi-producer, single-consumer queue based on urcu/static/wfqueue.h
 * from liburcu.  Note that head is only used by the consumer.
 */
static retro_atomic_int_t rcu_call_count;
static struct rcu_head dummy;
static struct rcu_head *head = &dummy;
static retro_atomic_ptr_t tail = &dummy.next;    /* retro_atomic_ptr_t * */
static QemuEvent rcu_call_ready_event;

static void enqueue(struct rcu_head *node)
{
    retro_atomic_ptr_t *old_tail;

    retro_atomic_store_relaxed_ptr(&node->next, NULL);

    /*
     * Make this node the tail of the list.  The node will be
     * used by further enqueue operations, but it will not
     * be dequeued yet...
     */
    old_tail = retro_atomic_exchange_ptr(&tail, &node->next);

    /*
     * ... until it is pointed to from another item in the list.
     * In the meantime, try_dequeue() will find a NULL next pointer
     * and loop.
     *
     * Synchronizes with the acquire load in try_dequeue().
     */
    retro_atomic_store_release_ptr(old_tail, node);
}

static struct rcu_head *try_dequeue(void)
{
    struct rcu_head *node, *next;

retry:
    /* Head is only written by this thread, so no need for barriers.  */
    node = head;

    /*
     * If the head node has NULL in its next pointer, the value is
     * wrong and we need to wait until its enqueuer finishes the update.
     */
    next = retro_atomic_load_acquire_ptr(&node->next);
    if (!next) {
        return NULL;
    }

    /*
     * Test for an empty list, which we do not expect.  Note that for
     * the consumer head and tail are always consistent.  The head
     * is consistent because only the consumer reads/writes it.
     * The tail, because it is the first step in the enqueuing.
     * It is only the next pointers that might be inconsistent.
     */
    if (head == &dummy && retro_atomic_load_relaxed_ptr(&tail) == &dummy.next) {
        abort();
    }

    /*
     * Since we are the sole consumer, and we excluded the empty case
     * above, the queue will always have at least two nodes: the
     * dummy node, and the one being removed.  So we do not need to update
     * the tail pointer.
     */
    head = next;

    /* If we dequeued the dummy node, add it back at the end and retry.  */
    if (node == &dummy) {
        enqueue(node);
        goto retry;
    }

    return node;
}

/* The callback queue is MPSC, including while startup is racing. Claim
 * its sole consumer before registering or touching the queue. */
static retro_atomic_int_t rcu_callback_worker_claimed;

static bool rcu_claim_callback_worker(void)
{
    return retro_atomic_cas_int(&rcu_callback_worker_claimed, 0, 1);
}

static void call_rcu_thread(void *opaque)
{
    struct rcu_head *node;

    sthread_setname("call_rcu");

    if (!rcu_claim_callback_worker()) {
        fprintf(stderr, "xemu: duplicate RCU callback worker ignored\n");
        return;
    }
    rcu_register_thread();

    for (;;) {
        int n;

        /*
         * Fetch rcu_call_count now, we only must process elements that were
         * added before synchronize_rcu() starts.
         */
        for (;;) {
            qemu_event_reset(&rcu_call_ready_event);
            n = retro_atomic_load_relaxed_int(&rcu_call_count);
            if (n) {
                break;
            }

            qemu_event_wait(&rcu_call_ready_event);
        }

        synchronize_rcu();
        retro_atomic_fetch_sub_int(&rcu_call_count, n);
        bql_lock();
        while (n > 0) {
            node = try_dequeue();
            while (!node) {
                bql_unlock();
                qemu_event_reset(&rcu_call_ready_event);
                node = try_dequeue();
                if (!node) {
                    qemu_event_wait(&rcu_call_ready_event);
                    node = try_dequeue();
                }
                bql_lock();
            }

            n--;
            node->func(node);
        }
        bql_unlock();
    }
    abort();
}

void call_rcu1(struct rcu_head *node, void (*func)(struct rcu_head *node))
{
    node->func = func;
    enqueue(node);
    retro_atomic_inc_int(&rcu_call_count);
    qemu_event_set(&rcu_call_ready_event);
}


struct rcu_drain {
    struct rcu_head rcu;
    QemuEvent drain_complete_event;
};

static void drain_rcu_callback(struct rcu_head *node)
{
    struct rcu_drain *event = (struct rcu_drain *)node;
    qemu_event_set(&event->drain_complete_event);
}

/*
 * This function ensures that all pending RCU callbacks
 * on the current thread are done executing

 * drops big qemu lock during the wait to allow RCU thread
 * to process the callbacks
 *
 */

void drain_call_rcu(void)
{
    struct rcu_drain rcu_drain;
    bool locked = bql_locked();

    memset(&rcu_drain, 0, sizeof(struct rcu_drain));
    qemu_event_init(&rcu_drain.drain_complete_event, false);

    if (locked) {
        bql_unlock();
    }


    /*
     * RCU callbacks are invoked in the same order as in which they
     * are registered, thus we can be sure that when 'drain_rcu_callback'
     * is called, all RCU callbacks that were registered on this thread
     * prior to calling this function are completed.
     *
     * Note that since we have only one global queue of the RCU callbacks,
     * we also end up waiting for most of RCU callbacks that were registered
     * on the other threads, but this is a side effect that shouldn't be
     * assumed.
     */

    call_rcu1(&rcu_drain.rcu, drain_rcu_callback);
    qemu_event_wait(&rcu_drain.drain_complete_event);

    if (locked) {
        bql_lock();
    }

}

void rcu_register_thread(void)
{
    struct rcu_reader_data *reader = get_ptr_rcu_reader();
    assert(!reader->registered && reader->depth == 0);
    rcu_submit_request(RCU_REGISTER, reader, NULL);
    reader->registered = true;
}

bool rcu_thread_registered(void)
{
    return get_ptr_rcu_reader()->registered;
}

void rcu_unregister_thread(void)
{
    struct rcu_reader_data *reader = get_ptr_rcu_reader();
    assert(reader->registered && reader->depth == 0);
    rcu_submit_request(RCU_UNREGISTER, reader, NULL);
    reader->registered = false;
}

void rcu_add_force_rcu_notifier(Notifier *n)
{
    struct rcu_reader_data *reader = get_ptr_rcu_reader();
    assert(reader->registered);
    rcu_submit_request(RCU_ADD_NOTIFIER, reader, n);
}

static void rcu_init_complete(bool register_self)
{
    sthread_t *owner, *callbacks;

    retro_mpsc_init(&rcu_requests);
    rcu_sync_pending = NULL;
    rcu_sync_tail = &rcu_sync_pending;
    if (!retro_eventcount_init(&rcu_gp_event) ||
        !retro_eventcount_init(&rcu_request_complete)) {
        abort();
    }
    qemu_event_init(&rcu_call_ready_event, false);

    /* Only a registered fork caller needs its TLS restored here. The
     * library-loading thread need not be a reader and may exit immediately.
     * Restore before creating workers: waiting for a new thread from a
     * Windows DLL constructor would deadlock on the loader lock. */
    if (register_self) {
        struct rcu_reader_data *reader = get_ptr_rcu_reader();
        assert(!reader->registered && reader->depth == 0);
        QLIST_INSERT_HEAD(&registry, reader, node);
        reader->registered = true;
    }

    owner = sthread_create(rcu_gp_worker, NULL);
    if (!owner || sthread_detach(owner)) {
        abort();
    }
    callbacks = sthread_create(call_rcu_thread, NULL);
    if (!callbacks || sthread_detach(callbacks)) {
        abort();
    }
}

static int atfork_depth = 1;

void rcu_disable_atfork(void)
{
    atfork_depth--;
}

#ifdef CONFIG_POSIX
static void rcu_init_child(void)
{
    if (atfork_depth < 1) {
        return;
    }

    memset(&registry, 0, sizeof(registry));
    /* Parent workers do not survive fork. Discard their private registry
     * and request state; only this thread's TLS is live in the child. */
    bool registered = get_ptr_rcu_reader()->registered;
    get_ptr_rcu_reader()->registered = false;
    retro_atomic_store_relaxed_int(&rcu_callback_worker_claimed, 0);
    rcu_init_complete(registered);
}
#endif

static void __attribute__((__constructor__)) rcu_init(void)
{
    static gsize initialized;

    /* Initialize once per loaded core image. This queue has one consumer:
     * never reset its queues/events or launch duplicate workers.
     * The fork child calls rcu_init_complete() directly to replace its
     * worker. */
    if (!g_once_init_enter(&initialized)) {
        return;
    }
    smp_mb_global_init();
#ifdef CONFIG_POSIX
    pthread_atfork(NULL, NULL, rcu_init_child);
#endif
    rcu_init_complete(false);
    g_once_init_leave(&initialized, 1);
}
