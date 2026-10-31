/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#endif
#include <retro_mpsc.h>
#include <retro_timers.h>
#include <rthreads/retro_eventcount.h>
#include <rthreads/rthreads.h>
typedef struct Error Error;
#include "qemu/notify.h"
#include "rcu-notify-production.h"

#define container_of(p, type, field) ((type *)((char *)(p) - offsetof(type, field)))
#define unlikely(x) (x)
#define smp_mb_global() retro_atomic_thread_fence_seq_cst()
#define smp_mb_placeholder() retro_atomic_thread_fence_seq_cst()
#define RCU_GP_LOCKED 1UL
#define RCU_GP_CTR 2UL
#define QEMU_DECLARE_CO_TLS(type, name) static type *get_ptr_##name(void);
#define QEMU_DEFINE_CO_TLS(type, name) \
    static _Thread_local type name; \
    static type *get_ptr_##name(void) { return &name; }
#define QEMU_DEFINE_STATIC_CO_TLS(type, name) \
    static _Thread_local type name; \
    static type get_##name(void) { return name; } \
    static void set_##name(type value) { name = value; }
extern retro_atomic_size_t rcu_gp_ctr;
extern retro_eventcount_t rcu_gp_event;
static retro_atomic_int_t stop, sync_published[2];
static _Thread_local unsigned writer_id;
static void test_published(int kind)
{
    if (writer_id)
        retro_atomic_store_release_int(&sync_published[writer_id - 1], 1);
}
#include "rcu-read-production.h"
static sthread_t *worker_handles[2];
static unsigned worker_count;
static int test_detach(sthread_t *thread)
{
    assert(worker_count < 2);
    worker_handles[worker_count++] = thread;
    return 0;
}
#define sthread_detach test_detach
static void call_rcu_thread(void *opaque);
#define qemu_event_init(event, value) ((void)(event), (void)(value))
static unsigned rcu_call_ready_event;
#ifndef _WIN32
static int atfork_depth = 1;
static retro_atomic_int_t rcu_callback_worker_claimed;
#endif
static void test_before_gp_prepare(void);
static void test_after_epoch_publish(void);
#include "rcu-owner-production.h"
static void call_rcu_thread(void *opaque)
{
    rcu_register_thread();
    rcu_unregister_thread();
}
static void assert_only_startup_reader(void)
{
    assert(QLIST_FIRST(&registry) == get_ptr_rcu_reader());
    assert(!QLIST_NEXT(get_ptr_rcu_reader(), node));
}


static retro_atomic_int_t ready[2], release[2], writer_done[2], forced, churn_done;
static retro_atomic_int_t pause_before_prepare, before_prepare, resume_prepare, read_exited[2];
static retro_atomic_ptr_t held_record[2];
static retro_atomic_int_t epoch_pause, epoch_published, epoch_resume, epoch_ready, epoch_read;
static size_t epoch_old;
static unsigned epoch_payload;
static void await(retro_atomic_int_t *value, int target)
{
    unsigned count = 0;
    while (retro_atomic_load_acquire_int(value) < target) {
        assert(++count < 10000000);
        retro_sleep(0);
    }
}
static void test_before_gp_prepare(void)
{
    if (retro_atomic_exchange_int(&pause_before_prepare, 0)) {
        assert(get_rcu_gp_owner());
        assert(!retro_atomic_load_acquire_int(&rcu_gp_event.waiters));
        retro_atomic_store_release_int(&before_prepare, 1);
        await(&resume_prepare, 1);
    }
}
static void test_after_epoch_publish(void)
{
    if (retro_atomic_exchange_int(&epoch_pause, 0)) {
        assert(get_rcu_gp_owner());
        retro_atomic_store_release_int(&epoch_published, 1);
        await(&epoch_resume, 1);
    }
}
static void force(Notifier *notifier, void *unused)
{
    assert(get_rcu_gp_owner());
    retro_atomic_fetch_add_int(&forced, 1);
}
static void held_reader(void *arg)
{
    unsigned id = (unsigned)(uintptr_t)arg;
    Notifier notifier = { .notify = force };
    rcu_register_thread();
    assert(rcu_thread_registered());
    rcu_add_force_rcu_notifier(&notifier);
    rcu_read_lock();
    rcu_read_lock();
    retro_atomic_store_release_ptr(&held_record[id], get_ptr_rcu_reader());
    retro_atomic_store_release_int(&ready[id], 1);
    await(&release[id], 1);
    rcu_read_unlock();
    assert(get_ptr_rcu_reader()->depth == 1);
    await(&release[id], 2);
    rcu_read_unlock();
    retro_atomic_store_release_int(&read_exited[id], 1);
    rcu_unregister_thread();
    assert(!rcu_thread_registered());
}
static void writer(void *arg)
{
    unsigned id = (unsigned)(uintptr_t)arg;
    writer_id = id + 1;
    synchronize_rcu();
    retro_atomic_store_release_int(&writer_done[id], 1);
}
static void churn(void *arg)
{
    for (unsigned i = 0; i < 100; i++) {
        Notifier notifier = { .notify = force };
        rcu_register_thread();
        rcu_add_force_rcu_notifier(&notifier);
        rcu_unregister_thread();
    }
    retro_atomic_store_release_int(&churn_done, 1);
}
/* Force the last old reader to exit before the owner registers a waiter.
 * The second scan must see quiescence rather than sleep on the new key. */
static void notify_before_registration(void)
{
    retro_atomic_store_release_int(&pause_before_prepare, 1);
    sthread_t *reader = sthread_create(held_reader, (void *)0);
    assert(reader);
    await(&ready[0], 1);
    sthread_t *sync = sthread_create(writer, (void *)0);
    assert(sync);
    await(&before_prepare, 1);
    struct rcu_reader_data *record = retro_atomic_load_acquire_ptr(&held_record[0]);
    retro_atomic_store_release_int(&release[0], 2);
    await(&read_exited[0], 1);
    assert(!retro_atomic_load_acquire_size(&record->ctr));
    assert(!retro_atomic_load_acquire_int(&writer_done[0]));
    retro_atomic_store_release_int(&resume_prepare, 1);
    sthread_join(reader);
    sthread_join(sync);
    assert_only_startup_reader();
    for (unsigned i = 0; i < 2; i++) {
        retro_atomic_store_relaxed_int(&ready[i], 0);
        retro_atomic_store_relaxed_int(&read_exited[i], 0);
        retro_atomic_store_relaxed_int(&release[i], 0);
        retro_atomic_store_relaxed_int(&writer_done[i], 0);
        retro_atomic_store_relaxed_int(&sync_published[i], 0);
    }
    retro_atomic_store_relaxed_int(&forced, 0);
}

static void epoch_reader(void *arg)
{
    rcu_register_thread();
    retro_atomic_store_release_int(&epoch_ready, 1);
    /* Relaxed polling intentionally supplies no publication acquire. The
     * production read_lock must acquire it before this non-atomic read. */
    while (retro_atomic_load_relaxed_size(&rcu_gp_ctr) == epoch_old)
        retro_cpu_relax();
    rcu_read_lock();
    assert(epoch_payload == 0x12345678U);
    retro_atomic_store_release_int(&epoch_read, 1);
    rcu_read_unlock();
    rcu_unregister_thread();
}
static void epoch_writer(void *arg)
{
    epoch_payload = 0x12345678U;
    writer((void *)0);
}
static void new_epoch_publication(void)
{
    epoch_old = retro_atomic_load_acquire_size(&rcu_gp_ctr);
    sthread_t *old = sthread_create(held_reader, (void *)0);
    assert(old);
    await(&ready[0], 1);
    sthread_t *fresh = sthread_create(epoch_reader, NULL);
    assert(fresh);
    await(&epoch_ready, 1);
    retro_atomic_store_release_int(&epoch_pause, 1);
    sthread_t *sync = sthread_create(epoch_writer, NULL);
    assert(sync);
    await(&epoch_published, 1);
    await(&epoch_read, 1);
    /* The owner is held after epoch publication, BEFORE the first scan's
     * process-wide barrier. New-epoch acquire already exposed the data. */
    assert(!retro_atomic_load_acquire_int(&writer_done[0]));
    retro_atomic_store_release_int(&epoch_resume, 1);
    retro_atomic_store_release_int(&release[0], 2);
    sthread_join(old);
    sthread_join(fresh);
    sthread_join(sync);
    assert_only_startup_reader();
    for (unsigned i = 0; i < 2; i++) {
        retro_atomic_store_relaxed_int(&ready[i], 0);
        retro_atomic_store_relaxed_int(&read_exited[i], 0);
        retro_atomic_store_relaxed_int(&release[i], 0);
        retro_atomic_store_relaxed_int(&writer_done[i], 0);
        retro_atomic_store_relaxed_int(&sync_published[i], 0);
    }
    retro_atomic_store_relaxed_int(&forced, 0);
}

static void overlapping_periods(void)
{
    sthread_t *readers[2], *writers[2], *lifecycle;
    readers[0] = sthread_create(held_reader, (void *)0);
    assert(readers[0]);
    await(&ready[0], 1);
    writers[0] = sthread_create(writer, (void *)0);
    assert(writers[0]);
    await(&forced, 1); /* first grace period has advanced and is blocked */
    readers[1] = sthread_create(held_reader, (void *)1);
    assert(readers[1]);
    await(&ready[1], 1);
    writers[1] = sthread_create(writer, (void *)1);
    assert(writers[1]);
    await(&sync_published[1], 1);
    lifecycle = sthread_create(churn, NULL);
    assert(lifecycle);
    await(&churn_done, 1); /* lifecycle RPCs cannot be blocked behind the GP */
    sthread_join(lifecycle);
    assert(!retro_atomic_load_acquire_int(&writer_done[0]));
    assert(!retro_atomic_load_acquire_int(&writer_done[1]));
    retro_atomic_store_release_int(&release[0], 1);
    retro_sleep(2);
    assert(!retro_atomic_load_acquire_int(&writer_done[0]));
    retro_atomic_store_release_int(&release[0], 2);
    if (sizeof(size_t) == 8) {
        await(&writer_done[0], 1);
        /* A request made after the epoch advance needs another period. */
        assert(!retro_atomic_load_acquire_int(&writer_done[1]));
    }
    retro_atomic_store_release_int(&release[1], 2);
    for (unsigned i = 0; i < 2; i++) {
        sthread_join(readers[i]);
        sthread_join(writers[i]);
    }
    assert_only_startup_reader();
}

typedef struct Object { uint64_t magic, serial; } Object;
static retro_atomic_ptr_t objects[4];
static retro_atomic_int_t stress_stop;
#define MAGIC UINT64_C(0xa1b2c3d4a5b6c7d8)
static void stress_reader(void *arg)
{
    unsigned rounds = 0;
    rcu_register_thread();
    while (!retro_atomic_load_acquire_int(&stress_stop)) {
        rcu_read_lock();
        for (unsigned i = 0; i < 4; i++) {
            Object *object = retro_atomic_load_acquire_ptr(&objects[i]);
            if (!(++rounds % 127)) retro_sleep(0);
            assert(object->magic == MAGIC && object->serial < 401);
        }
        rcu_read_unlock();
        if (!(rounds % 256)) {
            rcu_unregister_thread();
            rcu_register_thread();
        }
    }
    rcu_unregister_thread();
}
static void stress_writer(void *arg)
{
    unsigned id = (unsigned)(uintptr_t)arg;
    for (unsigned i = 1; i <= 400; i++) {
        Object *fresh = malloc(sizeof(*fresh));
        assert(fresh);
        *fresh = (Object){ MAGIC, i };
        Object *old = retro_atomic_exchange_ptr(&objects[id], fresh);
        synchronize_rcu();
        memset(old, 0x55, sizeof(*old));
        free(old);
    }
}
static void stress(void)
{
    sthread_t *readers[8], *writers[4];
    for (unsigned i = 0; i < 4; i++) {
        Object *object = malloc(sizeof(*object));
        assert(object);
        *object = (Object){ MAGIC, 0 };
        retro_atomic_ptr_init(&objects[i], object);
    }
    for (unsigned i = 0; i < 8; i++) {
        readers[i] = sthread_create(stress_reader, NULL);
        assert(readers[i]);
    }
    for (unsigned i = 0; i < 4; i++) {
        writers[i] = sthread_create(stress_writer, (void *)(uintptr_t)i);
        assert(writers[i]);
    }
    for (unsigned i = 0; i < 4; i++) sthread_join(writers[i]);
    retro_atomic_store_release_int(&stress_stop, 1);
    for (unsigned i = 0; i < 8; i++) sthread_join(readers[i]);
    for (unsigned i = 0; i < 4; i++) free(retro_atomic_load_acquire_ptr(&objects[i]));
    /* Reuse actual OS TLS allocations while the owner scans the registry. */
    for (unsigned i = 0; i < 32; i++) {
        sthread_t *thread = sthread_create(churn, NULL);
        assert(thread);
        sthread_join(thread);
    }
    synchronize_rcu();
    assert_only_startup_reader();
}
static void finish_workers(void)
{
    rcu_unregister_thread();
    retro_atomic_store_release_int(&stop, 1);
    retro_eventcount_notify(&rcu_gp_event);
    for (unsigned i = 0; i < worker_count; i++) {
        sthread_join(worker_handles[i]);
        worker_handles[i] = NULL;
    }
    assert(QLIST_EMPTY(&registry));
    assert(!rcu_sync_pending && retro_mpsc_empty(&rcu_requests));
    retro_eventcount_free(&rcu_request_complete);
    retro_eventcount_free(&rcu_gp_event);
}
#if !defined(_WIN32) && !defined(RCU_OWNER_NO_FORK)
static void test_fork_child(void)
{
    worker_count = 0; /* parent thread handles are not child workers */
    rcu_init_child();
}
static void fork_reinitialization(void)
{
    assert(!pthread_atfork(NULL, NULL, test_fork_child));
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        assert(worker_count == 2 && rcu_thread_registered());
        sthread_join(worker_handles[1]);
        worker_handles[1] = NULL;
        sthread_t *thread = sthread_create(churn, NULL);
        assert(thread);
        sthread_join(thread);
        synchronize_rcu();
        assert_only_startup_reader();
        finish_workers();
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    synchronize_rcu();
    assert_only_startup_reader();
}
#endif
int main(void)
{
    rcu_init_complete(false);
    assert(worker_count == 2 && !rcu_thread_registered());
    sthread_join(worker_handles[1]);
    worker_handles[1] = NULL;
    assert(QLIST_EMPTY(&registry));
    rcu_register_thread();
    notify_before_registration();
    new_epoch_publication();
    overlapping_periods();
    stress();
#if !defined(_WIN32) && !defined(RCU_OWNER_NO_FORK)
    fork_reinitialization();
#endif
    finish_workers();
    puts("RCU owner: overlapping periods, nested readers, registry churn, TLS lifetime and concurrent reclamation passed");
    return 0;
}
