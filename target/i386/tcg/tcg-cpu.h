/*
 * i386 TCG cpu class initialization functions
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TCG_CPU_H
#define TCG_CPU_H

#include "target/i386/cpu.h"
#include "accel/tcg/tb-cpu-state.h"
#include "qemu/cpu-boost.h"

/* Share the state calculation with the Xbox dispatcher without an ABI
 * call. The generic target hook uses the same calculation. */
static inline TCGTBCPUState x86_tb_cpu_state(CPUState *cs)
{
    CPUX86State *env = cpu_env(cs);
    uint32_t flags, cs_base;
    vaddr pc;

    flags = env->hflags |
        (env->eflags & (IOPL_MASK | TF_MASK | RF_MASK | VM_MASK | AC_MASK));
    if (xemu_cpu_boost) {
        flags |= tb_cf_flags(x86_cc_op_cf_class(env->cc_op));
    }
    if (env->hflags & HF_CS64_MASK) {
        cs_base = 0;
        pc = env->eip;
    } else {
        cs_base = env->segs[R_CS].base;
        pc = (uint32_t)(cs_base + env->eip);
    }

    return (TCGTBCPUState){ .pc = pc, .flags = flags, .cs_base = cs_base };
}

#define XSAVE_FCW_FSW_OFFSET    0x000
#define XSAVE_FTW_FOP_OFFSET    0x004
#define XSAVE_CWD_RIP_OFFSET    0x008
#define XSAVE_CWD_RDP_OFFSET    0x010
#define XSAVE_MXCSR_OFFSET      0x018
#define XSAVE_ST_SPACE_OFFSET   0x020
#define XSAVE_XMM_SPACE_OFFSET  0x0a0
#define XSAVE_XSTATE_BV_OFFSET  0x200
#define XSAVE_AVX_OFFSET        0x240
#define XSAVE_BNDREG_OFFSET     0x3c0
#define XSAVE_BNDCSR_OFFSET     0x400
#define XSAVE_OPMASK_OFFSET     0x440
#define XSAVE_ZMM_HI256_OFFSET  0x480
#define XSAVE_HI16_ZMM_OFFSET   0x680
#define XSAVE_PKRU_OFFSET       0xa80

typedef struct X86XSaveArea {
    X86LegacyXSaveArea legacy;
    X86XSaveHeader header;

    /* Extended save areas: */

    /* AVX State: */
    XSaveAVX avx_state;

    /* Ensure that XSaveBNDREG is properly aligned. */
    uint8_t padding[XSAVE_BNDREG_OFFSET
                    - sizeof(X86LegacyXSaveArea)
                    - sizeof(X86XSaveHeader)
                    - sizeof(XSaveAVX)];

    /* MPX State: */
    XSaveBNDREG bndreg_state;
    XSaveBNDCSR bndcsr_state;
    /* AVX-512 State: */
    XSaveOpmask opmask_state;
    XSaveZMM_Hi256 zmm_hi256_state;
    XSaveHi16_ZMM hi16_zmm_state;
    /* PKRU State: */
    XSavePKRU pkru_state;
} X86XSaveArea;

QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, legacy.fcw) != XSAVE_FCW_FSW_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, legacy.ftw) != XSAVE_FTW_FOP_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, legacy.fpip) != XSAVE_CWD_RIP_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, legacy.fpdp) != XSAVE_CWD_RDP_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, legacy.mxcsr) != XSAVE_MXCSR_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, legacy.fpregs) != XSAVE_ST_SPACE_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, legacy.xmm_regs) != XSAVE_XMM_SPACE_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, avx_state) != XSAVE_AVX_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, bndreg_state) != XSAVE_BNDREG_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, bndcsr_state) != XSAVE_BNDCSR_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, opmask_state) != XSAVE_OPMASK_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, zmm_hi256_state) != XSAVE_ZMM_HI256_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, hi16_zmm_state) != XSAVE_HI16_ZMM_OFFSET);
QEMU_BUILD_BUG_ON(offsetof(X86XSaveArea, pkru_state) != XSAVE_PKRU_OFFSET);

extern const TCGCPUOps x86_tcg_ops;

bool tcg_cpu_realizefn(CPUState *cs, Error **errp);

int x86_mmu_index_pl(CPUX86State *env, unsigned pl);

/* Generic target hook; the Xbox dispatcher shares the inline calculation. */
TCGTBCPUState x86_get_tb_cpu_state(CPUState *cs);

#endif /* TCG_CPU_H */
