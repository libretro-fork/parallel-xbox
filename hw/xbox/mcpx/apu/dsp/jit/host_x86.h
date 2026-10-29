/*
 * The DSP56300 JIT's host operations (host.h) on x86-64, SysV and Win64:
 * the encoders are emit_x86.h's.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DSP_JIT_HOST_X86_H
#define DSP_JIT_HOST_X86_H

#include "emit_x86.h"

/* HReg to the host's registers: rbx and r12-r15 callee-saved on both ABIs
 * and kept by the trampolines, rbp too (the REP count); the values and
 * addresses in r8-r11, which the stubs' calls save; the temporaries in
 * rax, rcx, rdx, r14, r15. rsi and rdi (callee-saved on Win64, saved by
 * the trampolines on both) are the encoders' own scratch, for an
 * immediate an instruction cannot carry. */
static const uint8_t hreg_x86[H_NREGS] = {
    [H_CORE] = RBX, [H_ACC_A] = R12, [H_ACC_B] = R13,
    [H_V1] = R8, [H_V2] = R9, [H_EA1] = R10, [H_EA2] = R11,
    [H_LC] = RBP,
    [H_T0] = RAX, [H_T1] = RCX, [H_T2] = RDX, [H_T3] = R14, [H_T4] = R15,
    [H_ARG0] = ARG0, [H_ARG1] = ARG1, [H_ARG2] = ARG2,
};
#define XR(h) (hreg_x86[h])
#define X86_SCRATCH RSI

static const uint8_t hcc_x86[] = {
    [HC_EQ] = CC_E, [HC_NE] = CC_NE,
    [HC_B] = CC_B, [HC_AE] = CC_AE, [HC_A] = CC_A, [HC_BE] = CC_BE,
    [HC_L] = CC_L, [HC_GE] = CC_GE, [HC_G] = CC_G, [HC_LE] = CC_LE,
    [HC_S] = CC_S, [HC_NS] = CC_NS,
    [HC_O] = CC_O,
    [HC_CARRY] = CC_B, [HC_BORROW] = CC_B,
};
#define XCC(c) (hcc_x86[c])

/* the frame the trampolines make: the Win64 shadow space for the calls
 * and the slots (h_frame_*) */
#define X86_FRAME 40
#define X86_SLOTS 32   /* one slot: 40 - 32 */

/* --- moves --- */

static inline void h_mov_rr(Emitter *e, int w, int d, int s)
{
    x86_mov_rr(e, w, XR(d), XR(s));
}

static inline void h_mov_ri(Emitter *e, int d, uint64_t imm)
{
    if (imm <= 0xFFFFFFFFu) {
        x86_mov_r32_imm(e, XR(d), (uint32_t)imm);
    } else {
        x86_mov_r64_imm(e, XR(d), imm);
    }
}

static inline void h_ld32(Emitter *e, int d, int base, int32_t off)
{
    x86_mov_r32_mem(e, XR(d), XR(base), off);
}

static inline void h_ld64(Emitter *e, int d, int base, int32_t off)
{
    x86_mov_r64_mem(e, XR(d), XR(base), off);
}

static inline void h_ldz16(Emitter *e, int d, int base, int32_t off)
{
    x86_movzx_r32_mem16(e, XR(d), XR(base), off);
}

static inline void h_st32(Emitter *e, int base, int32_t off, int s)
{
    x86_mov_mem_r32(e, XR(base), off, XR(s));
}

static inline void h_st32_imm(Emitter *e, int base, int32_t off, uint32_t imm)
{
    x86_mov_mem32_imm(e, XR(base), off, imm);
}

static inline void h_st16_imm(Emitter *e, int base, int32_t off, uint16_t imm)
{
    x86_mov_mem16_imm(e, XR(base), off, imm);
}

static inline void h_ld32_idx4(Emitter *e, int d, int base, int idx, int32_t off)
{
    x86_mov_r32_mem_idx4(e, XR(d), XR(base), XR(idx), off);
}

static inline void h_st32_idx4(Emitter *e, int base, int idx, int32_t off, int s)
{
    x86_mov_mem_idx4_r32(e, XR(base), XR(idx), off, XR(s));
}

static inline void h_ld64_idx8(Emitter *e, int d, int base, int idx, int32_t off)
{
    x86_mov_r64_mem_idx8(e, XR(d), XR(base), XR(idx), off);
}

static inline void h_lea(Emitter *e, int w, int d, int s, int32_t imm)
{
    emit_rex(e, w, XR(d), XR(s));
    emit_byte(e, 0x8D);
    emit_modrm_disp32(e, XR(d), XR(s), imm);
}

static inline void h_setcc(Emitter *e, int cc, int d)
{
    x86_setcc_r8(e, XCC(cc), XR(d));
    x86_movzx_r32_r8(e, XR(d), XR(d));
}

/* --- arithmetic and logic --- */

#define h_add_rr(e, w, d, s) x86_add_rr(e, w, XR(d), XR(s))
#define h_sub_rr(e, w, d, s) x86_sub_rr(e, w, XR(d), XR(s))
#define h_and_rr(e, w, d, s) x86_and_rr(e, w, XR(d), XR(s))
#define h_or_rr(e, w, d, s)  x86_or_rr(e, w, XR(d), XR(s))
#define h_xor_rr(e, w, d, s) x86_xor_rr(e, w, XR(d), XR(s))
#define h_cmp_rr(e, w, a, b) x86_cmp_rr(e, w, XR(a), XR(b))
#define h_test_rr(e, w, a, b) x86_test_rr(e, w, XR(a), XR(b))

/* op r, imm: as the instruction's sign-extended imm32 when the value is
 * one, else through the scratch register */
static inline void h_alu_ri(Emitter *e, int ext, uint8_t op_rr, int w, int d, int64_t imm)
{
    if (!w) {
        imm = (int32_t)(uint32_t)imm;
    }
    if (imm >= INT32_MIN && imm <= INT32_MAX) {
        x86_alu_ri(e, ext, w, XR(d), (int32_t)imm);
    } else {
        x86_mov_r64_imm(e, X86_SCRATCH, (uint64_t)imm);
        x86_alu_rr(e, op_rr, w, XR(d), X86_SCRATCH);
    }
}
#define h_add_ri(e, w, d, i) h_alu_ri(e, 0, 0x01, w, d, i)
#define h_or_ri(e, w, d, i)  h_alu_ri(e, 1, 0x09, w, d, i)
#define h_and_ri(e, w, d, i) h_alu_ri(e, 4, 0x21, w, d, i)
#define h_sub_ri(e, w, d, i) h_alu_ri(e, 5, 0x29, w, d, i)
#define h_xor_ri(e, w, d, i) h_alu_ri(e, 6, 0x31, w, d, i)
#define h_cmp_ri(e, w, d, i) h_alu_ri(e, 7, 0x39, w, d, i)

static inline void h_test_ri(Emitter *e, int w, int d, uint32_t imm)
{
    (void)w;
    x86_test_ri32(e, XR(d), imm);
}

#define h_neg(e, w, d) x86_neg_r(e, w, XR(d))
#define h_not(e, w, d) x86_not_r(e, w, XR(d))
#define h_shl_ri(e, w, d, n) x86_shl_ri(e, w, XR(d), n)
#define h_shr_ri(e, w, d, n) x86_shr_ri(e, w, XR(d), n)
#define h_sar_ri(e, w, d, n) x86_sar_ri(e, w, XR(d), n)
#define h_mul_rr(e, w, d, s) x86_imul_rr(e, w, XR(d), XR(s))

static inline void h_mul_rri(Emitter *e, int w, int d, int s, int32_t imm)
{
    emit_rex(e, w, XR(d), XR(s));
    emit_byte(e, 0x69);
    emit_modrm_rr(e, XR(d), XR(s));
    emit_u32(e, (uint32_t)imm);
}

#define h_add_r_mem32(e, d, b, o) x86_alu_rm32(e, 0x03, XR(d), XR(b), o)
#define h_sub_r_mem32(e, d, b, o) x86_alu_rm32(e, 0x2B, XR(d), XR(b), o)
#define h_and_r_mem32(e, d, b, o) x86_alu_rm32(e, 0x23, XR(d), XR(b), o)
#define h_or_r_mem32(e, d, b, o)  x86_alu_rm32(e, 0x0B, XR(d), XR(b), o)
#define h_xor_r_mem32(e, d, b, o) x86_alu_rm32(e, 0x33, XR(d), XR(b), o)
#define h_cmp_r_mem32(e, d, b, o) x86_alu_rm32(e, 0x3B, XR(d), XR(b), o)

#define h_add_mem32_imm(e, b, o, i) x86_add_mi32(e, XR(b), o, i)
#define h_sub_mem32_imm(e, b, o, i) x86_sub_mi32(e, XR(b), o, i)
#define h_or_mem32_imm(e, b, o, i)  x86_or_mi32(e, XR(b), o, i)
#define h_and_mem32_imm(e, b, o, i) x86_and_mi32(e, XR(b), o, i)
#define h_cmp_mem32_imm(e, b, o, i) x86_cmp_mi32(e, XR(b), o, i)
#define h_add_mem32_r(e, b, o, s)   x86_alu_mr32(e, 0x01, XR(b), o, XR(s))
#define h_cmp_mem16_imm(e, b, o, i) x86_cmp_mi16(e, XR(b), o, i)
#define h_cmp_mem8_imm(e, b, o, i)  x86_cmp_mi8(e, XR(b), o, i)
#define h_test_mem8_imm(e, b, o, i) x86_test_mi8(e, XR(b), o, i)

/* --- control --- */

static inline HLabel h_jcc(Emitter *e, int cc)
{
    return x86_jcc_rel32(e, XCC(cc));
}

static inline HLabel h_jmp(Emitter *e)
{
    return x86_jmp_rel32(e);
}

static inline void h_patch(Emitter *e, HLabel l, size_t target)
{
    x86_patch_rel32(e, l, target);
}

static inline void h_jcc_to(Emitter *e, int cc, size_t target)
{
    x86_jcc_to(e, XCC(cc), target);
}

static inline void h_jmp_to(Emitter *e, size_t target)
{
    x86_jmp_to(e, target);
}

static inline void h_jcc_abs(Emitter *e, int cc, const void *target)
{
    x86_jcc_abs(e, XCC(cc), target);
}

static inline void h_jmp_abs(Emitter *e, const void *target)
{
    x86_jmp_abs(e, target);
}

static inline void h_jmp_r(Emitter *e, int r)
{
    x86_jmp_r64(e, XR(r));
}

static inline void h_call(Emitter *e, const void *fn)
{
    x86_call_abs(e, fn);
}

/* a call from a stub: the values and addresses (caller-saved here) kept;
 * four pushes keep the stack aligned, and the callee gets its own shadow
 * space above them */
static inline void h_stub_call_begin(Emitter *e)
{
    x86_push_r64(e, R8);
    x86_push_r64(e, R9);
    x86_push_r64(e, R10);
    x86_push_r64(e, R11);
    x86_sub_rsp_imm8(e, 32);
}

static inline void h_stub_call_end(Emitter *e)
{
    x86_add_rsp_imm8(e, 32);
    x86_pop_r64(e, R11);
    x86_pop_r64(e, R10);
    x86_pop_r64(e, R9);
    x86_pop_r64(e, R8);
}

/* enter(core, fn): the callee-saved registers the code uses pushed (seven
 * of them: with the return address the stack is 16-byte aligned again),
 * the frame below, the state pointer in place, into the block */
static inline void h_enter(Emitter *e)
{
    x86_push_r64(e, RBX);
    x86_push_r64(e, RBP);
    x86_push_r64(e, RSI);
    x86_push_r64(e, RDI);
    x86_push_r64(e, R12);
    x86_push_r64(e, R13);
    x86_push_r64(e, R14);
    x86_push_r64(e, R15);
    x86_sub_rsp_imm8(e, X86_FRAME);
    x86_mov_r64_r64(e, RBX, ARG0);
    x86_jmp_r64(e, ARG1);
}

static inline void h_leave(Emitter *e)
{
    x86_add_rsp_imm8(e, X86_FRAME);
    x86_pop_r64(e, R15);
    x86_pop_r64(e, R14);
    x86_pop_r64(e, R13);
    x86_pop_r64(e, R12);
    x86_pop_r64(e, RDI);
    x86_pop_r64(e, RSI);
    x86_pop_r64(e, RBP);
    x86_pop_r64(e, RBX);
    x86_ret(e);
}

static inline void h_frame_st32(Emitter *e, int slot, int s)
{
    x86_mov_mem_r32(e, RSP, X86_SLOTS + slot * 8, XR(s));
}

static inline void h_frame_ld32(Emitter *e, int d, int slot)
{
    x86_mov_r32_mem(e, XR(d), RSP, X86_SLOTS + slot * 8);
}

#endif
