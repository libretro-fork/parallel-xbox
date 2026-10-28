/*
 * The libretro core's TCG vCPU, run by the emulator thread (ui/libretro.c)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TCG_ACCEL_OPS_LIBRETRO_H
#define TCG_ACCEL_OPS_LIBRETRO_H

void lr_start_vcpu(CPUState *cpu);
void lr_kick_vcpu(CPUState *cpu);

#endif
