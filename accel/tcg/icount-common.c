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
#include "system/cpus.h"
#include "system/cpu-timers.h"
#include "hw/core/cpu.h"
#include "exec/icount.h"
#include "system/cpu-timers-internal.h"
#include "system/libretro-vcpu.h"

/* Migration fields stay plain for VMState's type checks. Keep whole-width
 * atomic accessors; only the owner combines raw fields, and observers read
 * the separately published timestamp. */
#define TS_LOAD64(f) \
    retro_atomic_load_relaxed_64((retro_atomic_64_t *)&timers_state.f)
#define TS_STORE64(f, v) \
    retro_atomic_store_relaxed_64((retro_atomic_64_t *)&timers_state.f, (v))

#if !defined(RETRO_ATOMIC_LOCK_FREE) || !defined(RETRO_ATOMIC_HAS_64)
#error "Virtual clock publication requires lock-free 64-bit atomics"
#endif
#if defined(__GNUC__)
_Static_assert(__atomic_always_lock_free(sizeof(retro_atomic_64_t), 0),
               "Virtual clock timestamps must be lock-free");
#endif

/* Count and bias have one owner in the libretro accelerator. Observers
 * consume one complete timestamp, never the raw multi-field state. */
void icount_publish_time(void)
{
    int64_t ns = TS_LOAD64(qemu_icount_bias) +
                 icount_to_ns(TS_LOAD64(qemu_icount));
    retro_atomic_store_release_64(&timers_state.published_icount_ns, ns);
}

/* Disabled until the core initializes its fixed Xbox instruction clock. */
ICountMode use_icount = ICOUNT_DISABLED;

void libretro_instruction_clock_init(void)
{
    use_icount = ICOUNT_PRECISE;
    /* Keep the legacy VMState fields and wire layout, with no host-paced
     * timers or adaptive rate changes attached to them. */
    timers_state.icount_time_shift = 0;
    timers_state.last_delta = 0;
    timers_state.vm_clock_warp_start = -1;
    icount_publish_time();
}

/*
 * The current number of executed instructions is based on what we
 * originally budgeted minus the current state of the decrementing
 * icount counters in extra/u16.low.
 */
static int64_t icount_get_executed(CPUState *cpu)
{
    return (cpu->icount_budget -
            (retro_atomic_load_relaxed_u16(&cpu->neg.icount_decr.u16.low)
             + cpu->icount_extra));
}

/*
 * Update the global shared timer_state.qemu_icount to take into
 * account executed instructions. This is done by the TCG vCPU
 * thread so the main-loop can see time has moved forward.
 */
static void icount_update_owner(CPUState *cpu)
{
    int64_t executed = icount_get_executed(cpu);

    if (!executed) {
        return;
    }
    cpu->icount_budget -= executed;

    TS_STORE64(qemu_icount,
                    timers_state.qemu_icount + executed);
    icount_publish_time();
}

/*
 * Update the global shared timer_state.qemu_icount to take into
 * account executed instructions. This is done by the TCG vCPU
 * thread so the main-loop can see time has moved forward.
 */
void icount_update(CPUState *cpu)
{
    icount_update_owner(cpu);
}

static int64_t icount_get_raw_owner(void)
{
    CPUState *cpu = current_cpu;

    if (cpu && retro_atomic_load_relaxed_int(&cpu->running)) {
        if (!cpu->neg.can_do_io) {
            /* never end the frontend's process: the count as of the
             * block's start, a few instructions behind */
            return TS_LOAD64(qemu_icount);
        }
        /* Take into account what has run */
        icount_update_owner(cpu);
    }
    /* The emulation thread owns the raw fields. Other threads use publication. */
    return TS_LOAD64(qemu_icount);
}

static int64_t icount_get_owner(void)
{
    int64_t icount = icount_get_raw_owner();
    return TS_LOAD64(qemu_icount_bias) +
        icount_to_ns(icount);
}

/* Return the virtual CPU time, based on the instruction counter.  */
int64_t icount_get(void)
{
    CPUState *cpu = current_cpu;

    /* The emulation thread can account for the current slice. Outside an
     * I/O-safe boundary retain the last committed instruction count, as
     * before. Observers never inspect owner-only count/bias/budget fields. */
    if (cpu && retro_atomic_load_relaxed_int(&cpu->running) &&
        cpu->neg.can_do_io) {
        icount_update_owner(cpu);
    }
    return retro_atomic_load_acquire_64(&timers_state.published_icount_ns);
}

/* Executed instructions use a fixed conversion independent of host load.
 * The frame owner accounts separately for idle/device intervals. 1.977 ns an
 * instruction, 1.45 of the 733.33 MHz Pentium III's clock periods, the
 * rate standalone's Real hardware speed paces its CPU at (XEMU_PACE_PS,
 * accel/tcg/cpu-exec.c: judged on footage of Virtua Cop 3's cinematics,
 * and the games that hold 60 frames a second on the cabinet). The guest's
 * TSC counts 733.33 MHz of this time (hw/i386/x86-cpu.c). */
#define LR_ICOUNT_NS_NUM 1977
#define LR_ICOUNT_NS_DEN 1000

int64_t icount_to_ns(int64_t icount)
{
    return icount * LR_ICOUNT_NS_NUM / LR_ICOUNT_NS_DEN;
}

/* The frame owner proved an idle interval with nothing under way and
 * selected its next deadline: advance guest time to vt at once. */
void icount_skip_to(int64_t vt)
{
    int64_t now = icount_get_owner();

    if (vt > now) {
        TS_STORE64(qemu_icount_bias,
                   timers_state.qemu_icount_bias + vt - now);
        icount_publish_time();
    }
}

int64_t icount_round(int64_t count)
{
    /* the instructions that take at least count ns */
    return (count * LR_ICOUNT_NS_DEN + LR_ICOUNT_NS_NUM - 1) /
           LR_ICOUNT_NS_NUM;
}

void icount_notify_exit(void)
{
    assert(icount_enabled());

    /* A bottom half was scheduled: the guest's slice ends so the core's
     * loop runs it (from another thread, the main loop's notify hook makes
     * it leave, ui/libretro.c). Kicking the vCPU (cpu_exit too) would wake
     * the main loop with another bottom half, and come back here. */
    if (current_cpu) {
        lr_vcpu_exit(current_cpu);
    }
}
