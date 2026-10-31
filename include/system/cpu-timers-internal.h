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

#ifndef TIMERS_STATE_H
#define TIMERS_STATE_H

/* timers state, for sharing between icount and cpu-timers */

typedef struct TimersState {
    /* Protected by BQL.  */
    int64_t cpu_ticks_offset;

    /* Raw clock state has one emulation-thread owner in the core.
     * Off-thread virtual-time readers use published_icount_ns below. */
    retro_atomic_int_t cpu_ticks_enabled;

    /* Conversion factor from emulated instructions to virtual clock ticks.  */
    int16_t icount_time_shift;
    /* Icount delta used for shift auto adjust. */
    int64_t last_delta;

    /* Compensate for varying guest execution speed.  */
    int64_t qemu_icount_bias;

    int64_t vm_clock_warp_start;
    int64_t cpu_clock_offset;

    /* Only written by TCG thread */
    int64_t qemu_icount;

    /* Runtime-only coherent virtual time for observing threads. The
     * emulation thread owns count/bias updates; this field is not migrated. */
    retro_atomic_64_t published_icount_ns;

    /* for adjusting icount */
    QEMUTimer *icount_rt_timer;
    QEMUTimer *icount_vm_timer;
    QEMUTimer *icount_warp_timer;
} TimersState;

extern TimersState timers_state;

/* Publish after an owner-thread count/bias update or state restoration. */
void icount_publish_time(void);

#endif /* TIMERS_STATE_H */
