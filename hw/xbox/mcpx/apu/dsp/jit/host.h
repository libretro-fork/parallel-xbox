/*
 * The host side of the DSP56300 JIT (dspjit.c): the registers the
 * generated code keeps the DSP's state in and the operations it is made
 * of, the same on every host; host_x86.h and host_a64.h encode them.
 *
 * Widths: w is 0 for a 32-bit operation (the upper half of the register
 * zeroed on both hosts), 1 for 64-bit. Flags: the arithmetic and logic
 * operations set the host's condition flags as a compare would, and an
 * h_jcc or h_setcc right after uses them (HC_CARRY and HC_BORROW: an
 * h_add's carry out, an h_sub's borrow); a move, a shift, a multiply or
 * a load leaves them alone. Immediates: any 32-bit value for a 32-bit
 * operation, any 64-bit value for a 64-bit one; the host makes of them
 * what it can encode.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DSP_JIT_HOST_H
#define DSP_JIT_HOST_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum HReg {
    H_CORE,     /* the DSP's state (dsp_core_t *): callee-saved */
    H_ACC_A,    /* the accumulators, 56 bits sign-extended: callee-saved */
    H_ACC_B,
    H_V1,       /* a parallel move's values and addresses: kept across
                   the out-of-line helper calls */
    H_V2,
    H_EA1,
    H_EA2,
    H_LC,       /* a REP's count, while its loop runs: callee-saved */
    H_T0,       /* scratch within an instruction; T0 is the return value
                   of a call and the first temporary */
    H_T1,
    H_T2,
    H_T3,
    H_T4,
    H_ARG0,     /* a call's arguments; may be the same registers as the
                   temporaries, so they are set last before the call */
    H_ARG1,
    H_ARG2,
    H_NREGS
};
#define H_RET H_T0

enum HCond {
    HC_EQ, HC_NE,
    HC_B, HC_AE, HC_A, HC_BE,       /* unsigned below, above or equal ... */
    HC_L, HC_GE, HC_G, HC_LE,       /* signed */
    HC_S, HC_NS,                    /* negative */
    HC_O,                           /* signed overflow */
    HC_CARRY,                       /* an add carried out */
    HC_BORROW,                      /* a sub borrowed */
};

typedef struct Emitter {
    uint8_t *buf;
    size_t cap;
    size_t len;
    int overflow;
} Emitter;

static inline void emit_init(Emitter *e, void *buf, size_t cap)
{
    e->buf = buf;
    e->cap = cap;
    e->len = 0;
    e->overflow = 0;
}

static inline void emit_byte(Emitter *e, uint8_t b)
{
    if (e->len < e->cap) {
        e->buf[e->len] = b;
    } else {
        e->overflow = 1;
    }
    e->len++;
}

static inline void emit_u16(Emitter *e, uint16_t v)
{
    emit_byte(e, v & 0xff);
    emit_byte(e, v >> 8);
}

static inline void emit_u32(Emitter *e, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        emit_byte(e, (v >> (8 * i)) & 0xff);
    }
}

static inline void emit_u64(Emitter *e, uint64_t v)
{
    emit_u32(e, (uint32_t)v);
    emit_u32(e, (uint32_t)(v >> 32));
}

/* a jump whose target is patched in later: an offset in the code */
typedef size_t HLabel;

/* The operations, as each host encodes them:
 *
 * moves
 *   h_mov_rr(e, w, d, s)             d = s
 *   h_mov_ri(e, d, imm64)            d = imm (32-bit when it fits, zero-extended)
 *   h_ld32(e, d, base, off)          d = *(uint32_t *)(base + off)
 *   h_ld64(e, d, base, off)
 *   h_ldz16(e, d, base, off)         d = *(uint16_t *), zero-extended
 *   h_st32(e, base, off, s)          *(uint32_t *)(base + off) = s
 *   h_st32_imm(e, base, off, imm)
 *   h_st16_imm(e, base, off, imm)
 *   h_ld32_idx4(e, d, base, idx, off) d = *(uint32_t *)(base + idx * 4 + off)
 *   h_st32_idx4(e, base, idx, off, s)
 *   h_ld64_idx8(e, d, base, idx, off)
 *   h_lea(e, w, d, s, imm)           d = s + imm, no flags
 *   h_setcc(e, cc, d)                d = cc ? 1 : 0 (32-bit)
 * arithmetic and logic (flags set)
 *   h_add_rr/h_sub_rr/h_and_rr/h_or_rr/h_xor_rr(e, w, d, s)
 *   h_cmp_rr(e, w, a, b)             flags of a - b
 *   h_test_rr(e, w, a, b)            flags of a & b
 *   h_add_ri/h_sub_ri/h_and_ri/h_or_ri/h_xor_ri/h_cmp_ri(e, w, d, imm)
 *   h_test_ri(e, w, d, imm)
 *   h_neg(e, w, d)
 *   h_not(e, w, d)                   no flags
 *   h_shl_ri/h_shr_ri/h_sar_ri(e, w, d, n)   no flags
 *   h_mul_rr(e, w, d, s)             d *= s, no flags
 *   h_mul_rri(e, w, d, s, imm)       d = s * imm, no flags
 *   h_add_r_mem32/h_sub_r_mem32/h_and_r_mem32/h_or_r_mem32/h_xor_r_mem32/
 *   h_cmp_r_mem32(e, d, base, off)   d op= *(uint32_t *)(base + off)
 *   h_add_mem32_imm/h_sub_mem32_imm/h_or_mem32_imm/h_and_mem32_imm(e, base, off, imm)
 *                                    *(uint32_t *)(base + off) op= imm
 *   h_add_mem32_r(e, base, off, s)
 *   h_cmp_mem32_imm/h_cmp_mem16_imm/h_cmp_mem8_imm(e, base, off, imm)
 *   h_test_mem8_imm(e, base, off, imm)
 * control
 *   h_jcc(e, cc) -> HLabel           a conditional jump, target patched in
 *   h_jmp(e) -> HLabel
 *   h_patch(e, label, target)        the jump's target: an offset in the code
 *   h_jcc_to(e, cc, target), h_jmp_to(e, target)   to an offset already emitted
 *   h_jcc_abs(e, cc, ptr), h_jmp_abs(e, ptr)       to an address (within the cache)
 *   h_jmp_r(e, r)
 *   h_call(e, fn)                    a C function; the temporaries and the
 *                                    arguments are clobbered, nothing else
 *   h_stub_call_begin(e)/h_stub_call_end(e)   around a call made from a
 *                                    stub: V1, V2, EA1, EA2 kept
 * the code's way in and out (emit_trampolines)
 *   h_enter(e)                       enter(core, fn): the callee-saved
 *                                    registers kept, H_CORE set, into fn
 *   h_leave(e)                       back to enter's caller
 *   h_frame_st32(e, slot, s), h_frame_ld32(e, d, slot)  a word in the frame
 */

#if defined(__x86_64__) || defined(_M_X64)
#include "host_x86.h"
#elif defined(__aarch64__) || defined(_M_ARM64)
#include "host_a64.h"
#endif

#endif
