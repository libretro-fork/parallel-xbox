/*
 * The libretro core's vCPU, run in slices by the emulator thread
 * (accel/tcg/tcg-accel-ops-libretro.c)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef SYSTEM_LIBRETRO_VCPU_H
#define SYSTEM_LIBRETRO_VCPU_H

/* Makes the guest leave its translated code at the next block, from any
 * thread; wakes nothing */
void lr_vcpu_exit(CPUState *cpu);
/* Runs the work queued for the vCPU (run_on_cpu and friends), as the vCPU,
 * without running the guest; under the BQL */
void lr_vcpu_process_work(CPUState *cpu);
/* Whether the guest can run now: not stopped, not halted without work */
bool lr_vcpu_runnable(CPUState *cpu);
/* Runs the guest for at most ns of its time (its instructions, icount),
 * less when something calls cpu_exit() or it halts; the BQL is
 * held on entry and on return, and dropped while the guest runs. The caller
 * clears cpu->exit_request before it publishes anything a kick for this
 * slice depends on, so no kick between the two is lost. Returns
 * the cpu_exec() exit code, EXCP_HALTED when it could not run. */
int lr_vcpu_run_slice(CPUState *cpu, int64_t ns);

#endif
