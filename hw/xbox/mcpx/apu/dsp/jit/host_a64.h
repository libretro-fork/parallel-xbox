/*
 * The DSP56300 JIT's host operations (host.h) on AArch64 (the AAPCS64
 * ABI, Linux and Windows alike for what the code uses).
 *
 * HReg to the host's registers: x19-x26 are callee-saved and kept by
 * the trampolines (the state, the accumulators, the values and
 * addresses, the REP count: a call from a stub clobbers none of them);
 * the temporaries are x0-x4, the arguments x0-x2 (the same registers:
 * set last before a call). x16 and x17 are the encoders' own scratch,
 * for an immediate an instruction cannot carry and an offset a load
 * cannot (x16), and the word of a read-modify-write (x17).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef DSP_JIT_HOST_A64_H
#define DSP_JIT_HOST_A64_H

static const uint8_t hreg_a64[H_NREGS] = {
    [H_CORE] = 19, [H_ACC_A] = 20, [H_ACC_B] = 21,
    [H_V1] = 22, [H_V2] = 23, [H_EA1] = 24, [H_EA2] = 25,
    [H_LC] = 26,
    [H_T0] = 0, [H_T1] = 1, [H_T2] = 2, [H_T3] = 3, [H_T4] = 4,
    [H_ARG0] = 0, [H_ARG1] = 1, [H_ARG2] = 2,
};
#define AR(h) (hreg_a64[h])
#define A64_IP0 16
#define A64_IP1 17
#define A64_SP 31
#define A64_ZR 31

/* the conditions: EQ NE CS CC MI PL VS VC HI LS GE LT GT LE */
static const uint8_t hcc_a64[] = {
    [HC_EQ] = 0, [HC_NE] = 1,
    [HC_B] = 3, [HC_AE] = 2, [HC_A] = 8, [HC_BE] = 9,
    [HC_L] = 11, [HC_GE] = 10, [HC_G] = 12, [HC_LE] = 13,
    [HC_S] = 4, [HC_NS] = 5,
    [HC_O] = 6,
    [HC_CARRY] = 2, [HC_BORROW] = 3,
};
#define ACC(c) (hcc_a64[c])

/* the frame the trampolines make: x29 and x30, x19-x26, a slot */
#define A64_FRAME 96
#define A64_SLOTS 80

static inline void a64(Emitter *e, uint32_t insn)
{
    emit_u32(e, insn);
}

/* --- immediates --- */

/* an arithmetic immediate: 12 bits, or 12 bits shifted by 12 */
static inline bool a64_arith_imm(uint64_t v, uint32_t *field)
{
    if (v < 0x1000) {
        *field = (uint32_t)v << 10;
        return true;
    }
    if ((v & 0xFFF) == 0 && (v >> 12) < 0x1000) {
        *field = (uint32_t)(v >> 12) << 10 | 1u << 22;
        return true;
    }
    return false;
}

/* a logical immediate: a run of ones, rotated, repeating over 2 to 64
 * bits; the N:immr:imms field */
static inline bool a64_logic_imm(uint64_t v, int w, uint32_t *field)
{
    int size = w ? 64 : 32;
    uint64_t mask = w ? ~(uint64_t)0 : 0xFFFFFFFFu;

    v &= mask;
    if (v == 0 || v == mask) {
        return false;
    }
    /* the smallest repeating element */
    int esize = size;
    while (esize > 2) {
        int half = esize / 2;
        uint64_t emask = half == 64 ? ~(uint64_t)0 : ((uint64_t)1 << half) - 1;
        if (((v >> half) & emask) != (v & emask)) {
            break;
        }
        esize = half;
    }
    uint64_t emask = esize == 64 ? ~(uint64_t)0 : ((uint64_t)1 << esize) - 1;
    uint64_t elem = v & emask;
    /* rotate the element so its ones are at the bottom: find the run */
    int ones = 0;
    for (int i = 0; i < esize; i++) {
        ones += (elem >> i) & 1;
    }
    /* rotate right until bit 0 is 1 and bit esize-1 is 0 */
    int rot = 0;
    uint64_t r = elem;
    for (rot = 0; rot < esize; rot++) {
        if ((r & 1) && !((r >> (esize - 1)) & 1)) {
            break;
        }
        r = ((r >> 1) | (r << (esize - 1))) & emask;
    }
    if (rot == esize) {
        return false;
    }
    /* r must now be a run of `ones` ones at the bottom */
    uint64_t run = ones == 64 ? ~(uint64_t)0 : ((uint64_t)1 << ones) - 1;
    if (r != run) {
        return false;
    }
    /* the element was r rotated left by rot: immr is the right rotation */
    int immr = (esize - rot) % esize;
    uint32_t imms, n;
    /* N:imms: 1:0xxxxxx 64 bits, 0:0xxxxx 32, 0:10xxxx 16, 0:110xxx 8,
     * 0:1110xx 4, 0:11110x 2; x: the ones less one */
    switch (esize) {
    case 64: n = 1; imms = ones - 1; break;
    case 32: n = 0; imms = ones - 1; break;
    case 16: n = 0; imms = 0x20 | (ones - 1); break;
    case 8:  n = 0; imms = 0x30 | (ones - 1); break;
    case 4:  n = 0; imms = 0x38 | (ones - 1); break;
    case 2:  n = 0; imms = 0x3C | (ones - 1); break;
    default: return false;
    }
    *field = n << 22 | (uint32_t)immr << 16 | imms << 10;
    return true;
}

/* mov to a host register (by number) by movz/movn and movk */
static inline void a64_mov_imm(Emitter *e, int rd, uint64_t imm)
{
    uint64_t inv = ~imm;
    int zeros = 0, ones = 0;

    for (int i = 0; i < 4; i++) {
        zeros += ((imm >> (16 * i)) & 0xFFFF) == 0;
        ones += ((inv >> (16 * i)) & 0xFFFF) == 0;
    }
    if (imm <= 0xFFFFFFFFu) {
        /* 32-bit: movz w, then movk w for the other half if needed */
        uint32_t lo = imm & 0xFFFF, hi = (imm >> 16) & 0xFFFF;
        if (hi == 0) {
            a64(e, 0x52800000 | lo << 5 | rd);
        } else if (lo == 0) {
            a64(e, 0x52800000 | 1u << 21 | hi << 5 | rd);
        } else {
            a64(e, 0x52800000 | lo << 5 | rd);
            a64(e, 0x72800000 | 1u << 21 | hi << 5 | rd);
        }
        return;
    }
    if (ones > zeros) {
        bool first = true;
        for (int i = 0; i < 4; i++) {
            uint32_t part = (imm >> (16 * i)) & 0xFFFF;
            if (part == 0xFFFF) {
                continue;
            }
            if (first) {
                a64(e, 0x92800000 | (uint32_t)i << 21 | (~part & 0xFFFF) << 5 | rd);
                first = false;
            } else {
                a64(e, 0xF2800000 | (uint32_t)i << 21 | part << 5 | rd);
            }
        }
        if (first) {
            a64(e, 0x92800000 | rd);
        }
    } else {
        bool first = true;
        for (int i = 0; i < 4; i++) {
            uint32_t part = (imm >> (16 * i)) & 0xFFFF;
            if (part == 0) {
                continue;
            }
            a64(e, (first ? 0xD2800000 : 0xF2800000) | (uint32_t)i << 21 | part << 5 | rd);
            first = false;
        }
        if (first) {
            a64(e, 0xD2800000 | rd);
        }
    }
}

/* --- moves --- */

static inline void h_mov_rr(Emitter *e, int w, int d, int s)
{
    /* orr d, zr, s */
    a64(e, (w ? 0xAA0003E0 : 0x2A0003E0) | (uint32_t)AR(s) << 16 | AR(d));
}

static inline void h_mov_ri(Emitter *e, int d, uint64_t imm)
{
    a64_mov_imm(e, AR(d), imm);
}

/* a load or store: the unsigned scaled offset when it fits, else the
 * offset in x16 as a register offset. size: 0 byte, 1 half, 2 word,
 * 3 double; op: the unsigned-offset opcode's top byte bits */
static inline void a64_ldst(Emitter *e, uint32_t op_imm, uint32_t op_reg, int size, int rt, int rn, int32_t off)
{
    if (off >= 0 && (off & ((1 << size) - 1)) == 0 && (off >> size) < 0x1000) {
        a64(e, op_imm | (uint32_t)(off >> size) << 10 | (uint32_t)rn << 5 | rt);
    } else {
        a64_mov_imm(e, A64_IP0, (uint64_t)(int64_t)off);
        a64(e, op_reg | (uint32_t)A64_IP0 << 16 | 3u << 13 | (uint32_t)rn << 5 | rt);
    }
}

static inline void h_ld32(Emitter *e, int d, int base, int32_t off)
{
    a64_ldst(e, 0xB9400000, 0xB8600800, 2, AR(d), AR(base), off);
}

static inline void h_ld64(Emitter *e, int d, int base, int32_t off)
{
    a64_ldst(e, 0xF9400000, 0xF8600800, 3, AR(d), AR(base), off);
}

static inline void h_ldz16(Emitter *e, int d, int base, int32_t off)
{
    a64_ldst(e, 0x79400000, 0x78600800, 1, AR(d), AR(base), off);
}

static inline void a64_ldz8(Emitter *e, int rd, int rn, int32_t off)
{
    a64_ldst(e, 0x39400000, 0x38600800, 0, rd, rn, off);
}

static inline void h_st32(Emitter *e, int base, int32_t off, int s)
{
    a64_ldst(e, 0xB9000000, 0xB8200800, 2, AR(s), AR(base), off);
}

static inline void h_st32_imm(Emitter *e, int base, int32_t off, uint32_t imm)
{
    if (imm == 0) {
        a64_ldst(e, 0xB9000000, 0xB8200800, 2, A64_ZR, AR(base), off);
        return;
    }
    a64_mov_imm(e, A64_IP1, imm);
    a64_ldst(e, 0xB9000000, 0xB8200800, 2, A64_IP1, AR(base), off);
}

static inline void h_st16_imm(Emitter *e, int base, int32_t off, uint16_t imm)
{
    a64_mov_imm(e, A64_IP1, imm);
    a64_ldst(e, 0x79000000, 0x78200800, 1, A64_IP1, AR(base), off);
}

/* [base + idx * 4 + off]: base + off into x16, then the index as a
 * zero-extended 32-bit register offset */
static inline void a64_base_off(Emitter *e, int base, int32_t off)
{
    a64_mov_imm(e, A64_IP0, (uint64_t)(int64_t)off);
    a64(e, 0x8B000000 | (uint32_t)AR(base) << 16 | (uint32_t)A64_IP0 << 5 | A64_IP0);
}

static inline void h_ld32_idx4(Emitter *e, int d, int base, int idx, int32_t off)
{
    a64_base_off(e, base, off);
    /* ldr wd, [x16, widx, uxtw #2] */
    a64(e, 0xB8605800 | (uint32_t)AR(idx) << 16 | (uint32_t)A64_IP0 << 5 | AR(d));
}

static inline void h_st32_idx4(Emitter *e, int base, int idx, int32_t off, int s)
{
    a64_base_off(e, base, off);
    a64(e, 0xB8205800 | (uint32_t)AR(idx) << 16 | (uint32_t)A64_IP0 << 5 | AR(s));
}

static inline void h_ld64_idx8(Emitter *e, int d, int base, int idx, int32_t off)
{
    a64_base_off(e, base, off);
    a64(e, 0xF8605800 | (uint32_t)AR(idx) << 16 | (uint32_t)A64_IP0 << 5 | AR(d));
}

/* add/sub immediate, with or without flags, any value */
static inline void a64_addsub_imm(Emitter *e, int w, bool flags, bool sub, int rd, int rn, int64_t imm)
{
    uint32_t field;
    uint32_t base;

    if (!w) {
        imm = (int32_t)(uint32_t)imm;
    }
    if (imm < 0 && imm != INT64_MIN) {
        sub = !sub;
        imm = -imm;
    }
    if (a64_arith_imm((uint64_t)imm, &field)) {
        base = sub ? (flags ? 0x71000000 : 0x51000000) : (flags ? 0x31000000 : 0x11000000);
        a64(e, base | (w ? 0x80000000 : 0) | field | (uint32_t)rn << 5 | rd);
    } else {
        a64_mov_imm(e, A64_IP0, (uint64_t)imm);
        base = sub ? (flags ? 0x6B000000 : 0x4B000000) : (flags ? 0x2B000000 : 0x0B000000);
        a64(e, base | (w ? 0x80000000 : 0) | (uint32_t)A64_IP0 << 16 | (uint32_t)rn << 5 | rd);
    }
}

static inline void h_lea(Emitter *e, int w, int d, int s, int32_t imm)
{
    a64_addsub_imm(e, w, false, false, AR(d), AR(s), imm);
}

static inline void h_setcc(Emitter *e, int cc, int d)
{
    /* cset wd, cond = csinc wd, wzr, wzr, !cond */
    a64(e, 0x1A9F07E0 | (uint32_t)(ACC(cc) ^ 1) << 12 | AR(d));
}

/* --- arithmetic and logic --- */

static inline void a64_rrr(Emitter *e, uint32_t op32, int w, int rd, int rn, int rm)
{
    a64(e, op32 | (w ? 0x80000000 : 0) | (uint32_t)rm << 16 | (uint32_t)rn << 5 | rd);
}

#define h_add_rr(e, w, d, s) a64_rrr(e, 0x2B000000, w, AR(d), AR(d), AR(s))  /* adds */
#define h_sub_rr(e, w, d, s) a64_rrr(e, 0x6B000000, w, AR(d), AR(d), AR(s))  /* subs */
#define h_and_rr(e, w, d, s) a64_rrr(e, 0x6A000000, w, AR(d), AR(d), AR(s))  /* ands */
#define h_or_rr(e, w, d, s)  a64_rrr(e, 0x2A000000, w, AR(d), AR(d), AR(s))
#define h_xor_rr(e, w, d, s) a64_rrr(e, 0x4A000000, w, AR(d), AR(d), AR(s))
#define h_cmp_rr(e, w, a, b) a64_rrr(e, 0x6B000000, w, A64_ZR, AR(a), AR(b))
#define h_test_rr(e, w, a, b) a64_rrr(e, 0x6A000000, w, A64_ZR, AR(a), AR(b))

static inline void h_add_ri(Emitter *e, int w, int d, int64_t imm)
{
    a64_addsub_imm(e, w, true, false, AR(d), AR(d), imm);
}

static inline void h_sub_ri(Emitter *e, int w, int d, int64_t imm)
{
    a64_addsub_imm(e, w, true, true, AR(d), AR(d), imm);
}

static inline void h_cmp_ri(Emitter *e, int w, int d, int64_t imm)
{
    a64_addsub_imm(e, w, true, true, A64_ZR, AR(d), imm);
}

/* a logical operation with an immediate: the bitmask form, else via x17 */
static inline void a64_logic_ri(Emitter *e, uint32_t op_imm, uint32_t op_reg, int w, int rd, int rn, uint64_t imm)
{
    uint32_t field;

    if (!w) {
        imm &= 0xFFFFFFFFu;
    }
    if (a64_logic_imm(imm, w, &field)) {
        a64(e, op_imm | (w ? 0x80000000 : 0) | field | (uint32_t)rn << 5 | rd);
    } else {
        a64_mov_imm(e, A64_IP0, imm);
        a64_rrr(e, op_reg, w, rd, rn, A64_IP0);
    }
}

#define h_and_ri(e, w, d, i) a64_logic_ri(e, 0x72000000, 0x6A000000, w, AR(d), AR(d), (uint64_t)(int64_t)(i))  /* ands */
#define h_or_ri(e, w, d, i)  a64_logic_ri(e, 0x32000000, 0x2A000000, w, AR(d), AR(d), (uint64_t)(int64_t)(i))
#define h_xor_ri(e, w, d, i) a64_logic_ri(e, 0x52000000, 0x4A000000, w, AR(d), AR(d), (uint64_t)(int64_t)(i))
#define h_test_ri(e, w, d, i) a64_logic_ri(e, 0x72000000, 0x6A000000, w, A64_ZR, AR(d), (uint64_t)(int64_t)(i))

static inline void h_neg(Emitter *e, int w, int d)
{
    /* negs d, d = subs d, zr, d */
    a64_rrr(e, 0x6B000000, w, AR(d), A64_ZR, AR(d));
}

static inline void h_not(Emitter *e, int w, int d)
{
    /* orn d, zr, d */
    a64(e, (w ? 0xAA2003E0 : 0x2A2003E0) | (uint32_t)AR(d) << 16 | AR(d));
}

static inline void h_shl_ri(Emitter *e, int w, int d, int n)
{
    /* lsl = ubfm d, d, #(-n mod bits), #(bits-1-n) */
    int bits = w ? 64 : 32;
    n &= bits - 1;
    uint32_t immr = (uint32_t)((bits - n) & (bits - 1)), imms = (uint32_t)(bits - 1 - n);
    a64(e, (w ? 0xD3400000 : 0x53000000) | immr << 16 | imms << 10 | (uint32_t)AR(d) << 5 | AR(d));
}

static inline void h_shr_ri(Emitter *e, int w, int d, int n)
{
    int bits = w ? 64 : 32;
    n &= bits - 1;
    a64(e, (w ? 0xD340FC00 : 0x53007C00) | (uint32_t)n << 16 | (uint32_t)AR(d) << 5 | AR(d));
}

static inline void h_sar_ri(Emitter *e, int w, int d, int n)
{
    int bits = w ? 64 : 32;
    n &= bits - 1;
    a64(e, (w ? 0x9340FC00 : 0x13007C00) | (uint32_t)n << 16 | (uint32_t)AR(d) << 5 | AR(d));
}

static inline void h_mul_rr(Emitter *e, int w, int d, int s)
{
    /* madd d, d, s, zr */
    a64(e, (w ? 0x9B007C00 : 0x1B007C00) | (uint32_t)AR(s) << 16 | (uint32_t)AR(d) << 5 | AR(d));
}

static inline void h_mul_rri(Emitter *e, int w, int d, int s, int32_t imm)
{
    a64_mov_imm(e, A64_IP1, (uint64_t)(int64_t)imm);
    a64(e, (w ? 0x9B007C00 : 0x1B007C00) | (uint32_t)A64_IP1 << 16 | (uint32_t)AR(s) << 5 | AR(d));
}

/* d op= *(uint32_t *)(base + off): the word into x16 first */
static inline void a64_op_r_mem32(Emitter *e, uint32_t op, int d, int base, int32_t off)
{
    a64_ldst(e, 0xB9400000, 0xB8600800, 2, A64_IP0, AR(base), off);
    a64_rrr(e, op, 0, AR(d), AR(d), A64_IP0);
}

#define h_add_r_mem32(e, d, b, o) a64_op_r_mem32(e, 0x2B000000, d, b, o)
#define h_sub_r_mem32(e, d, b, o) a64_op_r_mem32(e, 0x6B000000, d, b, o)
#define h_and_r_mem32(e, d, b, o) a64_op_r_mem32(e, 0x6A000000, d, b, o)
#define h_or_r_mem32(e, d, b, o)  a64_op_r_mem32(e, 0x2A000000, d, b, o)
#define h_xor_r_mem32(e, d, b, o) a64_op_r_mem32(e, 0x4A000000, d, b, o)

static inline void h_cmp_r_mem32(Emitter *e, int d, int base, int32_t off)
{
    a64_ldst(e, 0xB9400000, 0xB8600800, 2, A64_IP0, AR(base), off);
    a64_rrr(e, 0x6B000000, 0, A64_ZR, AR(d), A64_IP0);
}

/* *(uint32_t *)(base + off) op= imm, through x17 (x16 may hold the
 * offset); the flags are the operation's */
static inline void h_add_mem32_imm(Emitter *e, int base, int32_t off, int32_t imm)
{
    a64_ldst(e, 0xB9400000, 0xB8600800, 2, A64_IP1, AR(base), off);
    a64_addsub_imm(e, 0, true, false, A64_IP1, A64_IP1, imm);
    a64_ldst(e, 0xB9000000, 0xB8200800, 2, A64_IP1, AR(base), off);
}

static inline void h_sub_mem32_imm(Emitter *e, int base, int32_t off, int32_t imm)
{
    a64_ldst(e, 0xB9400000, 0xB8600800, 2, A64_IP1, AR(base), off);
    a64_addsub_imm(e, 0, true, true, A64_IP1, A64_IP1, imm);
    a64_ldst(e, 0xB9000000, 0xB8200800, 2, A64_IP1, AR(base), off);
}

static inline void h_or_mem32_imm(Emitter *e, int base, int32_t off, int32_t imm)
{
    a64_ldst(e, 0xB9400000, 0xB8600800, 2, A64_IP1, AR(base), off);
    a64_logic_ri(e, 0x32000000, 0x2A000000, 0, A64_IP1, A64_IP1, (uint32_t)imm);
    a64_ldst(e, 0xB9000000, 0xB8200800, 2, A64_IP1, AR(base), off);
}

static inline void h_and_mem32_imm(Emitter *e, int base, int32_t off, int32_t imm)
{
    a64_ldst(e, 0xB9400000, 0xB8600800, 2, A64_IP1, AR(base), off);
    a64_logic_ri(e, 0x72000000, 0x6A000000, 0, A64_IP1, A64_IP1, (uint32_t)imm);
    a64_ldst(e, 0xB9000000, 0xB8200800, 2, A64_IP1, AR(base), off);
}

static inline void h_add_mem32_r(Emitter *e, int base, int32_t off, int s)
{
    a64_ldst(e, 0xB9400000, 0xB8600800, 2, A64_IP1, AR(base), off);
    a64_rrr(e, 0x2B000000, 0, A64_IP1, A64_IP1, AR(s));
    a64_ldst(e, 0xB9000000, 0xB8200800, 2, A64_IP1, AR(base), off);
}

static inline void h_cmp_mem32_imm(Emitter *e, int base, int32_t off, int32_t imm)
{
    a64_ldst(e, 0xB9400000, 0xB8600800, 2, A64_IP1, AR(base), off);
    a64_addsub_imm(e, 0, true, true, A64_ZR, A64_IP1, imm);
}

static inline void h_cmp_mem16_imm(Emitter *e, int base, int32_t off, uint16_t imm)
{
    a64_ldst(e, 0x79400000, 0x78600800, 1, A64_IP1, AR(base), off);
    a64_addsub_imm(e, 0, true, true, A64_ZR, A64_IP1, imm);
}

static inline void h_cmp_mem8_imm(Emitter *e, int base, int32_t off, uint8_t imm)
{
    a64_ldz8(e, A64_IP1, AR(base), off);
    a64_addsub_imm(e, 0, true, true, A64_ZR, A64_IP1, imm);
}

static inline void h_test_mem8_imm(Emitter *e, int base, int32_t off, uint8_t imm)
{
    a64_ldz8(e, A64_IP1, AR(base), off);
    a64_logic_ri(e, 0x72000000, 0x6A000000, 0, A64_ZR, A64_IP1, imm);
}

/* --- control --- */

static inline HLabel h_jcc(Emitter *e, int cc)
{
    HLabel l = e->len;
    a64(e, 0x54000000 | ACC(cc));
    return l;
}

static inline HLabel h_jmp(Emitter *e)
{
    HLabel l = e->len;
    a64(e, 0x14000000);
    return l;
}

/* the branch at l to target: b.cond (19 bits of words) or b (26) */
static inline void h_patch(Emitter *e, HLabel l, size_t target)
{
    int64_t rel = ((int64_t)target - (int64_t)l) / 4;
    uint32_t insn;

    if (l + 4 > e->cap) {
        return;
    }
    memcpy(&insn, e->buf + l, 4);
    if ((insn & 0xFF000010) == 0x54000000) {
        if (rel < -(1 << 18) || rel >= (1 << 18)) {
            e->overflow = 1;    /* out of a block's reach: cannot happen */
            return;
        }
        insn = (insn & 0xFF00001F) | ((uint32_t)rel & 0x7FFFF) << 5;
    } else {
        insn = 0x14000000 | ((uint32_t)rel & 0x3FFFFFF);
    }
    memcpy(e->buf + l, &insn, 4);
}

static inline void h_jcc_to(Emitter *e, int cc, size_t target)
{
    h_patch(e, h_jcc(e, cc), target);
}

static inline void h_jmp_to(Emitter *e, size_t target)
{
    h_patch(e, h_jmp(e), target);
}

/* to an address anywhere in the cache: b reaches 128 MB, b.cond only
 * 1 MB, so a conditional one skips over an unconditional one */
static inline void h_jmp_abs(Emitter *e, const void *target)
{
    int64_t rel = ((int64_t)(uintptr_t)target - (int64_t)(uintptr_t)(e->buf + e->len)) / 4;
    a64(e, 0x14000000 | ((uint32_t)rel & 0x3FFFFFF));
}

static inline void h_jcc_abs(Emitter *e, int cc, const void *target)
{
    a64(e, 0x54000000 | 2u << 5 | (ACC(cc) ^ 1));
    h_jmp_abs(e, target);
}

static inline void h_jmp_r(Emitter *e, int r)
{
    a64(e, 0xD61F0000 | (uint32_t)AR(r) << 5);
}

static inline void h_call(Emitter *e, const void *fn)
{
    a64_mov_imm(e, A64_IP0, (uint64_t)(uintptr_t)fn);
    a64(e, 0xD63F0000 | (uint32_t)A64_IP0 << 5);
}

/* the values and addresses are callee-saved: nothing to keep */
static inline void h_stub_call_begin(Emitter *e)
{
}

static inline void h_stub_call_end(Emitter *e)
{
}

/* enter(core, fn): x29, x30 and x19-x26 kept in the frame, the state
 * pointer in place, into the block */
static inline void h_enter(Emitter *e)
{
    /* stp x29, x30, [sp, #-96]! */
    a64(e, 0xA9800000 | ((uint32_t)(-A64_FRAME / 8) & 0x7F) << 15 | 30u << 10 | 31u << 5 | 29);
    a64(e, 0xA9000000 | (16 / 8) << 15 | 20u << 10 | 31u << 5 | 19);   /* stp x19, x20, [sp, #16] */
    a64(e, 0xA9000000 | (32 / 8) << 15 | 22u << 10 | 31u << 5 | 21);
    a64(e, 0xA9000000 | (48 / 8) << 15 | 24u << 10 | 31u << 5 | 23);
    a64(e, 0xA9000000 | (64 / 8) << 15 | 26u << 10 | 31u << 5 | 25);
    a64(e, 0x910003FD);                                                 /* mov x29, sp */
    a64(e, 0xAA0003E0 | 0u << 16 | 19);                                 /* mov x19, x0 */
    a64(e, 0xD61F0000 | 1u << 5);                                       /* br x1 */
}

static inline void h_leave(Emitter *e)
{
    a64(e, 0xA9400000 | (16 / 8) << 15 | 20u << 10 | 31u << 5 | 19);   /* ldp x19, x20, [sp, #16] */
    a64(e, 0xA9400000 | (32 / 8) << 15 | 22u << 10 | 31u << 5 | 21);
    a64(e, 0xA9400000 | (48 / 8) << 15 | 24u << 10 | 31u << 5 | 23);
    a64(e, 0xA9400000 | (64 / 8) << 15 | 26u << 10 | 31u << 5 | 25);
    /* ldp x29, x30, [sp], #96 */
    a64(e, 0xA8C00000 | ((uint32_t)(A64_FRAME / 8) & 0x7F) << 15 | 30u << 10 | 31u << 5 | 29);
    a64(e, 0xD65F03C0);                                                 /* ret */
}

static inline void h_frame_st32(Emitter *e, int slot, int s)
{
    a64(e, 0xB9000000 | (uint32_t)((A64_SLOTS + slot * 8) / 4) << 10 | 31u << 5 | AR(s));
}

static inline void h_frame_ld32(Emitter *e, int d, int slot)
{
    a64(e, 0xB9400000 | (uint32_t)((A64_SLOTS + slot * 8) / 4) << 10 | 31u << 5 | AR(d));
}

#endif
