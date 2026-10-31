/*
 * The per-CPU TranslationBlock jump cache.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_TB_JMP_CACHE_H
#define ACCEL_TCG_TB_JMP_CACHE_H

#include "qemu/rcu.h"
#include "exec/cpu-common.h"
#include "qemu/cpu-boost.h"

/* CPU boost widens the jump cache to 65536 entries (stock: 4096); the array
 * is sized for the wider one. */
#define TB_JMP_CACHE_BITS (xemu_cpu_boost ? 16 : 12)
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)
#define TB_JMP_CACHE_SIZE_MAX (1 << 16)

/*
 * Invalidated in parallel; all accesses to 'tb' must be atomic.
 * A valid entry is read/written by a single CPU, therefore relaxed
 * loads do and pc is always consistent with a non-NULL value of 'tb'.  Strictly speaking pc is only needed for
 * CF_PCREL, but it's used always for simplicity.
 */
typedef struct CPUJumpCache {
    struct rcu_head rcu;
    struct {
        retro_atomic_ptr_t tb;      /* TranslationBlock * */
        vaddr pc;
    } array[TB_JMP_CACHE_SIZE_MAX], classes[TB_JMP_CACHE_SIZE_MAX];
    /* classes is consulted only after a primary miss; preallocated so class
     * conflicts cannot allocate during emulation. */
} CPUJumpCache;

#endif /* ACCEL_TCG_TB_JMP_CACHE_H */
