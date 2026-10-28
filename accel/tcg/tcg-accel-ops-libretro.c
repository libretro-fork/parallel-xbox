/*
 * The libretro core's TCG vCPU: no thread of its own.
 *
 * The emulator thread (ui/libretro.c) is QEMU's main loop and the vCPU at
 * once: it runs the guest in slices, between which it runs the timers and
 * bottom halves that fell due, until the frame retro_run asked for is done.
 * Nothing else takes the BQL to run guest code, so the vCPU never waits for
 * it; a slice ends when something calls cpu_exit() (a device, the frontend
 * thread at a timer deadline it was given) or the guest halts.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/guest-random.h"
#include "qemu/rcu.h"
#include "system/tcg.h"
#include "system/cpus.h"
#include "exec/cpu-common.h"
#include "tcg/startup.h"
#include "hw/core/cpu.h"
#include "tcg-accel-ops.h"
#include "tcg-accel-ops-libretro.h"
#include "system/libretro-vcpu.h"

static bool lr_tcg_registered;

void lr_start_vcpu(CPUState *cpu)
{
    g_assert(tcg_enabled());
    tcg_cpu_init_cflags(cpu, false);

    /* The thread qemu_init runs on runs the guest: it is the vCPU's */
    qemu_thread_get_self(cpu->thread);
    cpu->thread_id = qemu_get_thread_id();
    cpu->neg.can_do_io = true;
    if (!lr_tcg_registered) {
        tcg_register_thread();
        lr_tcg_registered = true;
    }
    qemu_guest_random_seed_thread_part2(cpu->random_seed);
    cpu_thread_signal_created(cpu);
}

/* A device or another thread wants the guest to stop and look: leave the
 * translated code, and wake the main loop if it waits for events */
void lr_kick_vcpu(CPUState *cpu)
{
    tcg_kick_vcpu_thread(cpu);
    qemu_notify_event();
}

void lr_vcpu_exit(CPUState *cpu)
{
    tcg_kick_vcpu_thread(cpu);
}

void lr_vcpu_process_work(CPUState *cpu)
{
    /* as on its own thread, the vCPU is current while its work runs */
    SET_current_cpu(cpu);
    process_queued_cpu_work(cpu);
    SET_current_cpu(NULL);
}

bool lr_vcpu_runnable(CPUState *cpu)
{
    return cpu_can_run(cpu) && !cpu_thread_is_idle(cpu);
}

int lr_vcpu_run_slice(CPUState *cpu)
{
    int r;

    /* exit_request is not cleared here: the caller clears it before it
     * gives anyone a reason to set it for this slice */
    qatomic_set_mb(&cpu->thread_kicked, false);
    if (cpu->stop) {
        cpu->stop = false;
        cpu->stopped = true;
        return EXCP_HALTED;
    }
    if (!cpu_work_list_empty(cpu)) {
        lr_vcpu_process_work(cpu);
    }
    if (!lr_vcpu_runnable(cpu)) {
        return EXCP_HALTED;
    }

    bql_unlock();
    SET_current_cpu(cpu);
    r = tcg_cpu_exec(cpu);
    SET_current_cpu(NULL);
    bql_lock();

    if (r == EXCP_DEBUG) {
        cpu_handle_guest_debug(cpu);
    } else if (r == EXCP_ATOMIC) {
        bql_unlock();
        cpu_exec_step_atomic(cpu);
        bql_lock();
    }
    return r;
}
