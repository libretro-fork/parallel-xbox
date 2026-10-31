/*
 * CPU thread main loop - common bits for user and system mode emulation
 *
 *  Copyright (c) 2003-2005 Fabrice Bellard
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "exec/cpu-common.h"
#include "hw/core/cpu.h"
#include "trace/trace-root.h"
#include <rthreads/retro_eventcount.h>

static retro_eventcount_t qemu_work_done;

void qemu_init_cpu_list(void)
{
    if (!retro_eventcount_init(&qemu_work_done)) {
        abort();
    }
}


int cpu_get_free_index(void)
{
    CPUState *some_cpu;
    int max_cpu_index = 0;

    CPU_FOREACH(some_cpu) {
        if (some_cpu->cpu_index >= max_cpu_index) {
            max_cpu_index = some_cpu->cpu_index + 1;
        }
    }
    return max_cpu_index;
}

CPUTailQ cpus_queue = QTAILQ_RCU_HEAD_INITIALIZER(cpus_queue);

void cpu_list_add(CPUState *cpu)
{
    static bool cpu_index_auto_assigned;

    g_assert(bql_locked());
    /* Xbox has one guest CPU; lifecycle changes belong to the machine owner. */
    g_assert(QTAILQ_EMPTY(&cpus_queue));
    if (cpu->cpu_index == UNASSIGNED_CPU_INDEX) {
        cpu_index_auto_assigned = true;
        cpu->cpu_index = cpu_get_free_index();
        assert(cpu->cpu_index != UNASSIGNED_CPU_INDEX);
    } else {
        assert(!cpu_index_auto_assigned);
    }
    QTAILQ_INSERT_TAIL_RCU(&cpus_queue, cpu, node);
}

void cpu_list_remove(CPUState *cpu)
{
    g_assert(bql_locked());
    if (!QTAILQ_IN_USE(cpu, node)) {
        /* there is nothing to undo since cpu_exec_init() hasn't been called */
        return;
    }

    QTAILQ_REMOVE_RCU(&cpus_queue, cpu, node);
    cpu->cpu_index = UNASSIGNED_CPU_INDEX;
}

CPUState *qemu_get_cpu(int index)
{
    CPUState *cpu;

    CPU_FOREACH(cpu) {
        if (cpu->cpu_index == index) {
            return cpu;
        }
    }

    return NULL;
}

/* current CPU in the current thread. It is only valid inside cpu_exec() */
#if defined(_WIN32)
unsigned long tls_key_current_cpu = 0xFFFFFFFF; /* TLS_OUT_OF_INDEXES */
#else
__thread CPUState *current_cpu;
#endif

struct qemu_work_item {
    retro_mpsc_node_t node;
    run_on_cpu_func func;
    run_on_cpu_data data;
    bool free, exclusive;
    retro_atomic_int_t done;
};

static void queue_work_on_cpu(CPUState *cpu, struct qemu_work_item *wi)
{
    retro_atomic_int_init(&wi->done, false);
    retro_atomic_fetch_add_int(&cpu->work_count, 1);
    retro_mpsc_push(&cpu->work_queue, &wi->node);

    /* exit the inner loop and reach qemu_process_cpu_events_common().  */
    cpu_exit(cpu);
}

void do_run_on_cpu(CPUState *cpu, run_on_cpu_func func, run_on_cpu_data data)
{
    struct qemu_work_item wi;

    if (qemu_cpu_is_self(cpu)) {
        func(cpu, data);
        return;
    }

    wi.func = func;
    wi.data = data;
    wi.free = false;
    wi.exclusive = false;

    queue_work_on_cpu(cpu, &wi);
    while (!retro_atomic_load_acquire_int(&wi.done)) {
        CPUState *self_cpu = current_cpu;
        int key;

        bql_unlock();
        key = retro_eventcount_prepare_wait(&qemu_work_done);
        if (retro_atomic_load_acquire_int(&wi.done)) {
            retro_eventcount_cancel_wait(&qemu_work_done);
        } else {
            retro_eventcount_commit_wait(&qemu_work_done, key);
        }
        bql_lock();
        SET_current_cpu(self_cpu);
    }
}

void async_run_on_cpu(CPUState *cpu, run_on_cpu_func func, run_on_cpu_data data)
{
    struct qemu_work_item *wi;

    wi = g_new0(struct qemu_work_item, 1);
    wi->func = func;
    wi->data = data;
    wi->free = true;

    queue_work_on_cpu(cpu, wi);
}

/* The sole guest CPU owns exclusive scopes, outside guest execution. */
void start_exclusive(void)
{
    g_assert(current_cpu && qemu_cpu_is_self(current_cpu));
    g_assert(!retro_atomic_load_relaxed_int(&current_cpu->running));
    current_cpu->exclusive_context_count++;
}

void end_exclusive(void)
{
    g_assert(current_cpu && qemu_cpu_is_self(current_cpu));
    g_assert(current_cpu->exclusive_context_count > 0);
    current_cpu->exclusive_context_count--;
}

void cpu_exec_start(CPUState *cpu)
{
    g_assert(qemu_cpu_is_self(cpu));
    g_assert(cpu->exclusive_context_count == 0);
    trace_cpu_exec_start(cpu->cpu_index);
    retro_atomic_store_release_int(&cpu->running, 1);
}

void cpu_exec_end(CPUState *cpu)
{
    g_assert(qemu_cpu_is_self(cpu));
    retro_atomic_store_release_int(&cpu->running, 0);
    trace_cpu_exec_end(cpu->cpu_index);
}

void async_safe_run_on_cpu(CPUState *cpu, run_on_cpu_func func,
                           run_on_cpu_data data)
{
    struct qemu_work_item *wi;

    wi = g_new0(struct qemu_work_item, 1);
    wi->func = func;
    wi->data = data;
    wi->free = true;
    wi->exclusive = true;

    queue_work_on_cpu(cpu, wi);
}

/* The CPU thread owns the detached batch. Keep it in CPUState so a
 * callback that re-enters work processing still observes FIFO order. */
static struct qemu_work_item *take_cpu_work(CPUState *cpu)
{
    retro_mpsc_node_t *node = cpu->work_pending;

    if (!node) {
        node = retro_mpsc_take_all(&cpu->work_queue);
    }
    if (!node) {
        return NULL;
    }
    cpu->work_pending = node->next;
    retro_atomic_fetch_sub_int(&cpu->work_count, 1);
    return container_of(node, struct qemu_work_item, node);
}

void free_queued_cpu_work(CPUState *cpu)
{
    /* CPU finalization runs after publishers and the consumer stop. */
    struct qemu_work_item *wi;

    while ((wi = take_cpu_work(cpu))) {
        if (wi->free) {
            g_free(wi);
        }
    }
}

void process_queued_cpu_work(CPUState *cpu)
{
    struct qemu_work_item *wi;

    while ((wi = take_cpu_work(cpu))) {
        if (wi->exclusive) {
            /* Preserve the callback's outside-BQL contract. The sole CPU
             * enters its exclusive scope only after leaving guest execution. */
            bql_unlock();
            start_exclusive();
            wi->func(cpu, wi->data);
            end_exclusive();
            bql_lock();
        } else {
            wi->func(cpu, wi->data);
        }
        if (wi->free) {
            g_free(wi);
        } else {
            retro_atomic_store_release_int(&wi->done, true);
            /* The waiter can now destroy its stack item. Only touch the
             * global eventcount after publishing completion. */
            retro_eventcount_notify(&qemu_work_done);
        }
    }
}

/* Add a breakpoint.  */
int cpu_breakpoint_insert(CPUState *cpu, vaddr pc, int flags,
                          CPUBreakpoint **breakpoint)
{
    CPUBreakpoint *bp;

    if (cpu->cc->gdb_adjust_breakpoint) {
        pc = cpu->cc->gdb_adjust_breakpoint(cpu, pc);
    }

    bp = g_malloc(sizeof(*bp));

    bp->pc = pc;
    bp->flags = flags;

    /* keep all GDB-injected breakpoints in front */
    if (flags & BP_GDB) {
        QTAILQ_INSERT_HEAD(&cpu->breakpoints, bp, entry);
    } else {
        QTAILQ_INSERT_TAIL(&cpu->breakpoints, bp, entry);
    }

    if (breakpoint) {
        *breakpoint = bp;
    }

    trace_breakpoint_insert(cpu->cpu_index, pc, flags);
    return 0;
}

/* Remove a specific breakpoint.  */
int cpu_breakpoint_remove(CPUState *cpu, vaddr pc, int flags)
{
    CPUBreakpoint *bp;

    if (cpu->cc->gdb_adjust_breakpoint) {
        pc = cpu->cc->gdb_adjust_breakpoint(cpu, pc);
    }

    QTAILQ_FOREACH(bp, &cpu->breakpoints, entry) {
        if (bp->pc == pc && bp->flags == flags) {
            cpu_breakpoint_remove_by_ref(cpu, bp);
            return 0;
        }
    }
    return -ENOENT;
}

/* Remove a specific breakpoint by reference.  */
void cpu_breakpoint_remove_by_ref(CPUState *cpu, CPUBreakpoint *bp)
{
    QTAILQ_REMOVE(&cpu->breakpoints, bp, entry);

    trace_breakpoint_remove(cpu->cpu_index, bp->pc, bp->flags);
    g_free(bp);
}

/* Remove all matching breakpoints. */
void cpu_breakpoint_remove_all(CPUState *cpu, int mask)
{
    CPUBreakpoint *bp, *next;

    QTAILQ_FOREACH_SAFE(bp, &cpu->breakpoints, entry, next) {
        if (bp->flags & mask) {
            cpu_breakpoint_remove_by_ref(cpu, bp);
        }
    }
}
