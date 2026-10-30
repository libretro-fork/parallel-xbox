/*
 * Software MMU support
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef ACCEL_TCG_CPU_LDST_COMMON_H
#define ACCEL_TCG_CPU_LDST_COMMON_H

#ifndef CONFIG_TCG
#error Can only include this header with TCG
#endif

#include "exec/memopidx.h"
#include "exec/vaddr.h"
#include "exec/mmu-access-type.h"
#include "qemu/int128.h"

uint8_t cpu_ldb_mmu(CPUArchState *env, vaddr ptr, MemOpIdx oi, uintptr_t ra);
uint16_t cpu_ldw_mmu(CPUArchState *env, vaddr ptr, MemOpIdx oi, uintptr_t ra);
uint32_t cpu_ldl_mmu(CPUArchState *env, vaddr ptr, MemOpIdx oi, uintptr_t ra);
uint64_t cpu_ldq_mmu(CPUArchState *env, vaddr ptr, MemOpIdx oi, uintptr_t ra);
Int128 cpu_ld16_mmu(CPUArchState *env, vaddr addr, MemOpIdx oi, uintptr_t ra);

void cpu_stb_mmu(CPUArchState *env, vaddr ptr, uint8_t val,
                 MemOpIdx oi, uintptr_t ra);
void cpu_stw_mmu(CPUArchState *env, vaddr ptr, uint16_t val,
                 MemOpIdx oi, uintptr_t ra);
void cpu_stl_mmu(CPUArchState *env, vaddr ptr, uint32_t val,
                 MemOpIdx oi, uintptr_t ra);
void cpu_stq_mmu(CPUArchState *env, vaddr ptr, uint64_t val,
                 MemOpIdx oi, uintptr_t ra);
void cpu_st16_mmu(CPUArchState *env, vaddr addr, Int128 val,
                  MemOpIdx oi, uintptr_t ra);

uint8_t cpu_ldb_code_mmu(CPUArchState *env, vaddr addr,
                         MemOpIdx oi, uintptr_t ra);
uint16_t cpu_ldw_code_mmu(CPUArchState *env, vaddr addr,
                          MemOpIdx oi, uintptr_t ra);
uint32_t cpu_ldl_code_mmu(CPUArchState *env, vaddr addr,
                          MemOpIdx oi, uintptr_t ra);
uint64_t cpu_ldq_code_mmu(CPUArchState *env, vaddr addr,
                          MemOpIdx oi, uintptr_t ra);

#endif /* ACCEL_TCG_CPU_LDST_COMMON_H */
