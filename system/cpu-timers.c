/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "system/cpus.h"
#include "qemu/main-loop.h"
#include "qemu/option.h"
#include "system/runstate.h"
#include "hw/core/cpu.h"
#include "system/cpu-timers.h"
#include "system/cpu-timers-internal.h"
#include "exec/icount.h"
#include "system/libretro-vcpu.h"

/* Host elapsed time is read and updated on the emulation thread, which
 * also runs the vCPU and main-loop callbacks. Cross-thread guest-time
 * observers use icount_get()'s published timestamp instead. */
int64_t cpu_get_clock(void)
{
    int64_t time = timers_state.cpu_clock_offset;

    if (retro_atomic_load_relaxed_int(&timers_state.cpu_ticks_enabled)) {
        time += get_clock();
    }
    return time;
}

/*
 * enable cpu_get_ticks()
 * Emulation-thread lifecycle operation; caller holds BQL.
 */
void cpu_enable_ticks(void)
{
    if (!retro_atomic_load_relaxed_int(&timers_state.cpu_ticks_enabled)) {
        timers_state.cpu_ticks_offset -= cpu_get_host_ticks();
        timers_state.cpu_clock_offset -= get_clock();
        retro_atomic_store_relaxed_int(&timers_state.cpu_ticks_enabled, 1);
    }

    /* Also refresh after a failed load before execution resumes. */
    icount_publish_time();
}

/*
 * disable cpu_get_ticks() : the clock is stopped. You must not call
 * cpu_get_ticks() after that.
 * Emulation-thread lifecycle operation; caller holds BQL.
 */
void cpu_disable_ticks(void)
{
    if (retro_atomic_load_relaxed_int(&timers_state.cpu_ticks_enabled)) {
        timers_state.cpu_ticks_offset += cpu_get_host_ticks();
        timers_state.cpu_clock_offset = cpu_get_clock();
        retro_atomic_store_relaxed_int(&timers_state.cpu_ticks_enabled, 0);
    }
}

/*
 * The libretro core's frame clock: QEMU_CLOCK_VIRTUAL is the guest's
 * instructions (icount), and while the guest idles with nothing in flight
 * it is moved on to the next deadline instead of waited out. Without
 * icount, the host clock's time the guest ran, moved on the same way.
 * Only forwards.
 */
void cpu_clock_skip_to(int64_t vt)
{
    int64_t now;

    if (icount_enabled()) {
        icount_skip_to(vt);
        return;
    }
    now = cpu_get_clock();
    if (vt > now) {
        /* The guest's TSC follows this clock when icount is disabled. */
        timers_state.cpu_clock_offset += vt - now;
    }
}

bool cpu_ticks_enabled(void)
{
    return retro_atomic_load_relaxed_int(&timers_state.cpu_ticks_enabled);
}

static bool icount_state_needed(void *opaque)
{
    return icount_enabled();
}

static bool warp_timer_state_needed(void *opaque)
{
    TimersState *s = opaque;
    return s->icount_warp_timer != NULL;
}

static bool adjust_timers_state_needed(void *opaque)
{
    TimersState *s = opaque;
    return s->icount_rt_timer != NULL;
}

static bool icount_shift_state_needed(void *opaque)
{
    return icount_enabled() == ICOUNT_ADAPTATIVE;
}

/*
 * Subsection for warp timer migration is optional, because may not be created
 */
static const VMStateDescription icount_vmstate_warp_timer = {
    .name = "timer/icount/warp_timer",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = warp_timer_state_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(vm_clock_warp_start, TimersState),
        VMSTATE_TIMER_PTR(icount_warp_timer, TimersState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription icount_vmstate_adjust_timers = {
    .name = "timer/icount/timers",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = adjust_timers_state_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER_PTR(icount_rt_timer, TimersState),
        VMSTATE_TIMER_PTR(icount_vm_timer, TimersState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription icount_vmstate_shift = {
    .name = "timer/icount/shift",
    .version_id = 2,
    .minimum_version_id = 2,
    .needed = icount_shift_state_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_INT16(icount_time_shift, TimersState),
        VMSTATE_INT64(last_delta, TimersState),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * This is a subsection for icount migration.
 */
static const VMStateDescription icount_vmstate_timers = {
    .name = "timer/icount",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = icount_state_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(qemu_icount_bias, TimersState),
        VMSTATE_INT64(qemu_icount, TimersState),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &icount_vmstate_warp_timer,
        &icount_vmstate_adjust_timers,
        &icount_vmstate_shift,
        NULL
    }
};

/* Fields and subsections are fully restored before this owner callback.
 * Runtime publication is rebuilt, not added to the savestate wire format. */
static int cpu_timers_post_load(void *opaque, int version_id)
{
    icount_publish_time();
    return 0;
}

static const VMStateDescription vmstate_timers = {
    .name = "timer",
    .post_load = cpu_timers_post_load,
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT64(cpu_ticks_offset, TimersState),
        VMSTATE_UNUSED(8),
        VMSTATE_INT64_V(cpu_clock_offset, TimersState, 2),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &icount_vmstate_timers,
        NULL
    }
};

static void do_nothing(CPUState *cpu, run_on_cpu_data unused)
{
}

void qemu_timer_notify_cb(void *opaque, QEMUClockType type)
{
    /* The core's emulator thread is the vCPU and the main loop at once, and
     * works out the guest's next slice from the timers before each one
     * (ui/libretro.c): a guest timer set in a slice ends it, one set from
     * another thread ends it and wakes the loop, one set between slices
     * needs nothing. (No work is queued on the vCPU, which allocated an
     * item for each timer set.) */
    if (icount_enabled() && type == QEMU_CLOCK_VIRTUAL) {
        if (current_cpu && qemu_cpu_is_self(current_cpu)) {
            lr_vcpu_exit(current_cpu);
        } else if (first_cpu && !qemu_cpu_is_self(first_cpu)) {
            qemu_cpu_kick(first_cpu);
        }
        return;
    }
    if (!icount_enabled() || type != QEMU_CLOCK_VIRTUAL) {
        qemu_notify_event();
        return;
    }

    if (qemu_in_vcpu_thread()) {
        /*
         * A CPU is currently running; send it out of the
         * tcg_cpu_exec() loop so it will recalculate its
         * icount deadline immediately.
         */
        cpu_exit(current_cpu);
    } else if (first_cpu) {
        /*
         * cpu_exit() is not enough to kick a halted CPU out of
         * qemu_tcg_wait_io_event.  async_run_on_cpu, instead,
         * causes cpu_thread_is_idle to return false.  This way,
         * handle_icount_deadline can run.
         * If we have no CPUs at all for some reason, we don't
         * need to do anything.
         */
        async_run_on_cpu(first_cpu, do_nothing, RUN_ON_CPU_NULL);
    }
}

TimersState timers_state;

/* Register migrated fields; the owner-thread runtime state starts at zero. */
void cpu_timers_init(void)
{
    vmstate_register(NULL, 0, &vmstate_timers, &timers_state);
}
