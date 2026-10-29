/*
 * DSP56300 JIT: the DSP's program, translated to host code a block at a
 * time, running on the interpreter's own state (dsp_core_t) with the
 * interpreter's own semantics (interp/dsp_cpu.c), instruction for
 * instruction and flag for flag. The translation is in the host
 * operations of host.h, which host_x86.h (x86-64) and host_a64.h
 * (AArch64) encode.
 *
 * A block is the straight-line code from a P address: the instructions
 * the translator knows (the parallel-move instructions with the data ALU
 * operations the DSP programs are made of, the register moves, the
 * address register updates) are translated to native code; the first
 * instruction it does not know, or one that may move the PC (dsp56k_decode's
 * control flag), ends the block and runs as the interpreter's own function
 * for it followed by dsp56k_post(), which moves the PC the interpreter's
 * way (loops, repeats, interrupts) and spends its cycles.
 *
 * The native code keeps the DSP's state pointer in rbx and the
 * accumulators A and B in r12 and r13, as 56-bit values sign-extended to
 * 64, loaded when a block first uses them and stored back before anything
 * else may look at them (an interpreter function, the end of the block).
 * The rest of the DSP's registers are read and written in place.
 * Condition codes are computed as the interpreter computes them; they
 * depend on the scaling mode (SR's S1 S0), so there is a block per mode
 * at an address. A REP repeats its instruction in a loop of its own
 * (rep_blocks); the interpreter steps the rest (an interrupt in its
 * pipeline, a REP's own instruction). A block charges its cycles when it
 * ends, so a run may go a block past its budget; the budget carries over,
 * as the interpreter's does.
 *
 * Every write to P drops the blocks made from the word (dspjit_invalidate):
 * a program that changes itself runs its new code from the next block on
 * (the instruction that writes P ends its block). The code cache is one
 * fixed arena made with the context: no allocation while the DSP runs;
 * when it is full, it is emptied and filled again.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "../interp/dsp_cpu.h"
#include "dspjit.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#if DSPJIT_HOST
#include "host.h"
#endif

#define DSPJIT_CACHE_BYTES (4u << 20)
#define DSPJIT_BLOCK_MAX_INSNS 64

typedef void (*dspjit_block_fn)(dsp_core_t *core);
typedef struct Tr Tr;

/* the code's entry: the host registers saved, the state pointer in
 * place, into the block */
typedef void (*dspjit_enter_fn)(dsp_core_t *core, dspjit_block_fn fn);

struct DspJit {
    dsp_core_t *core;
    uint8_t *cache;
    size_t cache_len;
    size_t cache_used;
    size_t cache_base;      /* after the trampolines */
    dspjit_enter_fn enter;
    const void *leave;      /* the blocks' way out: the registers restored */
    /* the blocks by scaling mode (SR's S1 S0) and P address, and the
     * P address each ends before (their instruction words, for a write
     * to P); the rep_blocks are the instruction at the address repeated */
    dspjit_block_fn blocks[4][DSP_PRAM_SIZE];
    uint16_t block_end[4][DSP_PRAM_SIZE];
    dspjit_block_fn rep_blocks[4][DSP_PRAM_SIZE];
    struct Tr *tr;      /* the translator's state, made once */
};

/* a block's longest span in P words: its instructions, two words each */
#define DSPJIT_BLOCK_MAX_WORDS (DSPJIT_BLOCK_MAX_INSNS * 2)

static void *dspjit_map_exec(size_t len)
{
#ifdef _WIN32
    return VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
#endif
}

static void dspjit_unmap_exec(void *p, size_t len)
{
#ifdef _WIN32
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, len);
#endif
}

#if DSPJIT_HOST

/* code written: the instruction cache told, where it is not coherent
 * with the data cache (AArch64) */
static void dspjit_code_written(void *buf, size_t len)
{
#if !defined(__x86_64__) && !defined(_M_X64)
#ifdef _WIN32
    FlushInstructionCache(GetCurrentProcess(), buf, len);
#else
    __builtin___clear_cache((char *)buf, (char *)buf + len);
#endif
#endif
}

/**********************************
 *  The translator
 **********************************/

#define OFF(f) ((int32_t)offsetof(dsp_core_t, f))
#define REGOFF(i) (OFF(registers) + 4 * (i))

/* the registers the generated code works in (host.h) */
#define REG_CORE H_CORE
#define REG_ACC_A H_ACC_A
#define REG_ACC_B H_ACC_B
#define V1 H_V1         /* a parallel move's first value */
#define V2 H_V2         /* its second */
#define EA1 H_EA1       /* the first move's address */
#define EA2 H_EA2       /* the second's */
#define REG_LC H_LC     /* a REP's count, while its loop runs */
/* T0-T4: scratch within an instruction */
#define T0 H_T0
#define T1 H_T1
#define T2 H_T2
#define T3 H_T3
#define T4 H_T4

/* the frame slot for a REP's count when its loop started */
#define SLOT_REP_START 0

#define SR_C (1 << DSP_SR_C)
#define SR_V (1 << DSP_SR_V)
#define SR_Z (1 << DSP_SR_Z)
#define SR_N (1 << DSP_SR_N)
#define SR_U (1 << DSP_SR_U)
#define SR_E (1 << DSP_SR_E)
#define SR_L (1 << DSP_SR_L)
#define SR_EUNZ (SR_E | SR_U | SR_N | SR_Z)

typedef struct Insn {
    uint32_t at;        /* its address */
    uint32_t inst;      /* its word */
    uint32_t ext;       /* its second word, if any */
    uint32_t len;
    uint32_t next;
    DspDecoded d;
} Insn;

static void emit_rep_charge(Emitter *e, uint32_t cycles, int less);

/* out-of-line pieces of a block, emitted after its straight-line code */
enum {
    STUB_LOOPEND,       /* the PC reached LA+1: the DO loop's end, if in one */
    STUB_READ_X,        /* an X read outside the fast range */
    STUB_WRITE_X,
    STUB_READ_Y,
    STUB_WRITE_Y,
    STUB_RN_UPDATE,     /* an Rn update with Mn not linear */
    STUB_EA_RN_PLUS_N,  /* an (Rn+Nn) address with Mn not linear */
    STUB_LIMIT,         /* an accumulator read that limits */
    STUB_HALT,          /* the DSP halted (a peripheral write) */
    STUB_REP_POST,      /* a REP's repeat ended through dsp56k_post */
    STUB_FALLBACK,      /* the instruction through the interpreter's function */
    STUB_CTRL_POST,     /* the instruction's work done, the PC to be imm
                           (0xFFFFFFFF: as set): dsp56k_post for the rest */
};

enum { MOD_IMM, MOD_PLUS_N, MOD_MINUS_N };

typedef struct Stub {
    int kind;
    size_t jump;        /* the rel32 of the jump into the stub */
    size_t back;        /* where the stub jumps back to */
    int reg;            /* the address, or the register to update */
    int reg2;           /* the value read or written */
    int n;              /* an address register's number */
    int modkind;
    int32_t imm;
    /* the instruction */
    uint32_t at, next, len, cycles, cyc_before;
    bool loaded[2], dirty[2];
    int lazy_acc;
    Insn in;
} Stub;

#define MAX_STUBS (DSPJIT_BLOCK_MAX_INSNS * 12)

struct Tr {
    Emitter e;
    dsp_core_t *core;
    bool loaded[2];     /* A/B are in their host registers */
    bool dirty[2];      /* ... and newer than the state's */
    uint32_t cyc;       /* the cycles of the instructions translated so far */
    bool wrote_x;       /* the instruction being translated writes X */
    int mode;           /* the scaling mode the block is for */
    int lazy_acc;       /* SR's E, U, N, Z are those of this accumulator's
                           value, not computed yet (-1: none) */
    uint32_t end;       /* the P address the block ends before */
    Stub stubs[MAX_STUBS];
    int nstubs;
    const void *leave;  /* the way out of the code (DspJit's) */
    dspjit_block_fn *table; /* the blocks for the block's scaling mode */
};

static Stub *stub_new(Tr *t, int kind, size_t jump)
{
    Stub *s;

    if (t->nstubs >= MAX_STUBS) {
        /* cannot happen with the per-instruction bound; make the block
         * fail to fit so it is retried after a flush, never overrun */
        t->e.overflow = 1;
        t->nstubs = MAX_STUBS - 1;
    }
    s = &t->stubs[t->nstubs++];
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    s->jump = jump;
    return s;
}

/* --- the accumulators --- */

static int acc_reg(int n)
{
    return n ? REG_ACC_B : REG_ACC_A;
}

static void acc_load(Tr *t, int n)
{
    Emitter *e = &t->e;
    int r = acc_reg(n);

    if (t->loaded[n]) {
        return;
    }
    h_ld32(e, r, REG_CORE, REGOFF(DSP_REG_A2 + n));
    h_shl_ri(e, 1, r, 48);
    h_ld32(e, T0, REG_CORE, REGOFF(DSP_REG_A1 + n));
    h_shl_ri(e, 1, T0, 24);
    h_or_rr(e, 1, r, T0);
    h_ld32(e, T0, REG_CORE, REGOFF(DSP_REG_A0 + n));
    h_or_rr(e, 1, r, T0);
    h_shl_ri(e, 1, r, 8);
    h_sar_ri(e, 1, r, 8);
    t->loaded[n] = true;
    t->dirty[n] = false;
}

/* the state's A/B from the host registers (uses rax) */
static void emit_acc_store(Emitter *e, int n)
{
    int r = acc_reg(n);

    h_mov_rr(e, 1, T0, r);
    h_and_ri(e, 0, T0, 0xFFFFFF);
    h_st32(e, REG_CORE, REGOFF(DSP_REG_A0 + n), T0);
    h_mov_rr(e, 1, T0, r);
    h_shr_ri(e, 1, T0, 24);
    h_and_ri(e, 0, T0, 0xFFFFFF);
    h_st32(e, REG_CORE, REGOFF(DSP_REG_A1 + n), T0);
    h_mov_rr(e, 1, T0, r);
    h_shr_ri(e, 1, T0, 48);
    h_and_ri(e, 0, T0, 0xFF);
    h_st32(e, REG_CORE, REGOFF(DSP_REG_A2 + n), T0);
}

/* the state up to date; keep: the host registers stay valid */
static void emit_flags_flush(Tr *t);
static void acc_spill_all(Tr *t, bool keep)
{
    emit_flags_flush(t);
    for (int n = 0; n < 2; n++) {
        if (t->dirty[n]) {
            emit_acc_store(&t->e, n);
            t->dirty[n] = false;
        }
        if (!keep) {
            t->loaded[n] = false;
        }
    }
}

static void acc_written(Tr *t, int n)
{
    t->loaded[n] = true;
    t->dirty[n] = true;
}

/* --- cycles and exits --- */

static void emit_charge(Emitter *e, uint32_t cyc)
{
    if (cyc == 0) {
        return;
    }
    h_sub_mem32_imm(e, REG_CORE, OFF(jit_cycles), cyc);
    h_add_mem32_imm(e, REG_CORE, OFF(cycle_count), cyc);
    h_add_mem32_imm(e, REG_CORE, OFF(num_inst), cyc);
}

static void emit_jump_to_epilogue(Tr *t)
{
    h_jmp_abs(&t->e, t->leave);
}

/* the cycles charged, then on to the next block while the budget lasts:
 * the block at the PC as stored (its code, if made; else out to the
 * dispatcher, which makes it). The accumulators must be stored. */
static void emit_chain(Tr *t, uint32_t cyc)
{
    Emitter *e = &t->e;

    if (cyc) {
        h_add_mem32_imm(e, REG_CORE, OFF(cycle_count), cyc);
        h_add_mem32_imm(e, REG_CORE, OFF(num_inst), cyc);
        h_sub_mem32_imm(e, REG_CORE, OFF(jit_cycles), cyc);
    } else {
        h_cmp_mem32_imm(e, REG_CORE, OFF(jit_cycles), 0);
    }
    h_jcc_abs(e, HC_LE, t->leave);
    h_ld32(e, T0, REG_CORE, OFF(pc));
    h_and_ri(e, 0, T0, DSP_PRAM_SIZE - 1);
    h_mov_ri(e, T1, (uint64_t)(uintptr_t)t->table);
    h_ld64_idx8(e, T0, T1, T0, 0);
    h_test_rr(e, 1, T0, T0);
    h_jcc_abs(e, HC_EQ, t->leave);
    h_jmp_r(e, T0);
}

/* as emit_chain, to the block at a PC known now */
static void emit_chain_to(Tr *t, uint32_t target, uint32_t cyc)
{
    Emitter *e = &t->e;

    h_st32_imm(e, REG_CORE, OFF(pc), target);
    if (cyc) {
        h_add_mem32_imm(e, REG_CORE, OFF(cycle_count), cyc);
        h_add_mem32_imm(e, REG_CORE, OFF(num_inst), cyc);
        h_sub_mem32_imm(e, REG_CORE, OFF(jit_cycles), cyc);
    } else {
        h_cmp_mem32_imm(e, REG_CORE, OFF(jit_cycles), 0);
    }
    h_jcc_abs(e, HC_LE, t->leave);
    h_mov_ri(e, T0, (uint64_t)(uintptr_t)&t->table[target & (DSP_PRAM_SIZE - 1)]);
    h_ld64(e, T0, T0, 0);
    h_test_rr(e, 1, T0, T0);
    h_jcc_abs(e, HC_EQ, t->leave);
    h_jmp_r(e, T0);
}

/* --- helpers called from the generated code --- */

static uint32_t jit_read_x(dsp_core_t *core, uint32_t addr)
{
    return dsp56k_read_memory(core, DSP_SPACE_X, addr);
}

static void jit_write_x(dsp_core_t *core, uint32_t addr, uint32_t value)
{
    dsp56k_write_memory(core, DSP_SPACE_X, addr, value);
}

static uint32_t jit_read_y(dsp_core_t *core, uint32_t addr)
{
    return dsp56k_read_memory(core, DSP_SPACE_Y, addr);
}

static void jit_write_y(dsp_core_t *core, uint32_t addr, uint32_t value)
{
    dsp56k_write_memory(core, DSP_SPACE_Y, addr, value);
}

/* a call from a stub: the values the block keeps in caller-saved
 * registers survive it */
static void emit_stub_call_begin(Emitter *e)
{
    h_stub_call_begin(e);
    h_mov_rr(e, 1, H_ARG0, REG_CORE);
}

static void emit_stub_call_end(Emitter *e)
{
    h_stub_call_end(e);
}

/* --- memory --- */

/* X addresses below this are the X RAM; from it, the mixbuffer's alias
 * (dsp56k_read_memory) */
#define DSP_XRAM_FAST 0xC00

/* X memory read: the address in reg (any 32-bit value), the value to out.
 * In line for the X RAM, in a stub for the mixbuffer, out of line through
 * the interpreter for the rest (the peripherals, the mixbuffer's alias,
 * out of bounds). */
static void emit_read_x(Tr *t, int addr, int out)
{
    Emitter *e = &t->e;
    size_t j;

    h_cmp_ri(e, 0, addr, DSP_XRAM_FAST);
    j = h_jcc(e, HC_AE);
    h_ld32_idx4(e, out, REG_CORE, addr, OFF(xram));
    Stub *s = stub_new(t, STUB_READ_X, j);
    s->reg = addr;
    s->reg2 = out;
    s->back = e->len;
}

static void emit_write_x(Tr *t, int addr, int val)
{
    Emitter *e = &t->e;
    size_t j;

    h_cmp_ri(e, 0, addr, DSP_XRAM_FAST);
    j = h_jcc(e, HC_AE);
    h_st32_idx4(e, REG_CORE, addr, OFF(xram), val);
    Stub *s = stub_new(t, STUB_WRITE_X, j);
    s->reg = addr;
    s->reg2 = val;
    s->back = e->len;
    t->wrote_x = true;
}

static void emit_read_y(Tr *t, int addr, int out)
{
    Emitter *e = &t->e;
    size_t j;

    h_cmp_ri(e, 0, addr, DSP_YRAM_SIZE);
    j = h_jcc(e, HC_AE);
    h_ld32_idx4(e, out, REG_CORE, addr, OFF(yram));
    Stub *s = stub_new(t, STUB_READ_Y, j);
    s->reg = addr;
    s->reg2 = out;
    s->back = e->len;
}

static void emit_write_y(Tr *t, int addr, int val)
{
    Emitter *e = &t->e;
    size_t j;

    h_cmp_ri(e, 0, addr, DSP_YRAM_SIZE);
    j = h_jcc(e, HC_AE);
    h_st32_idx4(e, REG_CORE, addr, OFF(yram), val);
    Stub *s = stub_new(t, STUB_WRITE_Y, j);
    s->reg = addr;
    s->reg2 = val;
    s->back = e->len;
}

static void emit_read_mem(Tr *t, int space, int addr, int out)
{
    if (space == DSP_SPACE_X) {
        emit_read_x(t, addr, out);
    } else {
        emit_read_y(t, addr, out);
    }
}

static void emit_write_mem(Tr *t, int space, int addr, int val)
{
    if (space == DSP_SPACE_X) {
        emit_write_x(t, addr, val);
    } else {
        emit_write_y(t, addr, val);
    }
}

/* --- address registers --- */

/* Rn += modifier the interpreter's way: in line when Mn is linear */
static void emit_rn_update(Tr *t, int n, int modkind, int32_t imm)
{
    Emitter *e = &t->e;
    size_t j;

    h_cmp_mem16_imm(e, REG_CORE, REGOFF(DSP_REG_M0 + n), 0xFFFF);
    j = h_jcc(e, HC_NE);
    h_ld32(e, T0, REG_CORE, REGOFF(DSP_REG_R0 + n));
    switch (modkind) {
    case MOD_IMM:
        h_add_ri(e, 0, T0, imm);
        break;
    case MOD_PLUS_N:
        h_add_r_mem32(e, T0, REG_CORE, REGOFF(DSP_REG_N0 + n));
        break;
    case MOD_MINUS_N:
        h_sub_r_mem32(e, T0, REG_CORE, REGOFF(DSP_REG_N0 + n));
        break;
    }
    h_and_ri(e, 0, T0, 0xFFFF);
    h_st32(e, REG_CORE, REGOFF(DSP_REG_R0 + n), T0);
    Stub *s = stub_new(t, STUB_RN_UPDATE, j);
    s->n = n;
    s->modkind = modkind;
    s->imm = imm;
    s->back = e->len;
}

typedef struct Ea {
    int kind;           /* EA_REG: the address is in reg; EA_ABS: it is
                           abs; EA_IMM: the operand is the value imm */
    int reg;
    uint32_t abs;
    uint32_t imm;
} Ea;

enum { EA_REG, EA_ABS, EA_IMM };

/* emu_calc_ea: the 6-bit MMMRRR field, the address into reg (or, for the
 * absolute and immediate forms, known now); ext is the instruction's
 * second word. Charges the extra cycles. */
static void emit_calc_ea(Tr *t, uint32_t ea6, uint32_t ext, int reg, Ea *ea)
{
    Emitter *e = &t->e;
    int mode = (ea6 >> 3) & 7;
    int n = ea6 & 7;

    ea->kind = EA_REG;
    ea->reg = reg;
    switch (mode) {
    case 0:
        h_ld32(e, reg, REG_CORE, REGOFF(DSP_REG_R0 + n));
        emit_rn_update(t, n, MOD_MINUS_N, 0);
        break;
    case 1:
        h_ld32(e, reg, REG_CORE, REGOFF(DSP_REG_R0 + n));
        emit_rn_update(t, n, MOD_PLUS_N, 0);
        break;
    case 2:
        h_ld32(e, reg, REG_CORE, REGOFF(DSP_REG_R0 + n));
        emit_rn_update(t, n, MOD_IMM, -1);
        break;
    case 3:
        h_ld32(e, reg, REG_CORE, REGOFF(DSP_REG_R0 + n));
        emit_rn_update(t, n, MOD_IMM, 1);
        break;
    case 4:
        h_ld32(e, reg, REG_CORE, REGOFF(DSP_REG_R0 + n));
        break;
    case 5: {
        size_t j;
        t->cyc += 2;
        h_cmp_mem16_imm(e, REG_CORE, REGOFF(DSP_REG_M0 + n), 0xFFFF);
        j = h_jcc(e, HC_NE);
        h_ld32(e, reg, REG_CORE, REGOFF(DSP_REG_R0 + n));
        h_add_r_mem32(e, reg, REG_CORE, REGOFF(DSP_REG_N0 + n));
        h_and_ri(e, 0, reg, 0xFFFF);
        Stub *s = stub_new(t, STUB_EA_RN_PLUS_N, j);
        s->n = n;
        s->reg = reg;
        s->back = e->len;
        break;
    }
    case 6:
        t->cyc += 2;
        if (n != 0) {
            ea->kind = EA_IMM;
            ea->imm = ext;
        } else {
            ea->kind = EA_ABS;
            ea->abs = ext;
        }
        break;
    case 7:
        t->cyc += 2;
        emit_rn_update(t, n, MOD_IMM, -1);
        h_ld32(e, reg, REG_CORE, REGOFF(DSP_REG_R0 + n));
        break;
    }
}

/* the address of an EA_ABS/EA_REG operand into reg (EA_ABS: materialized) */
static int ea_addr_reg(Tr *t, Ea *ea)
{
    if (ea->kind == EA_ABS) {
        h_mov_ri(&t->e, ea->reg, ea->abs);
    }
    return ea->reg;
}

/* --- registers in moves --- */

static bool G_GNUC_UNUSED reg_is_acc_part(int r)
{
    return r >= DSP_REG_A0 && r <= DSP_REG_B;
}

/* whether the translator moves to/from this register */
static bool reg_supported(int r)
{
    return (r >= DSP_REG_X0 && r <= DSP_REG_B) ||
           (r >= DSP_REG_R0 && r <= DSP_REG_M7);
}

/* an accumulator read as a 24-bit value, scaled and limited
 * (emu_pm_read_accu24): sets L when it limits */
static void emit_acc_limit(Tr *t, int n, int out)
{
    Emitter *e = &t->e;
    int r = acc_reg(n);
    size_t j, j2 = 0;
    int shift;

    acc_load(t, n);
    switch (t->mode) {
    case 1:
        /* scaled down: no limiting when A2 is 0 or 0xff */
        shift = 25;
        h_mov_rr(e, 1, T2, r);
        h_sar_ri(e, 1, T2, 48);
        h_add_ri(e, 1, T2, 1);
        h_cmp_ri(e, 1, T2, 1);
        j = h_jcc(e, HC_A);
        break;
    case 2:
        /* scaled up: no limiting when A2 is 0 with bit 46 clear, or
         * 0xff with it set */
        shift = 23;
        h_mov_rr(e, 1, T2, r);
        h_shr_ri(e, 1, T2, 46);
        h_and_ri(e, 0, T2, 0x3FD);
        j2 = h_jcc(e, HC_EQ);
        h_cmp_ri(e, 0, T2, 0x3FD);
        j = h_jcc(e, HC_NE);
        h_patch(e, j2, e->len);
        break;
    default:
        /* no limiting when the value fits 48 bits: bits 55..47 all equal */
        shift = 24;
        h_mov_rr(e, 1, T2, r);
        h_sar_ri(e, 1, T2, 47);
        h_add_ri(e, 1, T2, 1);
        h_cmp_ri(e, 1, T2, 1);
        j = h_jcc(e, HC_A);
        break;
    }
    h_mov_rr(e, 1, out, r);
    h_shr_ri(e, 1, out, shift);
    h_and_ri(e, 0, out, 0xFFFFFF);
    Stub *s = stub_new(t, STUB_LIMIT, j);
    s->reg = r;
    s->reg2 = out;
    s->back = e->len;
}

/* a register's value, as the parallel moves read it, into out */
static void emit_read_reg(Tr *t, int r, int out)
{
    Emitter *e = &t->e;

    switch (r) {
    case DSP_REG_A:
    case DSP_REG_B:
        emit_acc_limit(t, r & 1, out);
        break;
    case DSP_REG_A0:
    case DSP_REG_B0:
        acc_load(t, r & 1);
        h_mov_rr(e, 1, out, acc_reg(r & 1));
        h_and_ri(e, 0, out, 0xFFFFFF);
        break;
    case DSP_REG_A1:
    case DSP_REG_B1:
        acc_load(t, r & 1);
        h_mov_rr(e, 1, out, acc_reg(r & 1));
        h_shr_ri(e, 1, out, 24);
        h_and_ri(e, 0, out, 0xFFFFFF);
        break;
    case DSP_REG_A2:
    case DSP_REG_B2:
        acc_load(t, r & 1);
        h_mov_rr(e, 1, out, acc_reg(r & 1));
        h_shr_ri(e, 1, out, 48);
        h_and_ri(e, 0, out, 0xFF);
        break;
    default:
        h_ld32(e, out, REG_CORE, REGOFF(r));
        break;
    }
}

/* a register written with the value in val (any 32-bit value; masked as
 * the interpreter's dsp_write_reg / registers_mask mask it). val is
 * consumed. */
static void emit_write_reg(Tr *t, int r, int val)
{
    Emitter *e = &t->e;
    int n = r & 1;
    int acc = acc_reg(n);

    if (r >= DSP_REG_A0 && r <= DSP_REG_B && t->lazy_acc == n) {
        /* the flags are the value's before the move */
        emit_flags_flush(t);
    }

    switch (r) {
    case DSP_REG_A:
    case DSP_REG_B:
        /* 24 bits sign-extended into the whole accumulator */
        h_mov_rr(e, 1, acc, val);
        h_shl_ri(e, 1, acc, 40);
        h_sar_ri(e, 1, acc, 16);
        acc_written(t, n);
        break;
    case DSP_REG_A0:
    case DSP_REG_B0:
        acc_load(t, n);
        h_mov_ri(e, T0, ~(uint64_t)0xFFFFFF);
        h_and_rr(e, 1, acc, T0);
        h_and_ri(e, 0, val, 0xFFFFFF);
        h_or_rr(e, 1, acc, val);
        acc_written(t, n);
        break;
    case DSP_REG_A1:
    case DSP_REG_B1:
        acc_load(t, n);
        h_mov_ri(e, T0, ~((uint64_t)0xFFFFFF << 24));
        h_and_rr(e, 1, acc, T0);
        h_and_ri(e, 0, val, 0xFFFFFF);
        h_shl_ri(e, 1, val, 24);
        h_or_rr(e, 1, acc, val);
        acc_written(t, n);
        break;
    case DSP_REG_A2:
    case DSP_REG_B2:
        acc_load(t, n);
        h_shl_ri(e, 1, acc, 16);
        h_shr_ri(e, 1, acc, 16);
        h_and_ri(e, 0, val, 0xFF);
        h_shl_ri(e, 1, val, 56);
        h_sar_ri(e, 1, val, 8);
        h_or_rr(e, 1, acc, val);
        acc_written(t, n);
        break;
    case DSP_REG_X0:
    case DSP_REG_X1:
    case DSP_REG_Y0:
    case DSP_REG_Y1:
        h_and_ri(e, 0, val, 0xFFFFFF);
        h_st32(e, REG_CORE, REGOFF(r), val);
        break;
    default:
        /* R, N, M: 16 bits */
        h_and_ri(e, 0, val, 0xFFFF);
        h_st32(e, REG_CORE, REGOFF(r), val);
        break;
    }
}

/* --- the data ALU --- */

enum {
    OP_ASL_N, OP_ASR_N, /* by a count (shift), of accumulator s into d */
    OP_MOVE, OP_ADD, OP_SUB, OP_CMP, OP_TFR, OP_CLR, OP_TST, OP_RND,
    OP_MPY, OP_MPYR, OP_MAC, OP_MACR, OP_ASL, OP_ASR, OP_LSL, OP_LSR,
    OP_NEG, OP_NOT, OP_AND, OP_OR, OP_EOR, OP_ABS, OP_ADDL, OP_ADDR,
    OP_SUBL, OP_SUBR,
    OP_UNSUPPORTED
};

/* the operand of an ALU operation other than the destination */
enum {
    SRC_NONE,
    SRC_X0 = DSP_REG_X0, SRC_X1 = DSP_REG_X1, SRC_Y0 = DSP_REG_Y0, SRC_Y1 = DSP_REG_Y1,
    SRC_X = 0x100, SRC_Y, SRC_OTHER_ACC,
    SRC_IMM,            /* a 24-bit immediate (imm), the instruction's */
};

typedef struct AluOp {
    int op;
    int src;            /* SRC_*; for the multiplies, the first factor */
    int src2;           /* the multiplies' second factor */
    bool minus;         /* the multiplies' sign */
    int d;              /* 0: A, 1: B */
    uint32_t imm;       /* SRC_IMM's value */
    bool imm_unsigned;  /* ... not sign-extended (cmp #xx) */
    int shift;          /* asl/asr #ii's count */
    int s;              /* asl/asr #ii's source accumulator */
} AluOp;

/* opcodes_alu's 256 entries, decoded */
static void decode_alu(uint32_t code, AluOp *a)
{
    static const int ops_0_3f[32] = {
        /* 0x */ OP_MOVE, OP_TFR, OP_ADDR, OP_TST, OP_UNSUPPORTED, OP_CMP, OP_SUBR, OP_UNSUPPORTED,
        /* 1x */ OP_ADD, OP_RND, OP_ADDL, OP_CLR, OP_SUB, OP_UNSUPPORTED, OP_SUBL, OP_NOT,
        /* 2x */ OP_ADD, OP_UNSUPPORTED, OP_ASR, OP_LSR, OP_SUB, OP_UNSUPPORTED, OP_ABS, OP_UNSUPPORTED,
        /* 3x */ OP_ADD, OP_UNSUPPORTED, OP_ASL, OP_LSL, OP_SUB, OP_UNSUPPORTED, OP_NEG, OP_UNSUPPORTED,
    };
    static const int ops_40_7f[8] = {
        OP_ADD, OP_TFR, OP_OR, OP_EOR, OP_SUB, OP_CMP, OP_AND, OP_UNSUPPORTED /* cmpm */
    };
    static const int qqq1[8] = { DSP_REG_X0, DSP_REG_Y0, DSP_REG_X1, DSP_REG_Y1,
                                 DSP_REG_X0, DSP_REG_Y0, DSP_REG_X1, DSP_REG_Y1 };
    static const int qqq2[8] = { DSP_REG_X0, DSP_REG_Y0, DSP_REG_X0, DSP_REG_Y0,
                                 DSP_REG_Y1, DSP_REG_X0, DSP_REG_Y0, DSP_REG_X1 };

    memset(a, 0, sizeof(*a));
    a->d = (code >> 3) & 1;
    a->src = SRC_NONE;
    if (code < 0x40) {
        int row = code >> 4;
        a->op = ops_0_3f[row * 8 + (code & 7)];
        if (a->op == OP_MOVE && code != 0) {
            a->op = OP_UNSUPPORTED;
        }
        switch (row) {
        case 0:
        case 1:
            a->src = SRC_OTHER_ACC;
            break;
        case 2:
            a->src = SRC_X;
            break;
        case 3:
            a->src = SRC_Y;
            break;
        }
    } else if (code < 0x80) {
        static const int srcs[4] = { DSP_REG_X0, DSP_REG_Y0, DSP_REG_X1, DSP_REG_Y1 };
        a->op = ops_40_7f[code & 7];
        a->src = srcs[(code >> 4) & 3];
    } else {
        static const int mops[4] = { OP_MPY, OP_MPYR, OP_MAC, OP_MACR };
        int qqq = (code >> 4) & 7;
        a->op = mops[code & 3];
        a->minus = (code >> 2) & 1;
        a->src = qqq1[qqq];
        a->src2 = qqq2[qqq];
    }
}

/* the source operand as a 56-bit value sign-extended to 64, into out
 * (uses rax) */
static void emit_src56(Tr *t, const AluOp *a, int out)
{
    Emitter *e = &t->e;

    switch (a->src) {
    case SRC_X:
    case SRC_Y: {
        int lo = a->src == SRC_X ? DSP_REG_X0 : DSP_REG_Y0;
        h_ld32(e, out, REG_CORE, REGOFF(lo + 1));
        h_shl_ri(e, 1, out, 40);
        h_sar_ri(e, 1, out, 16);
        h_ld32(e, T0, REG_CORE, REGOFF(lo));
        h_or_rr(e, 1, out, T0);
        break;
    }
    case SRC_OTHER_ACC:
        acc_load(t, a->d ^ 1);
        h_mov_rr(e, 1, out, acc_reg(a->d ^ 1));
        break;
    case SRC_IMM: {
        int64_t v = a->imm & 0xFFFFFF;
        if (!a->imm_unsigned && (v & 0x800000)) {
            v -= 1 << 24;
        }
        h_mov_ri(e, out, (uint64_t)(v << 24));
        break;
    }
    default:
        h_ld32(e, out, REG_CORE, REGOFF(a->src));
        h_shl_ri(e, 1, out, 40);
        h_sar_ri(e, 1, out, 16);
        break;
    }
}

/* E, U, N, Z of the 56-bit value in res (sign-extended to 64) or-ed into
 * sr (a 32-bit register holding SR with those bits clear), as
 * emu_ccr_update_e_u_n_z computes them in the block's scaling mode (in
 * mode 3 it sets none); uses rax, rdx */
static void emit_eunz(Tr *t, int res, int sr)
{
    Emitter *e = &t->e;
    /* E: the bits from 55 down to this one not all equal; U: this bit
     * and the one below equal */
    static const int ebit[3] = { 47, 48, 46 };
    int b;

    if (t->mode == 3) {
        return;
    }
    b = ebit[t->mode];
    /* Z */
    h_test_rr(e, 1, res, res);
    h_setcc(e, HC_EQ, T0);
    h_shl_ri(e, 0, T0, DSP_SR_Z);
    h_or_rr(e, 0, sr, T0);
    /* N: bit 55 */
    h_mov_rr(e, 1, T0, res);
    h_shr_ri(e, 1, T0, 55 - DSP_SR_N);
    h_and_ri(e, 0, T0, SR_N);
    h_or_rr(e, 0, sr, T0);
    /* E */
    h_mov_rr(e, 1, T0, res);
    h_sar_ri(e, 1, T0, b);
    h_add_ri(e, 1, T0, 1);
    h_cmp_ri(e, 1, T0, 1);
    h_setcc(e, HC_A, T0);
    h_shl_ri(e, 0, T0, DSP_SR_E);
    h_or_rr(e, 0, sr, T0);
    /* U */
    h_mov_rr(e, 1, T0, res);
    h_shr_ri(e, 1, T0, b - 1);
    h_mov_rr(e, 0, T2, T0);
    h_shr_ri(e, 0, T2, 1);
    h_xor_rr(e, 0, T0, T2);
    h_not(e, 0, T0);
    h_and_ri(e, 0, T0, 1);
    h_shl_ri(e, 0, T0, DSP_SR_U);
    h_or_rr(e, 0, sr, T0);
}

/* E, U, N, Z computed from the accumulator they are owed to, into SR
 * (the bits are clear there since the operation); uses rax, rcx, rdx */
static void emit_flags_flush(Tr *t)
{
    Emitter *e = &t->e;

    if (t->lazy_acc < 0) {
        return;
    }
    h_ld32(e, T1, REG_CORE, REGOFF(DSP_REG_SR));
    emit_eunz(t, acc_reg(t->lazy_acc), T1);
    h_st32(e, REG_CORE, REGOFF(DSP_REG_SR), T1);
    t->lazy_acc = -1;
}

/* the same, out of line, from a stub's snapshot of the state */
static void emit_flags_flush_stub(Tr *t, const Stub *s)
{
    Emitter *e = &t->e;

    if (s->lazy_acc < 0) {
        return;
    }
    h_ld32(e, T1, REG_CORE, REGOFF(DSP_REG_SR));
    emit_eunz(t, acc_reg(s->lazy_acc), T1);
    h_st32(e, REG_CORE, REGOFF(DSP_REG_SR), T1);
}

/* SR = (SR & ~clear) | newsr (a register, or none) | EUNZ(res). With
 * lazy_n an accumulator (0, 1), E, U, N, Z are not computed now: the
 * result ends up in that accumulator, and they are computed from it when
 * SR is next needed (emit_flags_flush), which is once for a run of
 * operations rather than once each. */
static void emit_sr_result(Tr *t, uint32_t clear, int newsr, int res, int lazy_n)
{
    Emitter *e = &t->e;

    h_ld32(e, T1, REG_CORE, REGOFF(DSP_REG_SR));
    h_and_ri(e, 0, T1, ~(int32_t)clear);
    if (newsr >= 0) {
        h_or_rr(e, 0, T1, newsr);
    }
    if (res >= 0 && lazy_n < 0) {
        emit_eunz(t, res, T1);
    }
    h_st32(e, REG_CORE, REGOFF(DSP_REG_SR), T1);
    /* the bits are this operation's now, whatever was pending */
    t->lazy_acc = (clear & SR_EUNZ) == SR_EUNZ ? lazy_n : -1;
}

/* res = sext56(res) */
static void emit_sext56(Emitter *e, int res)
{
    h_shl_ri(e, 1, res, 8);
    h_sar_ri(e, 1, res, 8);
}

/* dst = dst +/- src as 56-bit values (dsp_add56/dsp_sub56): the result
 * in r14, and the carry (bit 0), overflow (V, bit 1) and L (bit 6) flags
 * in r15d. Both shifted up by 8 so the host's carry and overflow are the
 * 56-bit add's. Uses rax, rdx. */
static void emit_addsub56(Emitter *e, int dst, int src, bool sub)
{
    h_mov_rr(e, 1, T3, dst);
    h_shl_ri(e, 1, T3, 8);
    h_mov_rr(e, 1, T4, src);
    h_shl_ri(e, 1, T4, 8);
    if (sub) {
        h_sub_rr(e, 1, T3, T4);
    } else {
        h_add_rr(e, 1, T3, T4);
    }
    h_setcc(e, sub ? HC_BORROW : HC_CARRY, T4);
    h_setcc(e, HC_O, T2);
    h_sar_ri(e, 1, T3, 8);
    h_shl_ri(e, 0, T2, DSP_SR_V);
    h_or_rr(e, 0, T4, T2);
    h_shl_ri(e, 0, T2, DSP_SR_L - DSP_SR_V);
    h_or_rr(e, 0, T4, T2);
}

/* dsp_rnd56 in the block's scaling mode, on res; uses rax */
static void emit_rnd56(Tr *t, int res)
{
    Emitter *e = &t->e;
    size_t j;

    if (t->mode & 1) {
        /* S0: rounding at bit 24 */
        h_add_ri(e, 1, res, 1 << 24);
        emit_sext56(e, res);
        h_mov_rr(e, 1, T0, res);
        h_and_ri(e, 1, T0, 0x1FFFFFF);
        j = h_jcc(e, HC_NE);
        h_and_ri(e, 1, res, ~(int32_t)(3 << 24));
        h_patch(e, j, e->len);
        h_shr_ri(e, 1, res, 25);
        h_shl_ri(e, 1, res, 25);
    } else if (t->mode == 2) {
        /* S1: rounding at bit 22 */
        h_add_ri(e, 1, res, 1 << 22);
        emit_sext56(e, res);
        h_mov_rr(e, 1, T0, res);
        h_and_ri(e, 0, T0, 0x7FFFFF);
        j = h_jcc(e, HC_NE);
        h_and_ri(e, 1, res, ~(int32_t)0x800000);
        h_patch(e, j, e->len);
        h_shr_ri(e, 1, res, 23);
        h_shl_ri(e, 1, res, 23);
    } else {
        h_add_ri(e, 1, res, 1 << 23);
        emit_sext56(e, res);
        h_mov_rr(e, 1, T0, res);
        h_and_ri(e, 0, T0, 0xFFFFFF);
        j = h_jcc(e, HC_NE);
        h_and_ri(e, 1, res, ~(int32_t)(1 << 24));
        h_patch(e, j, e->len);
        h_shr_ri(e, 1, res, 24);
        h_shl_ri(e, 1, res, 24);
    }
}

/* the product of the two 24-bit registers, times two, into rax
 * (dsp_mul56, exact in 64 bits) */
static void emit_mul56(Emitter *e, const AluOp *a)
{
    h_ld32(e, T0, REG_CORE, REGOFF(a->src));
    h_shl_ri(e, 1, T0, 40);
    h_sar_ri(e, 1, T0, 40);
    h_ld32(e, T1, REG_CORE, REGOFF(a->src2));
    h_shl_ri(e, 1, T1, 40);
    h_sar_ri(e, 1, T1, 40);
    h_mul_rr(e, 1, T0, T1);
    h_add_rr(e, 1, T0, T0);
    if (a->minus) {
        h_neg(e, 1, T0);
    }
}

/* the A1/B1-only logic operations: the 24-bit value in eax, with the
 * flags from it, back into the accumulator */
static void emit_a1_result(Tr *t, int n, uint32_t clear, int carry_reg)
{
    Emitter *e = &t->e;
    int acc = acc_reg(n);

    /* N, Z of the 24-bit value */
    h_mov_rr(e, 0, T2, T0);
    h_shr_ri(e, 0, T2, 23 - DSP_SR_N);
    h_and_ri(e, 0, T2, SR_N);
    h_ld32(e, T1, REG_CORE, REGOFF(DSP_REG_SR));
    h_and_ri(e, 0, T1, ~(int32_t)clear);
    h_or_rr(e, 0, T1, T2);
    if (carry_reg >= 0) {
        h_or_rr(e, 0, T1, carry_reg);
    }
    h_test_rr(e, 0, T0, T0);
    h_setcc(e, HC_EQ, T2);
    h_shl_ri(e, 0, T2, DSP_SR_Z);
    h_or_rr(e, 0, T1, T2);
    h_st32(e, REG_CORE, REGOFF(DSP_REG_SR), T1);
    /* into bits 47..24 */
    h_mov_ri(e, T2, ~((uint64_t)0xFFFFFF << 24));
    h_and_rr(e, 1, acc, T2);
    h_shl_ri(e, 1, T0, 24);
    h_or_rr(e, 1, acc, T0);
    acc_written(t, n);
}

/* the operation, on the accumulators in their registers */
static void emit_alu(Tr *t, const AluOp *a)
{
    Emitter *e = &t->e;
    int acc = acc_reg(a->d);

    switch (a->op) {
    case OP_MOVE:
        break;

    case OP_ADD:
    case OP_SUB:
    case OP_CMP:
        acc_load(t, a->d);
        emit_src56(t, a, T1);
        emit_addsub56(e, acc, T1, a->op != OP_ADD);
        emit_sr_result(t, SR_EUNZ | SR_V | SR_C, T4, T3, a->op == OP_CMP ? -1 : a->d);
        if (a->op != OP_CMP) {
            h_mov_rr(e, 1, acc, T3);
            acc_written(t, a->d);
        }
        break;

    case OP_ADDL:
    case OP_ADDR:
    case OP_SUBL:
    case OP_SUBR: {
        /* the destination shifted (its flags in ecx), then the other
         * accumulator added or subtracted (its flags or-ed in) */
        bool left = a->op == OP_ADDL || a->op == OP_SUBL;
        bool sub = a->op == OP_SUBL || a->op == OP_SUBR;
        acc_load(t, a->d);
        acc_load(t, a->d ^ 1);
        h_mov_rr(e, 1, T3, acc);
        if (left) {
            /* dsp_asl56: C = L = bit 55, V = bit 55 ^ bit 54 */
            h_mov_rr(e, 1, T0, acc);
            h_shr_ri(e, 1, T0, 55);
            h_and_ri(e, 0, T0, 1);
            h_mov_rr(e, 1, T1, acc);
            h_shr_ri(e, 1, T1, 54);
            h_xor_rr(e, 0, T1, T0);
            h_and_ri(e, 0, T1, 1);
            h_shl_ri(e, 0, T1, DSP_SR_V);
            h_or_rr(e, 0, T1, T0);
            h_shl_ri(e, 0, T0, DSP_SR_L);
            h_or_rr(e, 0, T1, T0);
            h_shl_ri(e, 1, T3, 9);
            h_sar_ri(e, 1, T3, 8);
        } else {
            /* dsp_asr56: C = bit 0, a logical shift of the 56 bits */
            h_mov_rr(e, 1, T1, acc);
            h_and_ri(e, 0, T1, 1);
            h_shl_ri(e, 1, T3, 8);
            h_shr_ri(e, 1, T3, 9);
        }
        emit_addsub56(e, T3, acc_reg(a->d ^ 1), sub);
        h_or_rr(e, 0, T4, T1);
        emit_sr_result(t, SR_EUNZ | SR_V | SR_C, T4, T3, a->d);
        h_mov_rr(e, 1, acc, T3);
        acc_written(t, a->d);
        break;
    }

    case OP_TFR:
        emit_src56(t, a, T1);
        h_mov_rr(e, 1, acc, T1);
        acc_written(t, a->d);
        break;

    case OP_CLR:
        emit_flags_flush(t);
        h_xor_rr(e, 0, acc, acc);
        acc_written(t, a->d);
        h_and_mem32_imm(e, REG_CORE, REGOFF(DSP_REG_SR), ~(int32_t)(SR_E | SR_N | SR_V));
        h_or_mem32_imm(e, REG_CORE, REGOFF(DSP_REG_SR), SR_U | SR_Z);
        break;

    case OP_TST:
        acc_load(t, a->d);
        emit_sr_result(t, SR_EUNZ | SR_V, -1, acc, a->d);
        break;

    case OP_RND:
        acc_load(t, a->d);
        emit_rnd56(t, acc);
        acc_written(t, a->d);
        emit_sr_result(t, SR_EUNZ, -1, acc, a->d);
        break;

    case OP_MPY:
    case OP_MPYR:
        emit_mul56(e, a);
        h_mov_rr(e, 1, acc, T0);
        if (a->op == OP_MPYR) {
            emit_rnd56(t, acc);
        }
        acc_written(t, a->d);
        emit_sr_result(t, SR_EUNZ | SR_V, -1, acc, a->d);
        break;

    case OP_MAC:
    case OP_MACR:
        acc_load(t, a->d);
        emit_mul56(e, a);
        emit_addsub56(e, acc, T0, false);
        if (a->op == OP_MACR) {
            emit_rnd56(t, T3);
        }
        /* V and L from the add, C untouched */
        h_and_ri(e, 0, T4, ~(int32_t)SR_C);
        emit_sr_result(t, SR_EUNZ | SR_V, T4, T3, a->d);
        h_mov_rr(e, 1, acc, T3);
        acc_written(t, a->d);
        break;

    case OP_ASL:
        acc_load(t, a->d);
        /* dsp_asl56: C = L = bit 55, V = bit 55 ^ bit 54 */
        h_mov_rr(e, 1, T0, acc);
        h_shr_ri(e, 1, T0, 55);
        h_and_ri(e, 0, T0, 1);
        h_mov_rr(e, 1, T2, acc);
        h_shr_ri(e, 1, T2, 54);
        h_xor_rr(e, 0, T2, T0);
        h_and_ri(e, 0, T2, 1);
        h_shl_ri(e, 0, T2, DSP_SR_V);
        h_mov_rr(e, 0, T4, T0);
        h_or_rr(e, 0, T4, T2);
        h_shl_ri(e, 0, T0, DSP_SR_L);
        h_or_rr(e, 0, T4, T0);
        h_shl_ri(e, 1, acc, 9);
        h_sar_ri(e, 1, acc, 8);
        acc_written(t, a->d);
        emit_sr_result(t, SR_EUNZ | SR_V | SR_C, T4, acc, a->d);
        break;

    case OP_ASL_N: {
        /* dsp_asl56 by n: C = bit 56-n, L = any of the bits from it up,
         * V = bit 55 changed */
        int n = a->shift;
        acc_load(t, a->s);
        h_mov_rr(e, 1, T0, acc_reg(a->s));
        h_shl_ri(e, 1, T0, 8);
        h_shr_ri(e, 1, T0, 8);
        h_mov_rr(e, 1, T2, T0);
        h_shr_ri(e, 1, T2, 56 - n);
        h_mov_rr(e, 0, T4, T2);
        h_and_ri(e, 0, T4, 1);
        h_test_rr(e, 1, T2, T2);
        h_setcc(e, HC_NE, T2);
        h_shl_ri(e, 0, T2, DSP_SR_L);
        h_or_rr(e, 0, T4, T2);
        h_shl_ri(e, 1, T0, n);
        emit_sext56(e, T0);
        h_mov_rr(e, 1, T2, T0);
        h_xor_rr(e, 1, T2, acc_reg(a->s));
        h_shr_ri(e, 1, T2, 55 - DSP_SR_V);
        h_and_ri(e, 0, T2, SR_V);
        h_or_rr(e, 0, T4, T2);
        h_mov_rr(e, 1, acc, T0);
        acc_written(t, a->d);
        emit_sr_result(t, SR_EUNZ | SR_V | SR_C, T4, acc, a->d);
        break;
    }

    case OP_ASR_N: {
        /* dsp_asr56 by n: C = bit n-1, a logical shift of the 56 bits */
        int n = a->shift;
        acc_load(t, a->s);
        h_mov_rr(e, 1, T0, acc_reg(a->s));
        h_shl_ri(e, 1, T0, 8);
        h_shr_ri(e, 1, T0, 8);
        h_mov_rr(e, 1, T4, T0);
        h_shr_ri(e, 1, T4, (n - 1) & 63);
        h_and_ri(e, 0, T4, 1);
        h_shr_ri(e, 1, T0, n);
        h_mov_rr(e, 1, acc, T0);
        acc_written(t, a->d);
        emit_sr_result(t, SR_EUNZ | SR_V | SR_C, T4, acc, a->d);
        break;
    }

    case OP_ASR:
        acc_load(t, a->d);
        /* dsp_asr56: C = bit 0, a logical shift of the 56 bits */
        h_mov_rr(e, 1, T4, acc);
        h_and_ri(e, 0, T4, 1);
        h_shl_ri(e, 1, acc, 8);
        h_shr_ri(e, 1, acc, 9);
        acc_written(t, a->d);
        emit_sr_result(t, SR_EUNZ | SR_V | SR_C, T4, acc, a->d);
        break;

    case OP_NEG:
    case OP_ABS: {
        size_t j = 0;
        acc_load(t, a->d);
        /* overflow when the value is the most negative one */
        h_mov_ri(e, T0, (uint64_t)-((int64_t)1 << 55));
        h_cmp_rr(e, 1, acc, T0);
        h_setcc(e, HC_EQ, T4);
        h_mov_rr(e, 0, T2, T4);
        h_shl_ri(e, 0, T4, DSP_SR_V);
        h_shl_ri(e, 0, T2, DSP_SR_L);
        h_or_rr(e, 0, T4, T2);
        if (a->op == OP_ABS) {
            h_test_rr(e, 1, acc, acc);
            j = h_jcc(e, HC_NS);
        }
        h_neg(e, 1, acc);
        emit_sext56(e, acc);
        if (a->op == OP_ABS) {
            h_patch(e, j, e->len);
        }
        acc_written(t, a->d);
        emit_sr_result(t, SR_EUNZ | SR_V, T4, acc, a->d);
        break;
    }

    case OP_NOT:
    case OP_AND:
    case OP_OR:
    case OP_EOR:
        emit_flags_flush(t);
        acc_load(t, a->d);
        h_mov_rr(e, 1, T0, acc);
        h_shr_ri(e, 1, T0, 24);
        h_and_ri(e, 0, T0, 0xFFFFFF);
        switch (a->op) {
        case OP_NOT:
            h_not(e, 0, T0);
            h_and_ri(e, 0, T0, 0xFFFFFF);
            break;
        case OP_AND:
            if (a->src == SRC_IMM) {
                h_and_ri(e, 0, T0, a->imm & 0xFFFFFF);
            } else {
                h_and_r_mem32(e, T0, REG_CORE, REGOFF(a->src));
            }
            break;
        case OP_OR:
            h_or_r_mem32(e, T0, REG_CORE, REGOFF(a->src));
            break;
        case OP_EOR:
            h_xor_r_mem32(e, T0, REG_CORE, REGOFF(a->src));
            break;
        }
        emit_a1_result(t, a->d, SR_N | SR_Z | SR_V, -1);
        break;

    case OP_LSL:
    case OP_LSR:
        emit_flags_flush(t);
        acc_load(t, a->d);
        h_mov_rr(e, 1, T0, acc);
        h_shr_ri(e, 1, T0, 24);
        h_and_ri(e, 0, T0, 0xFFFFFF);
        h_mov_rr(e, 0, T4, T0);
        if (a->op == OP_LSL) {
            h_shr_ri(e, 0, T4, 23);
            h_shl_ri(e, 0, T0, 1);
            h_and_ri(e, 0, T0, 0xFFFFFF);
        } else {
            h_and_ri(e, 0, T4, 1);
            h_shr_ri(e, 0, T0, 1);
        }
        emit_a1_result(t, a->d, SR_C | SR_N | SR_Z | SR_V, T4);
        break;

    default:
        g_assert_not_reached();
    }
}

/* --- the instructions --- */


static bool alu_supported(uint32_t code)
{
    AluOp a;
    decode_alu(code, &a);
    return a.op != OP_UNSUPPORTED;
}

/* whether the parallel-move instruction is one the translator does */
static bool pm_supported(const Insn *in)
{
    uint32_t inst = in->inst;
    int type = (inst >> 20) & 0xF;

    if (!alu_supported(inst & 0xFF)) {
        return false;
    }
    switch (type) {
    case 0:
    case 1:
        return false;
    case 2:
        if ((inst & 0xffff00) == 0x200000 || (inst & 0xffe000) == 0x204000) {
            return true;
        }
        if ((inst & 0xfc0000) == 0x200000) {
            int src = (inst >> 13) & 0x1F, dst = (inst >> 8) & 0x1F;
            return reg_supported(src) && reg_supported(dst);
        }
        return reg_supported((inst >> 16) & 0x1F);
    case 3:
        return reg_supported((inst >> 16) & 0x1F);
    case 4:
        if ((inst & 0xf40000) == 0x400000) {
            return true;
        }
        /* fallthrough: pm_5 */
    case 5:
    case 6:
    case 7: {
        int numreg = ((inst >> 16) & 7) | ((inst >> 17) & 0x18);
        return reg_supported(numreg);
    }
    default:
        return true;
    }
}

static void emit_pm_2(Tr *t, const Insn *in, const AluOp *a)
{
    Emitter *e = &t->e;
    uint32_t inst = in->inst;

    if ((inst & 0xffff00) == 0x200000) {
        emit_alu(t, a);
        return;
    }
    if ((inst & 0xffe000) == 0x204000) {
        Ea ea;
        emit_calc_ea(t, (inst >> 8) & 0x1F, in->ext, EA1, &ea);
        emit_alu(t, a);
        return;
    }
    if ((inst & 0xfc0000) == 0x200000) {
        /* S,D */
        int src = (inst >> 13) & 0x1F, dst = (inst >> 8) & 0x1F;
        emit_read_reg(t, src, V1);
        emit_alu(t, a);
        emit_write_reg(t, dst, V1);
        return;
    }
    /* #xx,D (emu_pm_3) */
    {
        int dst = (inst >> 16) & 0x1F;
        uint32_t v = (inst >> 8) & 0xFF;
        if ((dst >= DSP_REG_X0 && dst <= DSP_REG_Y1) || dst == DSP_REG_A || dst == DSP_REG_B) {
            v <<= 16;
        }
        emit_alu(t, a);
        h_mov_ri(e, V1, v);
        emit_write_reg(t, dst, V1);
    }
}

static void emit_pm_4x(Tr *t, const Insn *in, const AluOp *a)
{
    Emitter *e = &t->e;
    uint32_t inst = in->inst;
    uint32_t value = (inst >> 8) & 0x3F;
    int numreg = ((inst >> 16) & 3) | ((inst >> 17) & 4);
    bool write = inst & (1 << 15);
    Ea ea;

    if (inst & (1 << 14)) {
        emit_calc_ea(t, value, in->ext, EA1, &ea);
    } else {
        ea.kind = EA_ABS;
        ea.reg = EA1;
        ea.abs = value;
    }
    if (ea.kind == EA_IMM) {
        /* the interpreter treats the immediate as an address here */
        ea.kind = EA_ABS;
        ea.abs = ea.imm;
    }
    int addr = ea_addr_reg(t, &ea);

    if (write) {
        emit_read_x(t, addr, V1);
        emit_read_y(t, addr, V2);
    } else {
        switch (numreg) {
        case 0: /* A10 */
        case 1:
            emit_read_reg(t, DSP_REG_A1 + numreg, V1);
            emit_read_reg(t, DSP_REG_A0 + numreg, V2);
            break;
        case 2: /* X */
        case 3:
            emit_read_reg(t, DSP_REG_X1 + (numreg - 2) * 2, V1);
            emit_read_reg(t, DSP_REG_X0 + (numreg - 2) * 2, V2);
            break;
        case 4: /* A */
        case 5: {
            /* limited: the low part is all ones or all zeros */
            int n = numreg - 4;
            size_t j;
            acc_load(t, n);
            emit_acc_limit(t, n, V1);
            /* limited iff the value does not fit 48 bits */
            h_mov_rr(e, 1, V2, acc_reg(n));
            h_and_ri(e, 0, V2, 0xFFFFFF);
            h_mov_rr(e, 1, T2, acc_reg(n));
            h_sar_ri(e, 1, T2, 47);
            h_add_ri(e, 1, T2, 1);
            h_cmp_ri(e, 1, T2, 1);
            j = h_jcc(e, HC_BE);
            h_mov_rr(e, 0, V2, V1);
            h_shr_ri(e, 0, V2, 23);
            h_and_ri(e, 0, V2, 1);
            h_sub_ri(e, 0, V2, 1);
            h_and_ri(e, 0, V2, 0xFFFFFF);
            h_patch(e, j, e->len);
            break;
        }
        case 6: /* AB */
            emit_acc_limit(t, 0, V1);
            emit_acc_limit(t, 1, V2);
            break;
        case 7: /* BA */
            emit_acc_limit(t, 1, V1);
            emit_acc_limit(t, 0, V2);
            break;
        }
    }

    emit_alu(t, a);

    if (write) {
        switch (numreg) {
        case 0:
        case 1:
            emit_write_reg(t, DSP_REG_A1 + numreg, V1);
            emit_write_reg(t, DSP_REG_A0 + numreg, V2);
            break;
        case 2:
        case 3:
            emit_write_reg(t, DSP_REG_X1 + (numreg - 2) * 2, V1);
            emit_write_reg(t, DSP_REG_X0 + (numreg - 2) * 2, V2);
            break;
        case 4:
        case 5: {
            int n = numreg - 4;
            int acc = acc_reg(n);
            /* A0 = V2, A1 = V1, A2 = sign of V1 */
            h_mov_rr(e, 1, acc, V1);
            h_shl_ri(e, 1, acc, 40);
            h_sar_ri(e, 1, acc, 16);
            h_or_rr(e, 1, acc, V2);
            acc_written(t, n);
            break;
        }
        case 6:
            emit_write_reg(t, DSP_REG_A, V1);
            emit_write_reg(t, DSP_REG_B, V2);
            break;
        case 7:
            emit_write_reg(t, DSP_REG_B, V1);
            emit_write_reg(t, DSP_REG_A, V2);
            break;
        }
    } else {
        emit_write_x(t, addr, V1);
        emit_write_y(t, addr, V2);
    }
}

static void emit_pm_5(Tr *t, const Insn *in, const AluOp *a)
{
    Emitter *e = &t->e;
    uint32_t inst = in->inst;
    uint32_t value = (inst >> 8) & 0x3F;
    int memspace = (inst >> 19) & 1;
    int numreg = ((inst >> 16) & 7) | ((inst >> 17) & 0x18);
    bool write = inst & (1 << 15);
    Ea ea;

    if (inst & (1 << 14)) {
        emit_calc_ea(t, value, in->ext, EA1, &ea);
    } else {
        ea.kind = EA_ABS;
        ea.reg = EA1;
        ea.abs = value;
    }

    if (write) {
        if (ea.kind == EA_IMM) {
            h_mov_ri(e, V1, ea.imm);
        } else {
            emit_read_mem(t, memspace, ea_addr_reg(t, &ea), V1);
        }
    } else {
        if (ea.kind == EA_IMM) {
            /* the interpreter writes to the immediate as an address */
            ea.kind = EA_ABS;
            ea.abs = ea.imm;
        }
        ea_addr_reg(t, &ea);
        emit_read_reg(t, numreg, V1);
    }

    emit_alu(t, a);

    if (write) {
        emit_write_reg(t, numreg, V1);
    } else {
        emit_write_mem(t, memspace, ea.reg, V1);
    }
}

static void emit_pm_8(Tr *t, const Insn *in, const AluOp *a)
{
    uint32_t inst = in->inst;
    static const int regs1[4] = { DSP_REG_X0, DSP_REG_X1, DSP_REG_A, DSP_REG_B };
    static const int regs2[4] = { DSP_REG_Y0, DSP_REG_Y1, DSP_REG_A, DSP_REG_B };
    uint32_t ea1 = (inst >> 8) & 0x1F;
    uint32_t ea2 = ((inst >> 13) & 3) | ((inst >> 17) & 0x18);
    int numreg1 = regs1[(inst >> 18) & 3];
    int numreg2 = regs2[(inst >> 16) & 3];
    bool write1 = inst & (1 << 15);
    bool write2 = inst & (1 << 22);
    Ea e1, e2;

    if ((ea1 >> 3) == 0) {
        ea1 |= 1 << 5;
    }
    if ((ea1 & (1 << 2)) == 0) {
        ea2 |= 1 << 2;
    }
    if ((ea2 >> 3) == 0) {
        ea2 |= 1 << 5;
    }

    emit_calc_ea(t, ea1, in->ext, EA1, &e1);
    emit_calc_ea(t, ea2, in->ext, EA2, &e2);

    if (write1) {
        emit_read_x(t, EA1, V1);
    } else {
        emit_read_reg(t, numreg1, V1);
    }
    if (write2) {
        emit_read_y(t, EA2, V2);
    } else {
        emit_read_reg(t, numreg2, V2);
    }

    emit_alu(t, a);

    if (write1) {
        emit_write_reg(t, numreg1, V1);
    } else {
        emit_write_x(t, EA1, V1);
    }
    if (write2) {
        emit_write_reg(t, numreg2, V2);
    } else {
        emit_write_y(t, EA2, V2);
    }
}

static void emit_parallel(Tr *t, const Insn *in)
{
    uint32_t inst = in->inst;
    int type = (inst >> 20) & 0xF;
    AluOp a;

    decode_alu(inst & 0xFF, &a);
    switch (type) {
    case 2:
    case 3:
        emit_pm_2(t, in, &a);
        break;
    case 4:
        if ((inst & 0xf40000) == 0x400000) {
            emit_pm_4x(t, in, &a);
        } else {
            emit_pm_5(t, in, &a);
        }
        break;
    case 5:
    case 6:
    case 7:
        emit_pm_5(t, in, &a);
        break;
    default:
        emit_pm_8(t, in, &a);
        break;
    }
}

/* the non-parallel instructions the translator does */
enum {
    NP_NONE,
    NP_NOP,
    NP_MOVE_XY_SHORT,   /* move X/Y:(Rn + xxx) <-> R, 7-bit offset */
    NP_MOVE_X_LONG,     /* move X:(Rn + xxxx) <-> R, a second word */
    NP_LUA,
    NP_LUA_REL,
    NP_ALU_IMM,         /* add/cmp/and #, asl/asr #ii: an AluOp (np_alu) */
    NP_MOVEP_EA,        /* movep [X or Y]:ea <-> [X or Y]:pp */
};

/* the AluOp of an NP_ALU_IMM instruction; whether it is one */
static bool np_alu(const Insn *in, AluOp *a)
{
    const char *name = in->d.name;
    uint32_t inst = in->inst;

    memset(a, 0, sizeof(*a));
    a->src = SRC_IMM;
    a->d = (inst >> 3) & 1;
    if (!strcmp(name, "add #xx, D")) {
        a->op = OP_ADD;
        a->imm = (inst >> 8) & 0x3F;
        return true;
    }
    if (!strcmp(name, "add #xxxx, D")) {
        a->op = OP_ADD;
        a->imm = in->ext;
        return true;
    }
    if (!strcmp(name, "cmp #xx, S2")) {
        a->op = OP_CMP;
        a->imm = (inst >> 8) & 0x3F;
        a->imm_unsigned = true;
        return true;
    }
    if (!strcmp(name, "and #xxxx, D")) {
        a->op = OP_AND;
        a->imm = in->ext;
        return true;
    }
    if (!strcmp(name, "asl #ii, S2, D") || !strcmp(name, "asr #ii, S2, D")) {
        a->op = name[1] == 's' && name[2] == 'l' ? OP_ASL_N : OP_ASR_N;
        a->s = (inst >> 7) & 1;
        a->d = inst & 1;
        a->shift = (inst >> 1) & 0x3F;
        /* the interpreter shifts by a count the host cannot (0 for asr,
         * 56 and more): not these */
        return a->shift >= 1 && a->shift <= 55;
    }
    return false;
}

static int np_kind(const Insn *in)
{
    const char *name = in->d.name;

    if (!name) {
        return NP_NONE;
    }
    if (!strcmp(name, "nop")) {
        return NP_NOP;
    }
    if (!strcmp(name, "move X:(Rn + xxx) <-> R") || !strcmp(name, "move Y:(Rn + xxx) <-> R")) {
        int numreg = in->inst & 0xF;
        return numreg >= DSP_REG_X0 ? NP_MOVE_XY_SHORT : NP_NONE;
    }
    if (!strcmp(name, "move X:(Rn + xxxx) <-> R")) {
        return reg_supported(in->inst & 0x3F) ? NP_MOVE_X_LONG : NP_NONE;
    }
    if (!strcmp(name, "lua ea, D")) {
        return NP_LUA;
    }
    if (!strcmp(name, "lua (Rn + aa), D")) {
        return NP_LUA_REL;
    }
    if (!strcmp(name, "movep [X or Y]:ea <-> [X or Y]:pp")) {
        return NP_MOVEP_EA;
    }
    {
        AluOp a;
        if (np_alu(in, &a)) {
            return NP_ALU_IMM;
        }
    }
    return NP_NONE;
}

static void emit_nonparallel(Tr *t, const Insn *in, int kind)
{
    Emitter *e = &t->e;
    uint32_t inst = in->inst;

    switch (kind) {
    case NP_NOP:
        break;

    case NP_MOVE_XY_SHORT: {
        /* emu_move_xy_imm */
        uint32_t xxx = (((inst >> 11) & 0x3F) << 1) + ((inst >> 6) & 1);
        int32_t off = (int32_t)(xxx << 25) >> 25;
        int space = (inst >> 5) & 1;
        bool W = (inst >> 4) & 1;
        int n = (inst >> 8) & 7;
        int numreg = inst & 0xF;
        h_ld32(e, EA1, REG_CORE, REGOFF(DSP_REG_R0 + n));
        h_add_ri(e, 0, EA1, off);
        h_and_ri(e, 0, EA1, 0xFFFFFF);
        if (W) {
            emit_read_mem(t, space, EA1, V1);
            emit_write_reg(t, numreg, V1);
        } else {
            emit_read_reg(t, numreg, V1);
            emit_write_mem(t, space, EA1, V1);
        }
        break;
    }

    case NP_MOVE_X_LONG: {
        /* emu_move_x_long */
        bool W = (inst >> 6) & 1;
        int n = (inst >> 8) & 7;
        int numreg = inst & 0x3F;
        h_ld32(e, EA1, REG_CORE, REGOFF(DSP_REG_R0 + n));
        h_add_ri(e, 0, EA1, in->ext);
        h_and_ri(e, 0, EA1, 0xFFFFFF);
        if (W) {
            emit_read_x(t, EA1, V1);
            emit_write_reg(t, numreg, V1);
        } else {
            emit_read_reg(t, numreg, V1);
            emit_write_x(t, EA1, V1);
        }
        break;
    }

    case NP_LUA: {
        /* emu_lua: the address register's updated value, the register
         * itself unchanged */
        int srcreg = (inst >> 8) & 7;
        int dstreg = inst & 7;
        Ea ea;
        h_ld32(e, V1, REG_CORE, REGOFF(DSP_REG_R0 + srcreg));
        emit_calc_ea(t, (inst >> 8) & 0x1F, in->ext, EA1, &ea);
        h_ld32(e, V2, REG_CORE, REGOFF(DSP_REG_R0 + srcreg));
        h_st32(e, REG_CORE, REGOFF(DSP_REG_R0 + srcreg), V1);
        h_st32(e, REG_CORE, REGOFF((inst & (1 << 3) ? DSP_REG_N0 : DSP_REG_R0) + dstreg), V2);
        t->cyc += 2;
        break;
    }

    case NP_ALU_IMM: {
        AluOp a;
        np_alu(in, &a);
        emit_alu(t, &a);
        break;
    }

    case NP_MOVEP_EA: {
        /* emu_movep_23: a peripheral (through the interpreter's memory
         * access, out of line) and X/Y memory */
        uint32_t peraddr = 0xFFFFC0 + (inst & 0x3F);
        int perspace = (inst >> 16) & 1;
        int easpace = (inst >> 6) & 1;
        Ea ea;
        emit_calc_ea(t, (inst >> 8) & 0x3F, in->ext, EA1, &ea);
        h_mov_ri(e, EA2, peraddr);
        if (inst & (1 << 15)) {
            /* write pp */
            if (ea.kind == EA_IMM) {
                h_mov_ri(e, V1, ea.imm);
            } else {
                emit_read_mem(t, easpace, ea_addr_reg(t, &ea), V1);
            }
            emit_write_mem(t, perspace, EA2, V1);
        } else {
            if (ea.kind == EA_IMM) {
                ea.kind = EA_ABS;
                ea.abs = ea.imm;
            }
            emit_read_mem(t, perspace, EA2, V1);
            emit_write_mem(t, easpace, ea_addr_reg(t, &ea), V1);
        }
        t->cyc += 2;
        break;
    }

    case NP_LUA_REL: {
        uint32_t aa = ((inst >> 4) & 0xF) + (((inst >> 11) & 7) << 4);
        int32_t off = (int32_t)(aa << 25) >> 25;
        int addrreg = (inst >> 8) & 7;
        int dstreg = inst & 7;
        h_ld32(e, V1, REG_CORE, REGOFF(DSP_REG_R0 + addrreg));
        h_add_ri(e, 0, V1, off);
        h_and_ri(e, 0, V1, 0xFFFFFF);
        h_st32(e, REG_CORE, REGOFF((inst & (1 << 3) ? DSP_REG_N0 : DSP_REG_R0) + dstreg), V1);
        t->cyc += 2;
        break;
    }
    }
}

/* cycles: the instruction's; cyc_before: the block's before it */
static void stub_set_insn(Tr *t, Stub *s, const Insn *in, uint32_t cycles, uint32_t cyc_before)
{
    s->back = t->e.len;
    s->in = *in;
    s->at = in->at;
    s->next = in->next;
    s->len = in->len;
    s->cycles = cycles;
    s->cyc_before = cyc_before;
    memcpy(s->loaded, t->loaded, sizeof(s->loaded));
    memcpy(s->dirty, t->dirty, sizeof(s->dirty));
    s->lazy_acc = t->lazy_acc;
}

/* the instruction run by the interpreter's own function, then
 * dsp56k_post, with cyc_before cycles charged; the block ends with it */
static void emit_fallback_body(Emitter *e, const Insn *in, uint32_t cyc_before)
{
    h_st32_imm(e, REG_CORE, OFF(pc), in->at);
    h_st32_imm(e, REG_CORE, OFF(cur_inst), in->inst);
    h_st32_imm(e, REG_CORE, OFF(cur_inst_len), 1);
    h_st16_imm(e, REG_CORE, OFF(instr_cycle), 2);
    h_mov_rr(e, 1, H_ARG0, REG_CORE);
    h_call(e, (const void *)in->d.emu);
    h_mov_rr(e, 1, H_ARG0, REG_CORE);
    h_call(e, (const void *)dsp56k_post);
    emit_charge(e, cyc_before);
}

static void emit_fallback(Tr *t, const Insn *in)
{
    acc_spill_all(t, false);
    emit_fallback_body(&t->e, in, t->cyc);
    emit_jump_to_epilogue(t);
    t->cyc = 0;
}

/* --- control flow --- */

/* emu_calc_cc: the condition into the host's ZF (jne: taken); uses
 * rax, rdx */
static void emit_cc(Emitter *e, int cc)
{
    h_ld32(e, T0, REG_CORE, REGOFF(DSP_REG_SR));
    switch (cc & 7) {
    case 0: /* CC / CS */
        h_and_ri(e, 0, T0, SR_C);
        break;
    case 1: /* GE / LT: N ^ V */
        h_mov_rr(e, 0, T2, T0);
        h_shr_ri(e, 0, T2, DSP_SR_N - DSP_SR_V);
        h_xor_rr(e, 0, T0, T2);
        h_and_ri(e, 0, T0, SR_V);
        break;
    case 2: /* NE / EQ */
        h_and_ri(e, 0, T0, SR_Z);
        break;
    case 3: /* PL / MI */
        h_and_ri(e, 0, T0, SR_N);
        break;
    case 4: /* NN / NR: Z | (~U & ~E) */
        h_mov_rr(e, 0, T2, T0);
        h_not(e, 0, T2);
        h_and_ri(e, 0, T2, SR_U | SR_E);
        h_cmp_ri(e, 0, T2, SR_U | SR_E);
        h_setcc(e, HC_EQ, T2);
        h_shr_ri(e, 0, T0, DSP_SR_Z);
        h_or_rr(e, 0, T0, T2);
        h_and_ri(e, 0, T0, 1);
        break;
    case 5: /* EC / ES */
        h_and_ri(e, 0, T0, SR_E);
        break;
    case 6: /* LC / LS */
        h_and_ri(e, 0, T0, SR_L);
        break;
    case 7: /* GT / LE: Z | (N ^ V) */
        h_mov_rr(e, 0, T2, T0);
        h_shr_ri(e, 0, T2, DSP_SR_N - DSP_SR_V);
        h_xor_rr(e, 0, T2, T0);
        h_and_ri(e, 0, T2, SR_V);
        h_and_ri(e, 0, T0, SR_Z);
        h_or_rr(e, 0, T0, T2);
        break;
    }
    /* the codes below 8 are true when the bits are clear */
    h_cmp_ri(e, 0, T0, 0);
    h_setcc(e, cc < 8 ? HC_EQ : HC_NE, T0);
    h_test_rr(e, 0, T0, T0);
}

/* the block's end at a control instruction, the PC to be target: the DO
 * loop's end if that is LA+1 (through the interpreter), else straight
 * out. cycles: the instruction's. The accumulators must be stored. */
static void emit_exit_to(Tr *t, const Insn *in, uint32_t target, uint32_t cycles, bool after_effects)
{
    Emitter *e = &t->e;
    size_t j;
    Stub *s;

    h_cmp_mem32_imm(e, REG_CORE, REGOFF(DSP_REG_LA), (int32_t)target - 1);
    j = h_jcc(e, HC_EQ);
    if (after_effects) {
        /* the instruction's work is done: dsp56k_post for the rest */
        s = stub_new(t, STUB_CTRL_POST, j);
        s->imm = target;
    } else {
        /* nothing done yet: the interpreter does the instruction */
        s = stub_new(t, STUB_FALLBACK, j);
    }
    stub_set_insn(t, s, in, cycles, t->cyc);
    emit_chain_to(t, target, t->cyc + cycles);
}

/* after a push or pop: a stack error raised ends the instruction
 * through dsp56k_post (the interrupt) */
static void emit_stack_check(Tr *t, const Insn *in, uint32_t target, uint32_t cycles)
{
    Emitter *e = &t->e;
    size_t j;
    Stub *s;

    h_cmp_mem16_imm(e, REG_CORE, OFF(interrupt_counter), 0);
    j = h_jcc(e, HC_NE);
    s = stub_new(t, STUB_CTRL_POST, j);
    s->imm = target;
    stub_set_insn(t, s, in, cycles, t->cyc);
}

/* the control instructions with native code; false: the interpreter's */
static bool emit_control(Tr *t, const Insn *in)
{
    Emitter *e = &t->e;
    const char *name = in->d.name;
    uint32_t inst = in->inst;
    uint32_t at = in->at;

    if (!name) {
        return false;
    }

    if (!strcmp(name, "bcc xxxx") || !strcmp(name, "bcc xxx") || !strcmp(name, "bra xxxx")) {
        bool lng = name[3] == ' ' && name[6] == 'x' ? true : !strcmp(name, "bcc xxxx");
        uint32_t target;
        int cc;
        if (!strcmp(name, "bcc xxx")) {
            uint32_t xxx = (inst & 0x1F) + ((inst & (0xF << 6)) >> 1);
            target = (at + ((int32_t)(xxx << 23) >> 23)) & 0xFFFFFF;
            cc = (inst >> 12) & 0xF;
            lng = false;
        } else {
            target = (at + in->ext) & 0xFFFFFF;
            cc = name[1] == 'r' ? -1 : inst & 0xF;
        }
        (void)lng;
        acc_spill_all(t, false);
        if (cc >= 0) {
            size_t j;
            emit_cc(e, cc);
            j = h_jcc(e, HC_NE);
            emit_exit_to(t, in, in->next, 2, false);
            h_patch(e, j, e->len);
        }
        emit_exit_to(t, in, target, 2, false);
        return true;
    }

    if (!strcmp(name, "brclr #n, S, xxxx") || !strcmp(name, "brset #n, S, xxxx")) {
        int numreg = (inst >> 8) & 0x3F;
        int numbit = inst & 0x1F;
        uint32_t target = (at + in->ext) & 0xFFFFFF;
        size_t j;
        if (!reg_supported(numreg)) {
            return false;
        }
        emit_read_reg(t, numreg, V1);
        acc_spill_all(t, false);
        h_test_ri(e, 0, V1, 1u << numbit);
        j = h_jcc(e, name[2] == 'c' ? HC_EQ : HC_NE);
        emit_exit_to(t, in, in->next, 6, false);
        h_patch(e, j, e->len);
        emit_exit_to(t, in, target, 6, false);
        return true;
    }

    if (!strcmp(name, "brclr #n, [X or Y]:pp, xxxx") || !strcmp(name, "brset #n, [X or Y]:pp, xxxx")) {
        int memspace = (inst >> 6) & 1;
        uint32_t addr = 0xFFFFC0 + ((inst >> 8) & 0x3F);
        int numbit = inst & 0x1F;
        uint32_t target = (at + in->ext) & 0xFFFFFF;
        size_t j;
        acc_spill_all(t, false);
        h_mov_ri(e, EA1, addr);
        emit_read_mem(t, memspace, EA1, V1);
        h_test_ri(e, 0, V1, 1u << numbit);
        j = h_jcc(e, name[2] == 'c' ? HC_EQ : HC_NE);
        /* the read done: the interpreter's way would read again, but a
         * peripheral read has no side effects (dsp.c) */
        emit_exit_to(t, in, in->next, 6, false);
        h_patch(e, j, e->len);
        emit_exit_to(t, in, target, 6, false);
        return true;
    }

    if (!strcmp(name, "bsr xxxx") || !strcmp(name, "bsr xxx")) {
        uint32_t target, cycles;
        if (name[4] == 'x' && name[7] == 'x') {
            target = (at + in->ext) & 0xFFFFFF;
            cycles = 6;
        } else {
            uint32_t xxx = (inst & 0x1F) + ((inst & (0xF << 6)) >> 1);
            target = (at + ((int32_t)(xxx << 23) >> 23)) & 0xFFFFFF;
            cycles = 4;
        }
        acc_spill_all(t, false);
        /* the loop's end at the target: the interpreter's */
        h_cmp_mem32_imm(e, REG_CORE, REGOFF(DSP_REG_LA), (int32_t)target - 1);
        stub_set_insn(t, stub_new(t, STUB_FALLBACK, h_jcc(e, HC_EQ)), in, cycles, t->cyc);
        h_mov_rr(e, 1, H_ARG0, REG_CORE);
        h_mov_ri(e, H_ARG1, in->next);
        h_ld32(e, H_ARG2, REG_CORE, REGOFF(DSP_REG_SR));
        h_call(e, (const void *)dsp56k_stack_push);
        emit_stack_check(t, in, target, cycles);
        emit_chain_to(t, target, t->cyc + cycles);
        return true;
    }

    if (!strcmp(name, "rts")) {
        acc_spill_all(t, false);
        h_mov_rr(e, 1, H_ARG0, REG_CORE);
        h_call(e, (const void *)dsp56k_stack_pop);
        h_st32(e, REG_CORE, OFF(pc), T0);
        /* the stack error, or the loop's end at the new PC: the rest
         * through dsp56k_post; its PC is set */
        {
            size_t j, j2;
            Stub *s;
            h_cmp_mem16_imm(e, REG_CORE, OFF(interrupt_counter), 0);
            j = h_jcc(e, HC_NE);
            h_sub_ri(e, 0, T0, 1);
            h_cmp_r_mem32(e, T0, REG_CORE, REGOFF(DSP_REG_LA));
            j2 = h_jcc(e, HC_EQ);
            s = stub_new(t, STUB_CTRL_POST, j);
            s->imm = 0xFFFFFFFF;    /* the PC as set */
            stub_set_insn(t, s, in, 4, t->cyc);
            s = stub_new(t, STUB_CTRL_POST, j2);
            s->imm = 0xFFFFFFFF;
            stub_set_insn(t, s, in, 4, t->cyc);
        }
        emit_chain(t, t->cyc + 4);
        return true;
    }

    if (!strcmp(name, "dor #xxx, label")) {
        uint32_t la = (at + in->ext) & 0xFFFF;
        uint32_t lc = ((inst >> 8) & 0xFF) + ((inst & 0xF) << 8);
        if (in->next == la + 1) {
            /* the loop's end right after: the interpreter's */
            return false;
        }
        acc_spill_all(t, false);
        h_mov_rr(e, 1, H_ARG0, REG_CORE);
        h_ld32(e, H_ARG1, REG_CORE, REGOFF(DSP_REG_LA));
        h_ld32(e, H_ARG2, REG_CORE, REGOFF(DSP_REG_LC));
        h_call(e, (const void *)dsp56k_stack_push);
        h_st32_imm(e, REG_CORE, REGOFF(DSP_REG_LA), la);
        h_mov_rr(e, 1, H_ARG0, REG_CORE);
        h_mov_ri(e, H_ARG1, in->next);
        h_ld32(e, H_ARG2, REG_CORE, REGOFF(DSP_REG_SR));
        h_call(e, (const void *)dsp56k_stack_push);
        h_or_mem32_imm(e, REG_CORE, REGOFF(DSP_REG_SR), 1 << DSP_SR_LF);
        h_st32_imm(e, REG_CORE, REGOFF(DSP_REG_LC), lc);
        emit_stack_check(t, in, in->next, 6);
        emit_chain_to(t, in->next, t->cyc + 6);
        return true;
    }

    if (!strcmp(name, "rep #xxx")) {
        uint32_t lc = ((inst >> 8) & 0xFF) + ((inst & 0xF) << 8);
        acc_spill_all(t, false);
        /* the loop's end right after the REP: the interpreter's */
        h_cmp_mem32_imm(e, REG_CORE, REGOFF(DSP_REG_LA), (int32_t)in->next - 1);
        stub_set_insn(t, stub_new(t, STUB_FALLBACK, h_jcc(e, HC_EQ)), in, 4, t->cyc);
        /* emu_rep_imm, then dsp_postexecute_update_pc's first pass over
         * the REP (LC 0 counts 65536; the REP's instruction next) */
        h_ld32(e, T0, REG_CORE, REGOFF(DSP_REG_LC));
        h_st32(e, REG_CORE, REGOFF(DSP_REG_LCSAVE), T0);
        h_st32_imm(e, REG_CORE, REGOFF(DSP_REG_LC), lc ? lc : 0x10000);
        h_st32_imm(e, REG_CORE, OFF(loop_rep), 1);
        h_st32_imm(e, REG_CORE, OFF(pc_on_rep), 0);
        h_st32_imm(e, REG_CORE, OFF(pc), in->next);
        emit_charge(e, t->cyc + 4);
        emit_jump_to_epilogue(t);
        return true;
    }

    return false;
}


/* after a native instruction: the DO loop's end, when the PC reached
 * LA+1 inside one; and, after one that wrote X, the halt a write to the
 * peripherals may have asked for (the interpreter stops right after it) */
static void emit_insn_checks(Tr *t, const Insn *in, uint32_t cycles)
{
    Emitter *e = &t->e;
    size_t j;

    if (t->wrote_x) {
        h_cmp_mem8_imm(e, REG_CORE, OFF(is_idle), 0);
        j = h_jcc(e, HC_NE);
        stub_set_insn(t, stub_new(t, STUB_HALT, j), in, cycles, t->cyc - cycles);
        t->wrote_x = false;
    }
    h_cmp_mem32_imm(e, REG_CORE, REGOFF(DSP_REG_LA), in->next - 1);
    j = h_jcc(e, HC_EQ);
    stub_set_insn(t, stub_new(t, STUB_LOOPEND, j), in, cycles, t->cyc - cycles);
}

static void emit_stubs(Tr *t)
{
    Emitter *e = &t->e;

    for (int i = 0; i < t->nstubs; i++) {
        Stub *s = &t->stubs[i];

        h_patch(e, s->jump, e->len);
        switch (s->kind) {
        case STUB_LOOPEND: {
            /* in a DO loop? (LF) */
            size_t j, j2;
            h_test_mem8_imm(e, REG_CORE, REGOFF(DSP_REG_SR) + 1, 0x80);
            h_jcc_to(e, HC_EQ, s->back);
            emit_flags_flush_stub(t, s);
            for (int n = 0; n < 2; n++) {
                if (s->dirty[n]) {
                    emit_acc_store(e, n);
                }
            }
            /* dsp_postexecute_update_pc: one pass fewer; the loop again
             * from its start (SSH), or, after the last, ended */
            h_ld32(e, T0, REG_CORE, REGOFF(DSP_REG_LC));
            h_sub_ri(e, 0, T0, 1);
            h_and_ri(e, 0, T0, 0xFFFF);
            h_st32(e, REG_CORE, REGOFF(DSP_REG_LC), T0);
            j = h_jcc(e, HC_EQ);
            h_ld32(e, T0, REG_CORE, REGOFF(DSP_REG_SSH));
            h_st32(e, REG_CORE, OFF(pc), T0);
            emit_chain(t, s->cyc_before + s->cycles);
            h_patch(e, j, e->len);
            h_mov_rr(e, 1, H_ARG0, REG_CORE);
            h_call(e, (const void *)dsp56k_do_end);
            h_st32_imm(e, REG_CORE, OFF(pc), s->next);
            /* a stack error: its interrupt */
            h_cmp_mem16_imm(e, REG_CORE, OFF(interrupt_counter), 0);
            j2 = h_jcc(e, HC_EQ);
            h_mov_rr(e, 1, H_ARG0, REG_CORE);
            h_call(e, (const void *)dsp56k_post_interrupts);
            h_patch(e, j2, e->len);
            /* out: the SR restored may be another scaling mode's */
            emit_charge(e, s->cyc_before + s->cycles);
            emit_jump_to_epilogue(t);
            continue;
        }

        case STUB_REP_POST:
            emit_flags_flush_stub(t, s);
            for (int n = 0; n < 2; n++) {
                if (s->dirty[n]) {
                    emit_acc_store(e, n);
                }
            }
            h_st32(e, REG_CORE, REGOFF(DSP_REG_LC), REG_LC);
            emit_rep_charge(e, s->cycles, 0);
            h_st32_imm(e, REG_CORE, OFF(pc), s->at);
            h_st32_imm(e, REG_CORE, OFF(cur_inst_len), s->len);
            h_st16_imm(e, REG_CORE, OFF(instr_cycle), s->cycles);
            h_mov_rr(e, 1, H_ARG0, REG_CORE);
            h_call(e, (const void *)dsp56k_post);
            emit_jump_to_epilogue(t);
            continue;

        case STUB_FALLBACK:
            /* the accumulators are stored before a control instruction */
            emit_fallback_body(e, &s->in, s->cyc_before);
            emit_jump_to_epilogue(t);
            continue;

        case STUB_CTRL_POST:
            if (s->imm != 0xFFFFFFFF) {
                h_st32_imm(e, REG_CORE, OFF(pc), s->imm);
            }
            h_st32_imm(e, REG_CORE, OFF(cur_inst_len), 0);
            h_st16_imm(e, REG_CORE, OFF(instr_cycle), s->cycles);
            h_mov_rr(e, 1, H_ARG0, REG_CORE);
            h_call(e, (const void *)dsp56k_post);
            emit_charge(e, s->cyc_before);
            emit_jump_to_epilogue(t);
            continue;

        case STUB_HALT:
            emit_flags_flush_stub(t, s);
            for (int n = 0; n < 2; n++) {
                if (s->dirty[n]) {
                    emit_acc_store(e, n);
                }
            }
            h_st32_imm(e, REG_CORE, OFF(pc), s->next);
            emit_charge(e, s->cyc_before + s->cycles);
            emit_jump_to_epilogue(t);
            continue;

        case STUB_READ_X: {
            size_t j;
            /* the mixbuffer in line, the rest through the interpreter */
            h_lea(e, 0, T0, s->reg, -DSP_MIXBUFFER_BASE);
            h_cmp_ri(e, 0, T0, DSP_MIXBUFFER_SIZE);
            j = h_jcc(e, HC_AE);
            h_ld32_idx4(e, s->reg2, REG_CORE, T0, OFF(mixbuffer));
            h_jmp_to(e, s->back);
            h_patch(e, j, e->len);
            emit_stub_call_begin(e);
            h_mov_rr(e, 0, H_ARG1, s->reg);
            h_call(e, (const void *)jit_read_x);
            emit_stub_call_end(e);
            h_mov_rr(e, 0, s->reg2, T0);
            break;
        }
        case STUB_WRITE_X: {
            size_t j;
            h_lea(e, 0, T0, s->reg, -DSP_MIXBUFFER_BASE);
            h_cmp_ri(e, 0, T0, DSP_MIXBUFFER_SIZE);
            j = h_jcc(e, HC_AE);
            h_st32_idx4(e, REG_CORE, T0, OFF(mixbuffer), s->reg2);
            h_jmp_to(e, s->back);
            h_patch(e, j, e->len);
            emit_stub_call_begin(e);
            /* the value first: H_ARG1 may be the address register on Win64 */
            h_mov_rr(e, 0, H_ARG2, s->reg2);
            h_mov_rr(e, 0, H_ARG1, s->reg);
            h_call(e, (const void *)jit_write_x);
            emit_stub_call_end(e);
            break;
        }
        case STUB_READ_Y:
            emit_stub_call_begin(e);
            h_mov_rr(e, 0, H_ARG1, s->reg);
            h_call(e, (const void *)jit_read_y);
            emit_stub_call_end(e);
            h_mov_rr(e, 0, s->reg2, T0);
            break;
        case STUB_WRITE_Y:
            emit_stub_call_begin(e);
            h_mov_rr(e, 0, H_ARG2, s->reg2);
            h_mov_rr(e, 0, H_ARG1, s->reg);
            h_call(e, (const void *)jit_write_y);
            emit_stub_call_end(e);
            break;

        case STUB_RN_UPDATE:
            emit_stub_call_begin(e);
            switch (s->modkind) {
            case MOD_IMM:
                h_mov_ri(e, H_ARG2, (uint32_t)s->imm);
                break;
            case MOD_PLUS_N:
                h_ld32(e, H_ARG2, REG_CORE, REGOFF(DSP_REG_N0 + s->n));
                break;
            case MOD_MINUS_N:
                h_ld32(e, H_ARG2, REG_CORE, REGOFF(DSP_REG_N0 + s->n));
                h_neg(e, 0, H_ARG2);
                break;
            }
            h_mov_ri(e, H_ARG1, s->n);
            h_call(e, (const void *)dsp56k_update_rn);
            emit_stub_call_end(e);
            break;

        case STUB_EA_RN_PLUS_N:
            emit_stub_call_begin(e);
            h_mov_ri(e, H_ARG1, s->n);
            h_call(e, (const void *)dsp56k_ea_rn_plus_n);
            emit_stub_call_end(e);
            h_mov_rr(e, 0, s->reg, T0);
            break;

        case STUB_LIMIT: {
            /* the limit of the accumulator's sign; L set */
            size_t j;
            h_mov_ri(e, s->reg2, 0x7FFFFF);
            h_test_rr(e, 1, s->reg, s->reg);
            j = h_jcc(e, HC_NS);
            h_mov_ri(e, s->reg2, 0x800000);
            h_patch(e, j, e->len);
            h_or_mem32_imm(e, REG_CORE, REGOFF(DSP_REG_SR), SR_L);
            break;
        }
        }
        h_jmp_to(e, s->back);
    }
}

static void decode_insn(dsp_core_t *core, uint32_t at, Insn *in)
{
    dsp56k_decode(core, at, &in->d);
    in->at = at;
    in->inst = in->d.inst;
    in->len = in->d.len;
    in->ext = at + 1 < DSP_PRAM_SIZE ? core->pram[at + 1] & 0xFFFFFF : 0;
    in->next = (at + in->len) & (DSP_PRAM_SIZE - 1);
}

/* whether the translator makes native code for the instruction */
static bool insn_native(const Insn *in)
{
    if (in->d.control) {
        return false;
    }
    if (in->inst >= 0x100000) {
        return pm_supported(in);
    }
    return np_kind(in) != NP_NONE;
}

/* the native code for an instruction insn_native says yes to, without
 * the checks after it; its cycles added to t->cyc */
static void emit_native(Tr *t, const Insn *in)
{
    t->cyc += 2;
    if (in->inst >= 0x100000) {
        emit_parallel(t, in);
    } else {
        emit_nonparallel(t, in, np_kind(in));
    }
}

/* The way into and out of the blocks' code, at the start of the cache:
 * enter(core, fn) saves the host registers the code uses, puts the
 * state pointer in place (and the Win64 shadow space for the calls) and
 * jumps to the block; a block ends with a jump to the next one, or to
 * leave, which restores the registers and returns to enter's caller. */
static void emit_trampolines(DspJit *jit)
{
    Emitter e;

    emit_init(&e, jit->cache, jit->cache_len);
    jit->enter = (dspjit_enter_fn)e.buf;
    h_enter(&e);
    jit->leave = e.buf + e.len;
    h_leave(&e);

    dspjit_code_written(e.buf, e.len);
    jit->cache_base = (e.len + 15) & ~(size_t)15;
    jit->cache_used = jit->cache_base;
}

static void translate_begin(DspJit *jit, int mode)
{
    Tr *t = jit->tr;
    Emitter *e = &t->e;

    memset(t, 0, offsetof(Tr, stubs));
    t->nstubs = 0;
    t->core = jit->core;
    t->mode = mode;
    t->lazy_acc = -1;
    t->leave = jit->leave;
    t->table = jit->blocks[mode];
    emit_init(e, jit->cache + jit->cache_used, jit->cache_len - jit->cache_used);
}

/* the stubs; the block's function, or NULL when it did not fit (the
 * caller empties the cache and tries again) */
static dspjit_block_fn translate_end(DspJit *jit)
{
    Tr *t = jit->tr;
    Emitter *e = &t->e;

    emit_stubs(t);

    if (e->overflow) {
        return NULL;
    }
    dspjit_code_written(e->buf, e->len);
    /* 16-byte aligned starts */
    jit->cache_used += (e->len + 15) & ~(size_t)15;
    return (dspjit_block_fn)e->buf;
}

/* The block at pc, for the scaling mode */
static dspjit_block_fn dspjit_translate(DspJit *jit, int mode, uint32_t pc)
{
    dsp_core_t *core = jit->core;
    Tr *t = jit->tr;
    uint32_t at = pc;

    translate_begin(jit, mode);

    for (int n = 0; n < DSPJIT_BLOCK_MAX_INSNS; n++) {
        Insn in;
        uint32_t cyc_before;
        bool done = false;

        if (at >= DSP_PRAM_SIZE) {
            break;
        }
        decode_insn(core, at, &in);
        cyc_before = t->cyc;
        t->end = at + in.len;

        if (insn_native(&in)) {
            emit_native(t, &in);
            emit_insn_checks(t, &in, t->cyc - cyc_before);
        } else if (in.d.control && emit_control(t, &in)) {
            t->cyc = 0;
            done = true;
        } else {
            emit_fallback(t, &in);
            done = true;
        }

        at = in.next;
        if (done) {
            break;
        }
        if (n == DSPJIT_BLOCK_MAX_INSNS - 1 || at == 0) {
            /* the block's end: the next word */
            acc_spill_all(t, false);
            emit_chain_to(t, at, t->cyc);
            t->cyc = 0;
        }
    }
    if (t->cyc) {
        /* ran out of P: the next word */
        acc_spill_all(t, false);
        emit_chain_to(t, at & (DSP_PRAM_SIZE - 1), t->cyc);
    }

    return translate_end(jit);
}

/* a REP's repeats done so far (the count at the start less the count
 * left in REG_LC), less `less` of them, charged to the cycle counters
 * (not the budget: the loop does that as it goes); uses rax */
static void emit_rep_charge(Emitter *e, uint32_t cycles, int less)
{
    h_frame_ld32(e, T0, SLOT_REP_START);
    h_sub_rr(e, 0, T0, REG_LC);
    if (less) {
        h_sub_ri(e, 0, T0, less);
    }
    h_mul_rri(e, 0, T0, T0, cycles);
    h_add_mem32_r(e, REG_CORE, OFF(cycle_count), T0);
    h_add_mem32_r(e, REG_CORE, OFF(num_inst), T0);
}

/* The instruction at pc as a REP repeats it: the DSP is in the REP
 * (loop_rep, LC counting the repeats left). The code repeats it until LC
 * reaches 0 (the interpreter's dsp_postexecute_update_pc: then the REP
 * ends, LC is LCSAVE's again and the PC moves on, through dsp56k_post as
 * for any instruction), the cycle budget is spent, or the DSP halts.
 * NULL for an instruction the translator has no native code for: the
 * interpreter repeats it. */
static dspjit_block_fn dspjit_translate_rep(DspJit *jit, int mode, uint32_t pc)
{
    dsp_core_t *core = jit->core;
    Tr *t = jit->tr;
    Emitter *e = &t->e;
    Insn in;
    uint32_t cycles;
    size_t top, j_end;

    if (pc >= DSP_PRAM_SIZE) {
        return NULL;
    }
    decode_insn(core, pc, &in);
    if (!insn_native(&in)) {
        return NULL;
    }

    translate_begin(jit, mode);
    /* both accumulators in their registers for the whole loop, and LC;
     * its count at the start, for the cycles of the repeats done */
    acc_load(t, 0);
    acc_load(t, 1);
    h_ld32(e, REG_LC, REG_CORE, REGOFF(DSP_REG_LC));
    h_frame_st32(e, SLOT_REP_START, REG_LC);

    top = e->len;
    emit_native(t, &in);
    cycles = t->cyc;
    t->cyc = 0;
    if (t->wrote_x) {
        /* halted: this repeat ends the interpreter's way, then out */
        size_t j;
        h_cmp_mem8_imm(e, REG_CORE, OFF(is_idle), 0);
        j = h_jcc(e, HC_NE);
        stub_set_insn(t, stub_new(t, STUB_REP_POST, j), &in, cycles, 0);
        t->wrote_x = false;
    }
    /* one repeat fewer; another while the budget lasts */
    h_sub_ri(e, 0, REG_LC, 1);
    h_and_ri(e, 0, REG_LC, 0xFFFF);
    j_end = h_jcc(e, HC_EQ);
    h_sub_mem32_imm(e, REG_CORE, OFF(jit_cycles), cycles);
    h_jcc_to(e, HC_G, top);
    /* the budget spent: out, the REP still on (the other path below
     * starts from the same state) */
    {
        bool loaded[2], dirty[2];
        int lazy = t->lazy_acc;
        memcpy(loaded, t->loaded, sizeof(loaded));
        memcpy(dirty, t->dirty, sizeof(dirty));
        acc_spill_all(t, true);
        h_st32(e, REG_CORE, REGOFF(DSP_REG_LC), REG_LC);
        emit_rep_charge(e, cycles, 0);
        emit_jump_to_epilogue(t);
        memcpy(t->loaded, loaded, sizeof(loaded));
        memcpy(t->dirty, dirty, sizeof(dirty));
        t->lazy_acc = lazy;
    }

    /* the last repeat: the REP ends; the PC, the loop, the interrupts and
     * this repeat's cycles through dsp56k_post, as for any instruction */
    h_patch(e, j_end, e->len);
    acc_spill_all(t, false);
    emit_rep_charge(e, cycles, 1);
    h_st32_imm(e, REG_CORE, OFF(loop_rep), 0);
    h_ld32(e, T0, REG_CORE, REGOFF(DSP_REG_LCSAVE));
    h_st32(e, REG_CORE, REGOFF(DSP_REG_LC), T0);
    h_st32_imm(e, REG_CORE, OFF(pc), in.at);
    h_st32_imm(e, REG_CORE, OFF(cur_inst_len), in.len);
    h_st16_imm(e, REG_CORE, OFF(instr_cycle), cycles);
    h_mov_rr(e, 1, H_ARG0, REG_CORE);
    h_call(e, (const void *)dsp56k_post);
    emit_jump_to_epilogue(t);

    return translate_end(jit);
}
#endif

DspJit *dspjit_create(dsp_core_t *core)
{
    DspJit *jit;

    if (!DSPJIT_HOST) {
        return NULL;
    }
    jit = g_new0(DspJit, 1);
    jit->core = core;
    jit->cache_len = DSPJIT_CACHE_BYTES;
    jit->cache = dspjit_map_exec(jit->cache_len);
    if (!jit->cache) {
        g_free(jit);
        return NULL;
    }
#if DSPJIT_HOST
    jit->tr = g_new0(Tr, 1);
    emit_trampolines(jit);
#endif
    return jit;
}

void dspjit_destroy(DspJit *jit)
{
    if (!jit) {
        return;
    }
    dspjit_unmap_exec(jit->cache, jit->cache_len);
    g_free(jit->tr);
    g_free(jit);
}

void dspjit_flush(DspJit *jit)
{
    jit->cache_used = jit->cache_base;
    memset(jit->blocks, 0, sizeof(jit->blocks));
    memset(jit->rep_blocks, 0, sizeof(jit->rep_blocks));
}

void dspjit_invalidate(DspJit *jit, uint32_t address)
{
    uint32_t from = address >= DSPJIT_BLOCK_MAX_WORDS ? address - DSPJIT_BLOCK_MAX_WORDS + 1 : 0;

    if (address >= DSP_PRAM_SIZE) {
        return;
    }
    for (int m = 0; m < 4; m++) {
        /* the blocks that start before the word and reach it */
        for (uint32_t pc = from; pc <= address; pc++) {
            if (jit->blocks[m][pc] && jit->block_end[m][pc] > address) {
                jit->blocks[m][pc] = NULL;
            }
        }
        jit->rep_blocks[m][address] = NULL;
        if (address > 0) {
            jit->rep_blocks[m][address - 1] = NULL;
        }
    }
}

/* a repeat of an instruction the translator has no code for */
static void rep_step(dsp_core_t *core)
{
    dsp56k_execute_instruction(core);
    core->cycle_count += core->instr_cycle;
    core->jit_cycles -= core->instr_cycle;
}

void dspjit_run(DspJit *jit, int cycles)
{
    dsp_core_t *core = jit->core;

    core->jit_cycles = cycles;
    while (core->jit_cycles > 0 && !core->is_idle) {
        uint32_t pc = core->pc & (DSP_PRAM_SIZE - 1);
        dspjit_block_fn fn;

        int mode = (core->registers[DSP_REG_SR] >> DSP_SR_S0) & 3;

        /* what the blocks' code is not made for: an interrupt in its
         * pipeline, a REP's own instruction: the interpreter's step */
        if (core->interrupt_state != DSP_INTERRUPT_NONE ||
            core->interrupt_counter != 0 || core->pc_on_rep) {
            dsp56k_execute_instruction(core);
            core->cycle_count += core->instr_cycle;
            core->jit_cycles -= core->instr_cycle;
            continue;
        }
        if (core->loop_rep) {
            /* in a REP: the instruction's repeating code */
            fn = jit->rep_blocks[mode][pc];
            if (!fn) {
#if DSPJIT_HOST
                fn = dspjit_translate_rep(jit, mode, pc);
                if (!fn && jit->tr->e.overflow) {
                    dspjit_flush(jit);
                    fn = dspjit_translate_rep(jit, mode, pc);
                }
#endif
                if (!fn) {
                    fn = rep_step;
                }
                jit->rep_blocks[mode][pc] = fn;
            }
            if (fn == rep_step) {
                rep_step(core);
            } else {
                jit->enter(core, fn);
            }
            continue;
        }
        fn = jit->blocks[mode][pc];
        if (!fn) {
#if DSPJIT_HOST
            fn = dspjit_translate(jit, mode, pc);
            if (!fn) {
                dspjit_flush(jit);
                fn = dspjit_translate(jit, mode, pc);
            }
            jit->blocks[mode][pc] = fn;
            jit->block_end[mode][pc] = jit->tr->end;
#endif
            if (!fn) {
                /* a block too big for an empty cache cannot happen; the
                 * interpreter's step keeps the DSP going if it does */
                dsp56k_execute_instruction(core);
                core->cycle_count += core->instr_cycle;
                core->jit_cycles -= core->instr_cycle;
                continue;
            }
        }
        jit->enter(core, fn);
    }
}
