/*
 * A small x86-64 emitter for the DSP56300 JIT (dspjit.c): the instructions
 * the generated code uses, nothing more. SysV and Win64 both: the generated
 * code keeps its state in registers both ABIs preserve across calls (rbx,
 * r12-r15, which its prologue saves), scratches only rax, rcx, rdx and
 * r8-r11 (caller-saved on both), and takes its arguments where each ABI
 * puts them.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DSP_JIT_EMIT_X86_H
#define DSP_JIT_EMIT_X86_H

#include "host.h"

enum {
    RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15
};

#ifdef _WIN32
#define ARG0 RCX
#define ARG1 RDX
#define ARG2 R8
#define ARG3 R9
#else
#define ARG0 RDI
#define ARG1 RSI
#define ARG2 RDX
#define ARG3 RCX
#endif

enum { CC_O = 0, CC_NO, CC_B, CC_AE, CC_E, CC_NE, CC_BE, CC_A, CC_S, CC_NS, CC_P, CC_NP, CC_L, CC_GE, CC_LE, CC_G };

/* REX with W as asked, R for the ModRM reg field's register, X for an
 * index register, B for the rm/base register; only when needed */
static inline void emit_rex_x(Emitter *e, int w, int reg, int index, int rm)
{
    uint8_t rex = 0x40 | (w ? 8 : 0) | ((reg >> 3) & 1) << 2 |
                  ((index >> 3) & 1) << 1 | ((rm >> 3) & 1);
    if (rex != 0x40) {
        emit_byte(e, rex);
    }
}

static inline void emit_rex(Emitter *e, int w, int reg, int rm)
{
    emit_rex_x(e, w, reg, 0, rm);
}

/* ModRM for register-direct operands */
static inline void emit_modrm_rr(Emitter *e, int reg, int rm)
{
    emit_byte(e, 0xC0 | ((reg & 7) << 3) | (rm & 7));
}

/* ModRM: [base + disp32] (rsp/r12 as the base need a SIB) */
static inline void emit_modrm_disp32(Emitter *e, int reg, int base, int32_t disp)
{
    emit_byte(e, 0x80 | ((reg & 7) << 3) | (base & 7));
    if ((base & 7) == RSP) {
        emit_byte(e, 0x24);
    }
    emit_u32(e, (uint32_t)disp);
}

/* ModRM + SIB: [base + index*4 + disp32] */
static inline void emit_modrm_idx4(Emitter *e, int reg, int base, int index, int32_t disp)
{
    emit_byte(e, 0x80 | ((reg & 7) << 3) | 4);
    emit_byte(e, (2 << 6) | ((index & 7) << 3) | (base & 7));
    emit_u32(e, (uint32_t)disp);
}

/* --- moves --- */

/* mov dword [base + disp], imm32 */
static inline void x86_mov_mem32_imm(Emitter *e, int base, int32_t disp, uint32_t imm)
{
    emit_rex(e, 0, 0, base);
    emit_byte(e, 0xC7);
    emit_modrm_disp32(e, 0, base, disp);
    emit_u32(e, imm);
}

/* mov word [base + disp], imm16 */
static inline void x86_mov_mem16_imm(Emitter *e, int base, int32_t disp, uint16_t imm)
{
    emit_byte(e, 0x66);
    emit_rex(e, 0, 0, base);
    emit_byte(e, 0xC7);
    emit_modrm_disp32(e, 0, base, disp);
    emit_u16(e, imm);
}

/* mov r32, dword [base + disp] */
static inline void x86_mov_r32_mem(Emitter *e, int reg, int base, int32_t disp)
{
    emit_rex(e, 0, reg, base);
    emit_byte(e, 0x8B);
    emit_modrm_disp32(e, reg, base, disp);
}

/* mov dword [base + disp], r32 */
static inline void x86_mov_mem_r32(Emitter *e, int base, int32_t disp, int reg)
{
    emit_rex(e, 0, reg, base);
    emit_byte(e, 0x89);
    emit_modrm_disp32(e, reg, base, disp);
}

/* movzx r32, word [base + disp] */
static inline void x86_movzx_r32_mem16(Emitter *e, int reg, int base, int32_t disp)
{
    emit_rex(e, 0, reg, base);
    emit_byte(e, 0x0F);
    emit_byte(e, 0xB7);
    emit_modrm_disp32(e, reg, base, disp);
}

/* mov r32, dword [base + index*4 + disp32] */
static inline void x86_mov_r32_mem_idx4(Emitter *e, int reg, int base, int index, int32_t disp)
{
    emit_rex_x(e, 0, reg, index, base);
    emit_byte(e, 0x8B);
    emit_modrm_idx4(e, reg, base, index, disp);
}

/* mov dword [base + index*4 + disp32], r32 */
static inline void x86_mov_mem_idx4_r32(Emitter *e, int base, int index, int32_t disp, int reg)
{
    emit_rex_x(e, 0, reg, index, base);
    emit_byte(e, 0x89);
    emit_modrm_idx4(e, reg, base, index, disp);
}

/* mov r64, r64 / mov r32, r32 */
static inline void x86_mov_rr(Emitter *e, int w, int dst, int src)
{
    emit_rex(e, w, src, dst);
    emit_byte(e, 0x89);
    emit_modrm_rr(e, src, dst);
}
#define x86_mov_r64_r64(e, d, s) x86_mov_rr(e, 1, d, s)
#define x86_mov_r32_r32(e, d, s) x86_mov_rr(e, 0, d, s)

/* mov r32, imm32 (zero-extends) */
static inline void x86_mov_r32_imm(Emitter *e, int reg, uint32_t imm)
{
    emit_rex(e, 0, 0, reg);
    emit_byte(e, 0xB8 | (reg & 7));
    emit_u32(e, imm);
}

/* mov r64, imm64 */
static inline void x86_mov_r64_imm(Emitter *e, int reg, uint64_t imm)
{
    emit_rex(e, 1, 0, reg);
    emit_byte(e, 0xB8 | (reg & 7));
    emit_u64(e, imm);
}

/* movsxd r64, r32 */
static inline void x86_movsxd(Emitter *e, int dst, int src)
{
    emit_rex(e, 1, dst, src);
    emit_byte(e, 0x63);
    emit_modrm_rr(e, dst, src);
}

/* movzx r32, r8 (the low byte of a register) */
static inline void x86_movzx_r32_r8(Emitter *e, int dst, int src)
{
    if (dst >= 8 || src >= 4) {
        uint8_t rex = 0x40 | ((dst >> 3) & 1) << 2 | ((src >> 3) & 1);
        emit_byte(e, rex);
    }
    emit_byte(e, 0x0F);
    emit_byte(e, 0xB6);
    emit_modrm_rr(e, dst, src);
}

/* setcc r8 */
static inline void x86_setcc_r8(Emitter *e, int cc, int dst)
{
    if (dst >= 4) {
        emit_byte(e, 0x40 | ((dst >> 3) & 1));
    }
    emit_byte(e, 0x0F);
    emit_byte(e, 0x90 | cc);
    emit_modrm_rr(e, 0, dst);
}

/* cmovcc r64/r32, r64/r32 */
static inline void x86_cmov_rr(Emitter *e, int cc, int w, int dst, int src)
{
    emit_rex(e, w, dst, src);
    emit_byte(e, 0x0F);
    emit_byte(e, 0x40 | cc);
    emit_modrm_rr(e, dst, src);
}

/* lea r32, [base + disp32] */
static inline void x86_lea_r32_mem(Emitter *e, int reg, int base, int32_t disp)
{
    emit_rex(e, 0, reg, base);
    emit_byte(e, 0x8D);
    emit_modrm_disp32(e, reg, base, disp);
}

/* --- arithmetic --- */

/* op r, r for the "/r" opcodes: add 01, or 09, and 21, sub 29, xor 31,
 * cmp 39, test 85 (reg is the source) */
static inline void x86_alu_rr(Emitter *e, uint8_t op, int w, int dst, int src)
{
    emit_rex(e, w, src, dst);
    emit_byte(e, op);
    emit_modrm_rr(e, src, dst);
}
#define x86_add_rr(e, w, d, s)  x86_alu_rr(e, 0x01, w, d, s)
#define x86_or_rr(e, w, d, s)   x86_alu_rr(e, 0x09, w, d, s)
#define x86_and_rr(e, w, d, s)  x86_alu_rr(e, 0x21, w, d, s)
#define x86_sub_rr(e, w, d, s)  x86_alu_rr(e, 0x29, w, d, s)
#define x86_xor_rr(e, w, d, s)  x86_alu_rr(e, 0x31, w, d, s)
#define x86_cmp_rr(e, w, d, s)  x86_alu_rr(e, 0x39, w, d, s)
#define x86_test_rr(e, w, d, s) x86_alu_rr(e, 0x85, w, d, s)

/* op r, imm32 (sign-extended): 81 /ext or 83 /ext ib */
static inline void x86_alu_ri(Emitter *e, int ext, int w, int dst, int32_t imm)
{
    emit_rex(e, w, 0, dst);
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_modrm_rr(e, ext, dst);
        emit_byte(e, (uint8_t)imm);
    } else {
        emit_byte(e, 0x81);
        emit_modrm_rr(e, ext, dst);
        emit_u32(e, (uint32_t)imm);
    }
}
#define x86_add_ri(e, w, d, i) x86_alu_ri(e, 0, w, d, i)
#define x86_or_ri(e, w, d, i)  x86_alu_ri(e, 1, w, d, i)
#define x86_and_ri(e, w, d, i) x86_alu_ri(e, 4, w, d, i)
#define x86_sub_ri(e, w, d, i) x86_alu_ri(e, 5, w, d, i)
#define x86_xor_ri(e, w, d, i) x86_alu_ri(e, 6, w, d, i)
#define x86_cmp_ri(e, w, d, i) x86_alu_ri(e, 7, w, d, i)

/* op dword [base + disp], imm32 */
static inline void x86_alu_mi32(Emitter *e, int ext, int base, int32_t disp, int32_t imm)
{
    emit_rex(e, 0, 0, base);
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_modrm_disp32(e, ext, base, disp);
        emit_byte(e, (uint8_t)imm);
    } else {
        emit_byte(e, 0x81);
        emit_modrm_disp32(e, ext, base, disp);
        emit_u32(e, (uint32_t)imm);
    }
}
#define x86_add_mi32(e, b, d, i) x86_alu_mi32(e, 0, b, d, i)
#define x86_or_mi32(e, b, d, i)  x86_alu_mi32(e, 1, b, d, i)
#define x86_and_mi32(e, b, d, i) x86_alu_mi32(e, 4, b, d, i)
#define x86_sub_mi32(e, b, d, i) x86_alu_mi32(e, 5, b, d, i)
#define x86_cmp_mi32(e, b, d, i) x86_alu_mi32(e, 7, b, d, i)

/* cmp word [base + disp], imm16 */
static inline void x86_cmp_mi16(Emitter *e, int base, int32_t disp, uint16_t imm)
{
    emit_byte(e, 0x66);
    emit_rex(e, 0, 0, base);
    emit_byte(e, 0x81);
    emit_modrm_disp32(e, 7, base, disp);
    emit_u16(e, imm);
}

/* cmp byte [base + disp], imm8 */
static inline void x86_cmp_mi8(Emitter *e, int base, int32_t disp, uint8_t imm)
{
    emit_rex(e, 0, 0, base);
    emit_byte(e, 0x80);
    emit_modrm_disp32(e, 7, base, disp);
    emit_byte(e, imm);
}

/* test byte [base + disp], imm8 */
static inline void x86_test_mi8(Emitter *e, int base, int32_t disp, uint8_t imm)
{
    emit_rex(e, 0, 0, base);
    emit_byte(e, 0xF6);
    emit_modrm_disp32(e, 0, base, disp);
    emit_byte(e, imm);
}

/* test r32, imm32 */
static inline void x86_test_ri32(Emitter *e, int reg, uint32_t imm)
{
    emit_rex(e, 0, 0, reg);
    emit_byte(e, 0xF7);
    emit_modrm_rr(e, 0, reg);
    emit_u32(e, imm);
}

/* op dword [base + disp], r32: add 01, or 09, and 21, sub 29, cmp 39 */
static inline void x86_alu_mr32(Emitter *e, uint8_t op, int base, int32_t disp, int reg)
{
    emit_rex(e, 0, reg, base);
    emit_byte(e, op);
    emit_modrm_disp32(e, reg, base, disp);
}

/* op r32, dword [base + disp]: add 03, or 0B, and 23, sub 2B, cmp 3B */
static inline void x86_alu_rm32(Emitter *e, uint8_t op, int reg, int base, int32_t disp)
{
    emit_rex(e, 0, reg, base);
    emit_byte(e, op);
    emit_modrm_disp32(e, reg, base, disp);
}

/* shifts by imm8: shl 4, shr 5, sar 7 (C1 /ext) */
static inline void x86_shift_ri(Emitter *e, int ext, int w, int dst, uint8_t imm)
{
    emit_rex(e, w, 0, dst);
    emit_byte(e, 0xC1);
    emit_modrm_rr(e, ext, dst);
    emit_byte(e, imm);
}
#define x86_shl_ri(e, w, d, i) x86_shift_ri(e, 4, w, d, i)
#define x86_shr_ri(e, w, d, i) x86_shift_ri(e, 5, w, d, i)
#define x86_sar_ri(e, w, d, i) x86_shift_ri(e, 7, w, d, i)

/* imul r, r */
static inline void x86_imul_rr(Emitter *e, int w, int dst, int src)
{
    emit_rex(e, w, dst, src);
    emit_byte(e, 0x0F);
    emit_byte(e, 0xAF);
    emit_modrm_rr(e, dst, src);
}

/* neg r (F7 /3), not r (F7 /2) */
static inline void x86_neg_r(Emitter *e, int w, int dst)
{
    emit_rex(e, w, 0, dst);
    emit_byte(e, 0xF7);
    emit_modrm_rr(e, 3, dst);
}
static inline void x86_not_r(Emitter *e, int w, int dst)
{
    emit_rex(e, w, 0, dst);
    emit_byte(e, 0xF7);
    emit_modrm_rr(e, 2, dst);
}

/* --- control --- */

/* call r64 */
static inline void x86_call_r64(Emitter *e, int reg)
{
    emit_rex(e, 0, 0, reg);
    emit_byte(e, 0xFF);
    emit_modrm_rr(e, 2, reg);
}

/* call to an absolute address, through rax */
static inline void x86_call_abs(Emitter *e, const void *fn)
{
    x86_mov_r64_imm(e, RAX, (uint64_t)(uintptr_t)fn);
    x86_call_r64(e, RAX);
}

/* jcc rel32 with the target not yet known: the offset of the rel32 field,
 * for x86_patch_rel32 */
static inline size_t x86_jcc_rel32(Emitter *e, int cc)
{
    emit_byte(e, 0x0F);
    emit_byte(e, 0x80 | cc);
    size_t at = e->len;
    emit_u32(e, 0);
    return at;
}

static inline size_t x86_jmp_rel32(Emitter *e)
{
    emit_byte(e, 0xE9);
    size_t at = e->len;
    emit_u32(e, 0);
    return at;
}

static inline void x86_patch_rel32(Emitter *e, size_t at, size_t target)
{
    int32_t rel = (int32_t)((int64_t)target - (int64_t)(at + 4));
    if (at + 4 <= e->cap) {
        memcpy(e->buf + at, &rel, 4);
    }
}

/* jcc/jmp to a target already emitted */
static inline void x86_jcc_to(Emitter *e, int cc, size_t target)
{
    x86_patch_rel32(e, x86_jcc_rel32(e, cc), target);
}
static inline void x86_jmp_to(Emitter *e, size_t target)
{
    x86_patch_rel32(e, x86_jmp_rel32(e), target);
}

/* jmp r64 */
static inline void x86_jmp_r64(Emitter *e, int reg)
{
    emit_rex(e, 0, 0, reg);
    emit_byte(e, 0xFF);
    emit_modrm_rr(e, 4, reg);
}

static inline void x86_push_r64(Emitter *e, int reg)
{
    emit_rex(e, 0, 0, reg);
    emit_byte(e, 0x50 | (reg & 7));
}

static inline void x86_pop_r64(Emitter *e, int reg)
{
    emit_rex(e, 0, 0, reg);
    emit_byte(e, 0x58 | (reg & 7));
}

/* sub/add rsp, imm8 */
static inline void x86_sub_rsp_imm8(Emitter *e, int8_t imm)
{
    emit_byte(e, 0x48); emit_byte(e, 0x83); emit_byte(e, 0xEC); emit_byte(e, (uint8_t)imm);
}

static inline void x86_add_rsp_imm8(Emitter *e, int8_t imm)
{
    emit_byte(e, 0x48); emit_byte(e, 0x83); emit_byte(e, 0xC4); emit_byte(e, (uint8_t)imm);
}

static inline void x86_ret(Emitter *e)
{
    emit_byte(e, 0xC3);
}

/* mov r64, qword [base + disp32] */
static inline void x86_mov_r64_mem(Emitter *e, int reg, int base, int32_t disp)
{
    emit_rex(e, 1, reg, base);
    emit_byte(e, 0x8B);
    emit_modrm_disp32(e, reg, base, disp);
}

/* mov r64, qword [base + index*8 + disp32] */
static inline void x86_mov_r64_mem_idx8(Emitter *e, int reg, int base, int index, int32_t disp)
{
    emit_rex_x(e, 1, reg, index, base);
    emit_byte(e, 0x8B);
    emit_byte(e, 0x80 | ((reg & 7) << 3) | 4);
    emit_byte(e, (3 << 6) | ((index & 7) << 3) | (base & 7));
    emit_u32(e, (uint32_t)disp);
}

/* jmp rel32 to an absolute address (within 2 GB of the code) */
static inline void x86_jmp_abs(Emitter *e, const void *target)
{
    size_t at = x86_jmp_rel32(e);
    int64_t rel = (int64_t)(uintptr_t)target - (int64_t)(uintptr_t)(e->buf + at + 4);
    int32_t rel32 = (int32_t)rel;
    if (at + 4 <= e->cap) {
        memcpy(e->buf + at, &rel32, 4);
    }
}

/* jcc rel32 to an absolute address */
static inline void x86_jcc_abs(Emitter *e, int cc, const void *target)
{
    size_t at = x86_jcc_rel32(e, cc);
    int64_t rel = (int64_t)(uintptr_t)target - (int64_t)(uintptr_t)(e->buf + at + 4);
    int32_t rel32 = (int32_t)rel;
    if (at + 4 <= e->cap) {
        memcpy(e->buf + at, &rel32, 4);
    }
}

/* imul r32, r32, imm32 (69 /r id) */
static inline void x86_imul_rri(Emitter *e, int dst, int src, int32_t imm)
{
    emit_rex(e, 0, dst, src);
    emit_byte(e, 0x69);
    emit_modrm_rr(e, dst, src);
    emit_u32(e, (uint32_t)imm);
}

#endif
