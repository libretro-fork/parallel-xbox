/*
 * Probe guest virtual addresses for access permissions.
 *
 * Copyright (c) 2003 Fabrice Bellard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef ACCEL_TCG_PROBE_H
#define ACCEL_TCG_PROBE_H

#include "exec/mmu-access-type.h"
#include "exec/vaddr.h"

/**
 * probe_access:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @size: size of the access
 * @access_type: read, write or execute permission
 * @mmu_idx: MMU index to use for lookup
 * @retaddr: return address for unwinding
 *
 * Look up the guest virtual address @addr.  Raise an exception if the
 * page does not satisfy @access_type.  Raise an exception if the
 * access (@addr, @size) hits a watchpoint.  For writes, mark a clean
 * page as dirty.
 *
 * Finally, return the host address for a page that is backed by RAM,
 * or NULL if the page requires I/O.
 */
void *probe_access(CPUArchState *env, vaddr addr, int size,
                   MMUAccessType access_type, int mmu_idx, uintptr_t retaddr);

static inline void *probe_write(CPUArchState *env, vaddr addr, int size,
                                int mmu_idx, uintptr_t retaddr)
{
    return probe_access(env, addr, size, MMU_DATA_STORE, mmu_idx, retaddr);
}

static inline void *probe_read(CPUArchState *env, vaddr addr, int size,
                               int mmu_idx, uintptr_t retaddr)
{
    return probe_access(env, addr, size, MMU_DATA_LOAD, mmu_idx, retaddr);
}

#ifndef CONFIG_USER_ONLY

/**
 * probe_access_full_mmu:
 * Like probe_access_full, except:
 *
 * This function is intended to be used for page table accesses by
 * the target mmu itself.  Since such page walking happens while
 * handling another potential mmu fault, this function never raises
 * exceptions (akin to @nonfault true for probe_access_full).
 * Likewise this function does not trigger plugin instrumentation.
 */
int probe_access_full_mmu(CPUArchState *env, vaddr addr, int size,
                          MMUAccessType access_type, int mmu_idx,
                          void **phost, CPUTLBEntryFull **pfull);

#endif /* !CONFIG_USER_ONLY */

#endif
