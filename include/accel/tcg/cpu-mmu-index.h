/*
 * cpu_mmu_index()
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_CPU_MMU_INDEX_H
#define ACCEL_TCG_CPU_MMU_INDEX_H

#include "hw/core/cpu.h"
#include "accel/tcg/cpu-ops.h"
#include "tcg/debug-assert.h"
#ifdef COMPILING_PER_TARGET
#endif

/**
 * cpu_mmu_index:
 * @env: The cpu environment
 * @ifetch: True for code access, false for data access.
 *
 * Return the core mmu index for the current translation regime.
 * This function is used by generic TCG code paths.
 */
static inline int cpu_mmu_index(CPUState *cs, bool ifetch)
{
#ifdef COMPILING_PER_TARGET
#endif

    int ret = cs->cc->tcg_ops->mmu_index(cs, ifetch);
    tcg_debug_assert(ret >= 0 && ret < NB_MMU_MODES);
    return ret;
}

#endif /* ACCEL_TCG_CPU_MMU_INDEX_H */
