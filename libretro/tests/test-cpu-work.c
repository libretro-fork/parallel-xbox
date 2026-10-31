/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <retro_mpsc.h>
#include <rthreads/retro_eventcount.h>
#include <rthreads/rthreads.h>

typedef uintptr_t run_on_cpu_data;
typedef struct CPUState CPUState;
typedef void (*run_on_cpu_func)(CPUState *, run_on_cpu_data);
struct CPUState {
    retro_atomic_int_t running;
    unsigned exclusive_context_count;
    int cpu_index;
    retro_mpsc_t work_queue;
    retro_mpsc_node_t *work_pending;
    retro_atomic_int_t work_count;
};

static _Thread_local CPUState *current_cpu;
static _Thread_local bool held = true;
static retro_eventcount_t qemu_work_done, kick;
static retro_atomic_int_t stop;
static CPUState target;
static unsigned executed, safe_executed;

#define container_of(p, type, member) ((type *)((char *)(p) - offsetof(type, member)))
#define g_new0(type, count) ((type *)calloc(count, sizeof(type)))
#define g_free free
#define SET_current_cpu(p) (current_cpu = (p))

static bool qemu_cpu_is_self(CPUState *cpu) { return current_cpu == cpu; }
static void cpu_exit(CPUState *cpu) { (void)cpu; retro_eventcount_notify(&kick); }
static void bql_unlock(void) { assert(held); held = false; }
static void bql_lock(void) { assert(!held); held = true; }
#define g_assert assert
#define trace_cpu_exec_start(index) ((void)(index))
#define trace_cpu_exec_end(index) ((void)(index))

/* Extracted from cpu-common.c by test-cpu-work.py. */
#include "cpu-work-production.h"

static unsigned order[4], order_count;
static void fifo_callback(CPUState *cpu, run_on_cpu_data data)
{
    assert(order_count < 4);
    order[order_count++] = (unsigned)data;
    if (data == 1) {
        async_run_on_cpu(cpu, fifo_callback, 4);
        process_queued_cpu_work(cpu);
    }
}

typedef struct payload { unsigned input, result; } payload;
static void sync_callback(CPUState *cpu, run_on_cpu_data data)
{
    payload *p = (payload *)data;
    assert(current_cpu == cpu && held);
    p->result = p->input ^ 0x12345678;
}

static void count_callback(CPUState *cpu, run_on_cpu_data data)
{
    assert(current_cpu == cpu && held && data == 123);
    executed++;
}

static void safe_callback(CPUState *cpu, run_on_cpu_data data)
{
    assert(current_cpu == cpu && !held && cpu->exclusive_context_count == 1 && data == 456);
    start_exclusive();
    assert(cpu->exclusive_context_count == 2);
    end_exclusive();
    safe_executed++;
}

static void producer(void *opaque)
{
    unsigned i, id = *(unsigned *)opaque;
    for (i = 0; i < 1000; i++) {
        payload p = { id * 1000 + i, 0 };
        async_run_on_cpu(&target, count_callback, 123);
        if (!(i % 10))
            async_safe_run_on_cpu(&target, safe_callback, 456);
        do_run_on_cpu(&target, sync_callback, (uintptr_t)&p);
        assert(p.result == (p.input ^ 0x12345678));
        assert(current_cpu == NULL && held);
    }
}

static void consumer(void *unused)
{
    (void)unused;
    current_cpu = &target;
    for (;;) {
        int key;
        process_queued_cpu_work(&target);
        if (retro_atomic_load_acquire_int(&stop))
            break;
        key = retro_eventcount_prepare_wait(&kick);
        if (retro_atomic_load_acquire_int(&target.work_count) ||
            retro_atomic_load_acquire_int(&stop))
            retro_eventcount_cancel_wait(&kick);
        else
            retro_eventcount_commit_wait(&kick, key);
    }
    process_queued_cpu_work(&target);
}

int main(void)
{
    sthread_t *worker, *threads[8];
    unsigned i, ids[8];
    payload p = { 42, 0 };
    assert(retro_eventcount_init(&qemu_work_done));
    assert(retro_eventcount_init(&kick));
    retro_mpsc_init(&target.work_queue);
    retro_atomic_int_init(&target.work_count, 0);
    current_cpu = &target;
    retro_atomic_int_init(&target.running, 0);
    cpu_exec_start(&target);
    assert(retro_atomic_load_acquire_int(&target.running));
    cpu_exec_end(&target);
    assert(!retro_atomic_load_acquire_int(&target.running));
    do_run_on_cpu(&target, sync_callback, (uintptr_t)&p);
    assert(p.result == (42 ^ 0x12345678));
    for (i = 1; i <= 3; i++)
        async_run_on_cpu(&target, fifo_callback, i);
    process_queued_cpu_work(&target);
    for (i = 0; i < 4; i++)
        assert(order[i] == i + 1);
    assert(retro_atomic_load_acquire_int(&target.work_count) == 0);
    async_run_on_cpu(&target, count_callback, 123);
    async_run_on_cpu(&target, count_callback, 123);
    free_queued_cpu_work(&target);
    assert(retro_atomic_load_acquire_int(&target.work_count) == 0);
    assert(executed == 0 && target.work_pending == NULL);
    current_cpu = NULL;
    retro_atomic_int_init(&stop, 0);
    worker = sthread_create(consumer, NULL);
    assert(worker);
    for (i = 0; i < 8; i++) {
        ids[i] = i;
        threads[i] = sthread_create(producer, &ids[i]);
        assert(threads[i]);
    }
    for (i = 0; i < 8; i++)
        sthread_join(threads[i]);
    retro_atomic_store_release_int(&stop, 1);
    retro_eventcount_notify(&kick);
    sthread_join(worker);
    assert(executed == 8000 && safe_executed == 800);
    assert(retro_atomic_load_acquire_int(&target.work_count) == 0);
    assert(retro_mpsc_empty(&target.work_queue) && !target.work_pending);
    retro_eventcount_free(&qemu_work_done);
    retro_eventcount_free(&kick);
    puts("CPU work: FIFO, reentry, completion, exclusivity and reclamation passed");
    return 0;
}
