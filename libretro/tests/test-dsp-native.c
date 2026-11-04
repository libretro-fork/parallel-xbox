/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reuse the full DSP machine/state oracle; no game code or BIOS is needed. */
int dspdiff_original_main(int argc, char **argv);
#define main dspdiff_original_main
#include "dspdiff.c"
#undef main
#include "hw/xbox/mcpx/apu/dsp/jit/dspjit.h"

static unsigned cases;

static void prepare(Machine *m, uint32_t op, uint32_t ext, unsigned seed,
                    unsigned mode, bool long_irq)
{
    DSPState *dsp = m->dsp;
    dsp_core_t *c = dsp->backend;
    dsp->ops->reset(dsp);
    dsp->interrupts = 0x82;
    rng_state = seed + 1;
    for (int r = DSP_REG_X0; r <= DSP_REG_B1; r++) {
        c->registers[r] = rng() & ((r == DSP_REG_A2 || r == DSP_REG_B2) ? 0xFF : 0xFFFFFF);
    }
    for (int r = 0; r < 8; r++) {
        c->registers[DSP_REG_R0 + r] = 0x20 + 8 * r + (seed & 7);
        c->registers[DSP_REG_N0 + r] = 1 + (seed & 3);
        c->registers[DSP_REG_M0 + r] = seed & 8 ? 0x3F : 0xFFFF;
    }
    for (int i = 0; i < 256; i++) {
        c->xram[i] = rng();
        c->yram[i] = rng();
    }
    c->registers[DSP_REG_SR] = (rng() & 0xFF) | (mode << DSP_SR_S0);
    if (seed & 0x10000) {
        static const uint64_t accum[] = {
            0, 1, 0xFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFF,
            0x80000000000000, 0x7FFFFFFFFFFFFE,
            0x80000000000001, 0xFFFFFF00000000
        };
        static const uint64_t source[] = {
            0, 1, 0xFFFFFFFFFFFF, 0x7FFFFFFFFFFF,
            0x800000000000, 0x7FFFFFFFFFFE,
            0x800000000001, 0xFFFFFF000000
        };
        uint64_t v = accum[(seed >> 1) & 7];
        uint64_t s = source[(seed >> 4) & 7];
        for (unsigned d = 0; d < 2; d++) {
            c->registers[DSP_REG_A0 + d] = v & 0xFFFFFF;
            c->registers[DSP_REG_A1 + d] = (v >> 24) & 0xFFFFFF;
            c->registers[DSP_REG_A2 + d] = v >> 48;
            c->registers[DSP_REG_X0 + 2 * d] = s & 0xFFFFFF;
            c->registers[DSP_REG_X1 + 2 * d] = s >> 24;
        }
        if ((op & 7) == 7 || (op & 0xFF) == 0x1D) {
            static const uint32_t data[] = {
                0, 1, 0xFFFFFF, 0x7FFFFF, 0x800000, 0x7FFFFE,
                0x800001, 0xFFFF00
            };
            unsigned other = ((op >> 3) & 1) ^ 1;
            uint64_t other_v = accum[(seed >> 4) & 7];
            c->registers[DSP_REG_A0 + other] = other_v & 0xFFFFFF;
            c->registers[DSP_REG_A1 + other] = (other_v >> 24) & 0xFFFFFF;
            c->registers[DSP_REG_A2 + other] = other_v >> 48;
            if ((op & 0xF7) == 0x27 || (op & 0xF7) == 0x37) {
                /* Exercise the 24-bit rotated word independently of the
                 * low word and extension, which must remain unchanged. */
                c->registers[DSP_REG_A1 + (other ^ 1)] = data[(seed >> 4) & 7];
            }
            for (unsigned r = DSP_REG_X0; r <= DSP_REG_Y1; r++) {
                c->registers[r] = data[(seed >> 4) & 7];
            }
        }
        c->registers[DSP_REG_SR] =
            (c->registers[DSP_REG_SR] & ~(1 << DSP_SR_C)) | (seed & 1);
    }
    c->registers[DSP_REG_SP] = 2;
    if (seed & 0x20000) {
        c->registers[DSP_REG_SR] = (seed & 0xFF) | (mode << DSP_SR_S0);
    }
    c->registers[DSP_REG_SSH] = 0x20;
    c->registers[DSP_REG_SSL] = 0;
    memset(c->stack, 0, sizeof(c->stack));
    c->stack[0][2] = 0x20;
    if ((seed & 15) == 0) {
        c->registers[DSP_REG_SR] |= 1 << DSP_SR_LF;
        c->registers[DSP_REG_LA] = ext - 1;
        c->registers[DSP_REG_LC] = 2;
    }
    c->is_idle = false;
    c->interrupt_state = long_irq ? DSP_INTERRUPT_LONG : DSP_INTERRUPT_NONE;
    c->interrupt_pipeline_count = long_irq ? 3 : 0;
    c->interrupt_counter = 0;
    c->pc = 0;
    c->cycle_count = 0;
    dsp->ops->write_memory(dsp, 'P', 0, op);
    dsp->ops->write_memory(dsp, 'P', 1, ext);
    dsp->ops->write_memory(dsp, 'P', 2, 0);
    if (seed & 0x8000000) {
        /* Reset uses an invalid-word sentinel in untouched P RAM. Supply
         * architectural 24-bit program data for memory-read operands. */
        for (unsigned i = 3; i < DSP_PRAM_SIZE; i++) {
            c->pram[i] = (i * 0x010307u) & 0xFFFFFF;
        }
    }
    if (seed & 0x2000000) {
        for (unsigned r = 0; r < 4; r++) c->registers[r] = rng() & 0xFFFFFF;
    }
    if (seed & 0x4000000) {
        for (unsigned r = DSP_REG_R0; r <= DSP_REG_N7; r++)
            c->registers[r] = 0xFF0000 | (rng() & 0xFFFF);
    }
    if (seed & 0x200000) {
        /* Keep every signed seven-bit offset inside X/Y RAM. */
        for (unsigned r = 0; r < 8; r++)
            c->registers[DSP_REG_R0 + r] = 0x80 + 8 * r + (seed & 7);
    }
    if (seed & 0x1000000) {
        c->registers[DSP_REG_SR] |= 1u << DSP_SR_FV;
    }
    if (seed & 0x20000000) {
        c->registers[DSP_REG_SR] &= ~(1u << DSP_SR_LF);
    }
    if (seed & 0x10000000) {
        c->registers[DSP_REG_SR] |= 1u << DSP_SR_LF;
        c->registers[DSP_REG_LA] = ext - 1;
        c->registers[DSP_REG_LC] = seed & 1 ? 1 : 2;
    }
    if (seed & 0x40000) {
        DspDecoded decoded;
        dsp56k_decode(c, 0, &decoded);
        c->registers[DSP_REG_SR] |= 1 << DSP_SR_LF;
        c->registers[DSP_REG_LA] = decoded.len - 1;
        c->registers[DSP_REG_LC] = seed & 1 ? 1 : 2;
    }
    if (seed & 0x200000) {
        /* A zero extension otherwise seeds LA with UINT32_MAX for inactive
         * loops; raw control stores require an architectural 24-bit word. */
        c->registers[DSP_REG_LA] &= 0xFFFFFF;
    }
    if (seed & 0x80000) {
        /* Exercise the architectural stack-error interrupt, not the
         * interpreter's optional host-debug assertion. */
        c->exception_debugging = false;
        c->registers[DSP_REG_SP] = 15;
        c->stack[0][15] = c->registers[DSP_REG_SSH];
    }
    if (seed & 0x40000000) {
        c->exception_debugging = false;
        c->registers[DSP_REG_SP] = 14;
        c->stack[0][14] = c->registers[DSP_REG_SSH];
    }
    if (seed & 0x100000) {
        /* Explicit loop-count edges; special registers retain the stack,
         * status and loop fixtures above so aliases observe real setup. */
        static const uint32_t values[] = {
            0, 1, 2, 0xFFFF, 0x10000, 0xFFFFFF, 0x7FFFFF, 0x800000
        };
        unsigned reg = (op >> 8) & 63;
        uint32_t value = values[(seed >> 8) & 7];
        if (reg == DSP_REG_A || reg == DSP_REG_B) {
            unsigned d = reg & 1;
            c->registers[DSP_REG_A0 + d] = 0x800001;
            c->registers[DSP_REG_A1 + d] = value;
            c->registers[DSP_REG_A2 + d] = (seed & 0x800) ? 1 :
                (value & 0x800000 ? 0xFF : 0);
        } else if (reg >= DSP_REG_X0 && reg <= DSP_REG_M7) {
            c->registers[reg] = value &
                ((reg == DSP_REG_A2 || reg == DSP_REG_B2) ? 0xFF : 0xFFFFFF);
        }
    }

    if (seed & 0x400000) {
        c->exception_debugging = false;
        for (unsigned n = 0; n < 16; n++) {
            c->stack[0][n] = n ? 0x1200 + n : 0;
            c->stack[1][n] = n ? 0x5600 + n : 0;
        }
        unsigned sp = c->registers[DSP_REG_SP] & 15;
        c->registers[DSP_REG_SSH] = c->stack[0][sp];
        c->registers[DSP_REG_SSL] = c->stack[1][sp];
    }

    if (seed & 0x80000000u) {
        /* A valid STOP/FREEZE action for DMA-control peripheral writes. */
        unsigned reg = (op >> 8) & 63;
        unsigned value = 2 + ((seed >> 8) & 1);
        DspDecoded decoded;
        dsp56k_decode(c, 0, &decoded);
        if (!strcmp(decoded.name, "movep [X or Y]:pp <-> P:ea")) {
            /* The controlled DMA source is the P-memory extension address. */
            m->dsp->ops->write_memory(m->dsp, 'P', ext, value);
        } else {
            c->registers[reg] = value;
            if (reg == DSP_REG_SSH) c->stack[0][c->registers[DSP_REG_SP] & 15] = value;
            if (reg == DSP_REG_SP) {
                c->registers[DSP_REG_SSH] = c->stack[0][value];
                c->registers[DSP_REG_SSL] = c->stack[1][value];
            }
        }
    }

}

static void check(Machine *a, Machine *b, uint32_t op, uint32_t ext,
                  unsigned seed, unsigned mode, bool long_irq)
{
    prepare(a, op, ext, seed, mode, long_irq);
    prepare(b, op, ext, seed, mode, long_irq);
    dsp_core_t *ca = a->dsp->backend;
    uint64_t reads_before = peripheral_read_count;
    uint64_t writes_before = peripheral_write_count;
    peripheral_write_hash = 0;
    a->dsp->ops->step(a->dsp);
    uint64_t reads_a = peripheral_read_count - reads_before;
    ca->cycle_count += ca->instr_cycle;
    uint64_t writes_a = peripheral_write_count - writes_before;
    uint64_t hash_a = peripheral_write_hash;
    dspjit_test_reset_fallback_count();
    reads_before = peripheral_read_count;
    writes_before = peripheral_write_count;
    peripheral_write_hash = 0;
    b->dsp->ops->run(b->dsp, 1);
    uint64_t reads_b = peripheral_read_count - reads_before;
    uint64_t writes_b = peripheral_write_count - writes_before;
    if (dspjit_test_fallback_count() || reads_a != reads_b ||
        writes_a != writes_b || hash_a != peripheral_write_hash || compare_machines(a, b)) {
        fprintf(stderr, "native DSP case %u: op=%06x ext=%x seed=%u mode=%u irq=%d fallbacks=%llu\n",
                cases, op, ext, seed, mode, long_irq,
                (unsigned long long)dspjit_test_fallback_count());
        fprintf(stderr, "peripheral reads: interpreter=%llu jit=%llu\n",
                (unsigned long long)reads_a, (unsigned long long)reads_b);
        fprintf(stderr, "peripheral writes: interpreter=%llu jit=%llu hashes=%llx/%llx\n",
                (unsigned long long)writes_a, (unsigned long long)writes_b,
                (unsigned long long)hash_a, (unsigned long long)peripheral_write_hash);
        exit(1);
    }
    cases++;
}

/* Reuse the same translated PC across LF changes without resetting or
 * flushing the JIT. LA deliberately matches even when LF is clear. */
/* A warmed native JSR/RTS loop should enter generated code once per run,
 * while matching the interpreter at every varying budget boundary. */
static void check_parallel_discard_and_self_moves(Machine *a, Machine *b)
{
    static const unsigned alus[] = {0, 0x10, 0x18, 0x13, 0x1B, 0x40, 0x80, 0x88};
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned src = 0; src < 32; src++) {
            for (unsigned dst = 0; dst < 32; dst++) {
                if (src != dst && src >= 4 && dst >= 4) continue;
                for (unsigned alu = 0; alu < ARRAY_SIZE(alus); alu++) {
                    uint32_t inst = 0x200000 | (src << 13) | (dst << 8) | alus[alu];
                    for (unsigned seed = 0; seed < 4; seed++) {
                        unsigned flags = seed | 0x10000 | 0x2000000;
                        if ((inst & 0xFFE000) != 0x204000) flags |= 0x4000000;
                        check(a, b, inst, 0, flags, mode, false);
                    }
                }
            }
        }
    }
}

static void check_register_bits(Machine *a, Machine *b)
{
    static const uint32_t encodings[] = {0x0AC040, 0x0AC060, 0x0BC040, 0x0BC060};
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_M7; reg++) {
            for (unsigned op = 0; op < ARRAY_SIZE(encodings); op++) {
                unsigned bits = op == 3 ? 16 : 32;
                if ((reg == DSP_REG_A || reg == DSP_REG_B) && bits > 24) bits = 24;
                for (unsigned bit = 0; bit < bits; bit++) {
                    for (unsigned seed = 0; seed < 8; seed++) {
                        unsigned flags = seed | 0x1000000 |
                            (seed & 1 ? 0x40000 : 0) | (seed & 2 ? 0x10000 : 0);
                        check(a, b, encodings[op] | (reg << 8) | bit,
                              0, flags, mode, false);
                    }
                }
            }
        }
    }
}

static void check_control_register_bits(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t encodings[] = {0x0AC040, 0x0AC060, 0x0BC040, 0x0BC060};
    static const unsigned regs[] = {
        DSP_REG_SR, DSP_REG_SP, DSP_REG_SSH, DSP_REG_SSL, DSP_REG_LA, DSP_REG_LC
    };
    /* Independent SSH oracle: even an unchanged RMW pushes once; BTST
     * only observes the old value and must neither push nor pop. */
    for (unsigned op = 0; op < ARRAY_SIZE(encodings); op++) {
        for (unsigned old = 0; old < 2; old++) {
            uint32_t value = old << 5;
            uint32_t result = op == 0 ? 0 : op == 1 ? 0x20 : op == 2 ? value ^ 0x20 : value;
            prepare(b, encodings[op] | (DSP_REG_SSH << 8) | 5, 0, 1, mode, false);
            dsp_core_t *c = b->dsp->backend;
            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (1u << DSP_SR_FV) | 0xAA;
            c->registers[DSP_REG_SP] = 2;
            c->registers[DSP_REG_SSH] = c->stack[0][2] = value;
            c->stack[0][3] = 0x1234;
            c->stack[1][3] = 0x5678;
            dspjit_test_reset_fallback_count();
            b->dsp->ops->run(b->dsp, 1);
            assert(!dspjit_test_fallback_count());
            assert(c->pc == 1 && c->cycle_count == 4);
            assert(c->registers[DSP_REG_SP] == (op == 3 ? 2 : 3));
            assert(c->registers[DSP_REG_SSH] == result);
            assert(c->stack[0][2] == value);
            assert(c->stack[0][3] == (op == 3 ? 0x1234 : result));
            assert(c->stack[1][3] == 0x5678);
            assert(c->registers[DSP_REG_SSL] == (op == 3 ? 0 : 0x5678));
            assert(c->registers[DSP_REG_SR] == ((mode << DSP_SR_S0) | 0xAA | old));
            cases++;
        }
    }

    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned r = 0; r < ARRAY_SIZE(regs); r++) {
        for (unsigned op = 0; op < ARRAY_SIZE(encodings); op++) {
            for (unsigned bit = 0; bit < (op == 3 ? 16 : 32); bit++) {
                for (unsigned seed = 0; seed < 4; seed++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        check(a, b, encodings[op] | (regs[r] << 8) | bit, 0,
                              0x400000 | 0x1000000 | contexts[ctx] | seed,
                              mode, seed & 1);
                    }
                }
            }
        }
    }
}

#define Y_OFFSET_CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Y long-offset oracle failed at line %u\n", __LINE__); \
        exit(1); \
    } \
} while (0)
static void check_y_long_offset_moves(Machine *a, Machine *b, unsigned mode)
{
    /* Independent raw-store/stack oracle, including 24-bit address wrap.
     * Long offset SSH stores read the register directly and must not pop. */
    static const unsigned oracle_regs[] = {DSP_REG_SSH, DSP_REG_LA, DSP_REG_X0, DSP_REG_M0};
    for (unsigned r = 0; r < ARRAY_SIZE(oracle_regs); r++) {
        for (unsigned read = 0; read < 2; read++) {
            for (unsigned wrap = 0; wrap < 2; wrap++) {
                for (unsigned v = 0; v < 2; v++) {
                    unsigned reg = oracle_regs[r];
                    uint32_t value = v ? 0x800001 : 0x123456;
                    uint32_t ext = wrap ? 0x50 : 0xFFFFFF;
                    unsigned addr = wrap ? 0x40 : 0x7F;
                    uint32_t sr = (mode << DSP_SR_S0) | (1u << DSP_SR_FV) | 0xA4;
                    prepare(b, 0x0B7080 | (read << 6) | reg, ext, 1, mode, false);
                    dsp_core_t *c = b->dsp->backend;
                    c->registers[DSP_REG_SR] = sr;
                    c->registers[DSP_REG_R0] = wrap ? 0xFFFFF0 : 0x80;
                    c->registers[reg] = value;
                    c->stack[0][2] = c->registers[DSP_REG_SSH];
                    c->stack[0][3] = 0x1234;
                    c->stack[1][3] = 0x5678;
                    c->yram[addr] = value;
                    c->xram[addr] = value ^ 0x5A5A5A;
                    dspjit_test_reset_fallback_count();
                    b->dsp->ops->run(b->dsp, 1);
                    Y_OFFSET_CHECK(!dspjit_test_fallback_count());
                    Y_OFFSET_CHECK(c->pc == 2 && c->cycle_count == 2);
                    Y_OFFSET_CHECK(c->registers[DSP_REG_R0] == (wrap ? 0xFFFFF0 : 0x80));
                    Y_OFFSET_CHECK(c->registers[reg] == (read ? value & (reg == DSP_REG_X0 ? 0xffffff : 0xffff) : value));
                    Y_OFFSET_CHECK(c->registers[DSP_REG_SP] == (read && reg == DSP_REG_SSH ? 3 : 2));
                    Y_OFFSET_CHECK(c->stack[0][3] == (read && reg == DSP_REG_SSH ? value & 0xFFFF : 0x1234));
                    Y_OFFSET_CHECK(c->stack[1][3] == 0x5678);
                    Y_OFFSET_CHECK(c->yram[addr] == value && c->registers[DSP_REG_SR] == sr);
                    Y_OFFSET_CHECK(c->xram[addr] == (value ^ 0x5A5A5A));
                    cases++;
                }
            }
        }
    }
    static const uint32_t offsets[] = {0, 1, 0x20, 0x100, 0xFFFFFF, 0xFFFFE0};
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    /* Check general operands and the existing raw control-register cases.
     * Bit-call EA matchers must not steal any long-offset MOVE encoding. */
    for (unsigned reg = DSP_REG_X0; reg < DSP_REG_MAX; reg++) {
        if (reg > DSP_REG_M7 && reg < DSP_REG_LCSAVE) continue;
        for (unsigned n = 0; n < 8; n++) {
            for (unsigned read = 0; read < 2; read++) {
                for (unsigned off = 0; off < ARRAY_SIZE(offsets); off++) {
                    for (unsigned seed = 0; seed < 4; seed++) {
                        for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                            check(a, b, 0x0B7080 | (n << 8) | (read << 6) | reg,
                                  offsets[off], 0x200000 | 0x400000 | 0x2000000 |
                                  0x1000000 | contexts[ctx] | seed, mode, seed & 1);
                        }
                    }
                }
            }
        }
    }
}

#undef Y_OFFSET_CHECK

static void check_offset_control_moves(Machine *a, Machine *b, unsigned mode)
{
    /* Independent raw-store/stack oracle, including 24-bit address wrap.
     * Long offset SSH stores read the register directly and must not pop. */
    static const unsigned oracle_regs[] = {DSP_REG_SSH, DSP_REG_LA};
    for (unsigned r = 0; r < ARRAY_SIZE(oracle_regs); r++) {
        for (unsigned read = 0; read < 2; read++) {
            for (unsigned wrap = 0; wrap < 2; wrap++) {
                for (unsigned v = 0; v < 2; v++) {
                    unsigned reg = oracle_regs[r];
                    uint32_t value = v ? 0x800001 : 0x123456;
                    uint32_t ext = wrap ? 0x50 : 0xFFFFFF;
                    unsigned addr = wrap ? 0x40 : 0x7F;
                    uint32_t sr = (mode << DSP_SR_S0) | (1u << DSP_SR_FV) | 0xA4;
                    prepare(b, 0x0A7080 | (read << 6) | reg, ext, 1, mode, false);
                    dsp_core_t *c = b->dsp->backend;
                    c->registers[DSP_REG_SR] = sr;
                    c->registers[DSP_REG_R0] = wrap ? 0xFFFFF0 : 0x80;
                    c->registers[reg] = value;
                    c->stack[0][2] = c->registers[DSP_REG_SSH];
                    c->stack[0][3] = 0x1234;
                    c->stack[1][3] = 0x5678;
                    c->xram[addr] = value;
                    dspjit_test_reset_fallback_count();
                    b->dsp->ops->run(b->dsp, 1);
                    assert(!dspjit_test_fallback_count());
                    assert(c->pc == 2 && c->cycle_count == 2);
                    assert(c->registers[DSP_REG_R0] == (wrap ? 0xFFFFF0 : 0x80));
                    assert(c->registers[reg] == (read ? value & 0xFFFF : value));
                    assert(c->registers[DSP_REG_SP] == (read && r == 0 ? 3 : 2));
                    assert(c->stack[0][3] == (read && r == 0 ? value & 0xFFFF : 0x1234));
                    assert(c->stack[1][3] == 0x5678);
                    assert(c->xram[addr] == value && c->registers[DSP_REG_SR] == sr);
                    cases++;
                }
            }
        }
    }
    static const uint32_t offsets[] = {0, 1, 0x20, 0x100, 0xFFFFFF, 0xFFFFE0};
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    /* Lower register fields overlap earlier bit-branch encodings. These
     * upper fields actually decode as the long-offset MOVE instruction. */
    for (unsigned reg = DSP_REG_LCSAVE; reg < DSP_REG_MAX; reg++) {
        for (unsigned n = 0; n < 8; n++) {
            for (unsigned read = 0; read < 2; read++) {
                for (unsigned off = 0; off < ARRAY_SIZE(offsets); off++) {
                    for (unsigned seed = 0; seed < 4; seed++) {
                        for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                            check(a, b, 0x0A7080 | (n << 8) | (read << 6) | reg,
                                  offsets[off], 0x200000 | 0x400000 | 0x2000000 |
                                  0x1000000 | contexts[ctx] | seed, mode, seed & 1);
                        }
                    }
                }
            }
        }
    }
    for (unsigned reg = 0; reg < DSP_REG_X0; reg++) {
        for (unsigned n = 0; n < 8; n++) {
            for (unsigned read = 0; read < 2; read++) {
                for (unsigned space = 0; space < 2; space++) {
                    for (unsigned off = 0; off < 128; off++) {
                        for (unsigned seed = 0; seed < 2; seed++) {
                            uint32_t inst = 0x020080 | ((off >> 1) << 11) |
                                ((off & 1) << 6) | (n << 8) | (read << 4) |
                                (space << 5) | reg;
                            check(a, b, inst, 0, 0x200000 | 0x2000000 |
                                  0x1000000 | seed, mode, false);
                        }
                    }
                }
            }
        }
    }
}

static void check_immediate_shift_boundaries(Machine *a, Machine *b, unsigned mode)
{
    static const uint64_t values[] = {
        0, 1, 2, 0xFFFFFF, 0x1000000, 0xFFFFFFFFFFFFFF,
        0x7FFFFFFFFFFFFF, 0x80000000000000, 0x80000000000001,
        0x00800000800000, 0x007FFFFF123456, 0xFF000000FFFFFF
    };
    static const uint32_t profiles[] = {0, 0xFFFF, 0x100FF, 0x2080};
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF);
    Machine *machines[] = {a, b};
    for (unsigned right = 0; right < 2; right++) {
        for (unsigned shift = 0; shift < 64; shift++) {
            for (unsigned src = 0; src < 2; src++) {
                for (unsigned dst = 0; dst < 2; dst++) {
                    uint32_t op = (right ? 0x0C1C00 : 0x0C1D00) |
                        (src << 7) | (shift << 1) | dst;
                    for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                        uint64_t value = values[v];
                        uint64_t result = value;
                        unsigned carry = 0, overflow = 0;
                        /* Independent bit-at-a-time oracle: overflow remembers
                         * every sign transition, not just the final sign. */
                        for (unsigned bit = 0; bit < shift; bit++) {
                            if (right) {
                                carry = result & 1;
                                result = (result >> 1) | (result & UINT64_C(0x80000000000000));
                            } else {
                                uint64_t next = (result << 1) & mask;
                                carry = result >> 55;
                                overflow |= ((result ^ next) >> 55) != 0;
                                result = next;
                            }
                        }
                        for (unsigned f = 0; f < ARRAY_SIZE(profiles); f++) {
                            uint32_t sr = (profiles[f] & ~((3u << DSP_SR_S0) | (1u << DSP_SR_LF))) |
                                (mode << DSP_SR_S0);
                            uint32_t expected = (sr & 0xFFFF & ~0x3F) | carry |
                                (overflow << DSP_SR_V) | (overflow << DSP_SR_L);
                            if (mode != 3) {
                                unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                                uint64_t upper = result >> bit;
                                unsigned pair = (result >> (bit - 1)) & 3;
                                expected |= ((upper != 0 && upper != (mask >> bit)) << DSP_SR_E) |
                                    ((pair == 0 || pair == 3) << DSP_SR_U) |
                                    ((result == 0) << DSP_SR_Z) | (((result >> 55) & 1) << DSP_SR_N);
                            }
                            dspjit_test_reset_fallback_count();
                            for (unsigned engine = 0; engine < 2; engine++) {
                                Machine *m = machines[engine];
                                prepare(m, op, 0, 1, mode, false);
                                dsp_core_t *c = m->dsp->backend;
                                c->registers[DSP_REG_SR] = sr;
                                c->registers[DSP_REG_A0 + src] = value & 0xFFFFFF;
                                c->registers[DSP_REG_A1 + src] = (value >> 24) & 0xFFFFFF;
                                c->registers[DSP_REG_A2 + src] = value >> 48;
                                if (engine == 0) {
                                    m->dsp->ops->step(m->dsp);
                                    c->cycle_count += c->instr_cycle;
                                } else {
                                    m->dsp->ops->run(m->dsp, 1);
                                }
                                uint64_t got = c->registers[DSP_REG_A0 + dst] |
                                    ((uint64_t)c->registers[DSP_REG_A1 + dst] << 24) |
                                    ((uint64_t)c->registers[DSP_REG_A2 + dst] << 48);
                                if (got != result || c->registers[DSP_REG_SR] != expected) {
                                    fprintf(stderr, "immediate shift oracle: engine=%u op=%06x value=%llx result=%llx/%llx SR=%x/%x\n",
                                            engine, op, (unsigned long long)value,
                                            (unsigned long long)got, (unsigned long long)result,
                                            c->registers[DSP_REG_SR], expected);
                                    exit(1);
                                }
                            }
                            assert(!dspjit_test_fallback_count());
                            assert(compare_machines(a, b) == 0);
                            cases++;
                        }
                    }
                }
            }
        }
    }
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned right = 0; right < 2; right++) {
        static const unsigned endpoints[] = {0, 56, 57, 58, 59, 60, 61, 62, 63};
        for (unsigned index = 0; index < ARRAY_SIZE(endpoints); index++) {
            unsigned shift = endpoints[index];
            for (unsigned src = 0; src < 2; src++) {
                for (unsigned dst = 0; dst < 2; dst++) {
                    for (unsigned seed = 0; seed < 4; seed++) {
                        for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                            for (unsigned irq = 0; irq < 2; irq++) {
                                check(a, b, (right ? 0x0C1C00 : 0x0C1D00) |
                                      (src << 7) | (shift << 1) | dst, 0,
                                      seed | 0x10000 | 0x1000000 | 0x400000 | contexts[ctx], mode, irq);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void check_unsigned_extract(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned controls[] = {DSP_REG_A1, DSP_REG_B1, DSP_REG_X0,
                                       DSP_REG_X1, DSP_REG_Y0, DSP_REG_Y1};
    static const uint64_t values[] = {0, UINT64_C(0xffffffffffffff), UINT64_C(1) << 55,
        UINT64_C(1) << 47, UINT64_C(1) << 24, 1, UINT64_C(0xabcdef01234567), UINT64_C(0x55555555555555)};
    for (unsigned short_words = 0; short_words < 2; short_words++) {
        for (unsigned form = 0; form < 7; form++) {
            for (unsigned width = 0; width < 64; width++) {
                for (unsigned offset = 0; offset < 64; offset++) {
                    unsigned src = (width >> 1) & 1, dst = (offset >> 1) & 1;
                    uint32_t control = short_words ? (width << 16) | (offset << 8) | 0xc0c0ff :
                        (width << 12) | offset | 0xc00fc0;
                    uint32_t op = (form ? 0x0c1a80 | ((form + 1) << 1) : 0x0c1880) | (src << 4) | dst;
                    uint64_t value = values[(width + offset + form) & 7];
                    if (form && controls[form - 1] == DSP_REG_A1 + src)
                        value = (value & ~UINT64_C(0x000000ffffff000000)) | ((uint64_t)control << 24);
                    /* Independent bit routing: enumerate logical field bits
                     * and map each to physical accumulator bits, without a
                     * packed-word shift/mask implementation. */
                    uint64_t result = 0;
                    unsigned active = short_words ? 40 : 56;
                    for (unsigned bit = 0; bit < width; bit++) {
                        unsigned source_bit = offset + bit;
                        if (source_bit >= active) continue;
                        unsigned from = short_words ? source_bit + (source_bit < 16 ? 8 : 16) : source_bit;
                        unsigned to = short_words ? bit + (bit < 16 ? 8 : 16) : bit;
                        if ((value >> from) & 1) result |= UINT64_C(1) << to;
                    }
                    uint32_t sr = ((width + offset + form) & 1 ? 0x3100ff : 0x80) |
                                  (mode << DSP_SR_S0) | (short_words << DSP_SR_SA);
                    uint32_t status = sr & ~0x3fu;
                    if (mode != 3) {
                        unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                        uint64_t upper = result >> bit;
                        unsigned pair = (result >> (bit - 1)) & 3;
                        status |= ((upper != 0 && upper != (UINT64_C(0xffffffffffffff) >> bit)) << DSP_SR_E) |
                                  ((pair == 0 || pair == 3) << DSP_SR_U) |
                                  ((result == 0) << DSP_SR_Z) | (((result >> 55) & 1) << DSP_SR_N);
                    }
                    Machine *machines[] = {a, b};
                    dspjit_test_reset_fallback_count();
                    for (unsigned engine = 0; engine < 2; engine++) {
                        Machine *m = machines[engine];
                        prepare(m, op, control, 1, mode, false);
                        dsp_core_t *c = m->dsp->backend;
                        c->registers[DSP_REG_SR] = sr;
                        c->registers[DSP_REG_A0 + src] = value & 0xffffff;
                        c->registers[DSP_REG_A1 + src] = (value >> 24) & 0xffffff;
                        c->registers[DSP_REG_A2 + src] = value >> 48;
                        if (form) c->registers[controls[form - 1]] = control;
                        uint32_t expected[DSP_REG_MAX];
                        memcpy(expected, c->registers, sizeof(expected));
                        expected[DSP_REG_A0 + dst] = result & 0xffffff;
                        expected[DSP_REG_A1 + dst] = (result >> 24) & 0xffffff;
                        expected[DSP_REG_A2 + dst] = result >> 48;
                        expected[DSP_REG_SR] = status;
                        if (!engine) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        } else m->dsp->ops->run(m->dsp, 1);
                        if (c->pc != (form ? 1 : 2) || c->cycle_count != 2 ||
                            memcmp(c->registers, expected, sizeof(expected))) {
                            fprintf(stderr, "EXTRACTU engine=%u SA=%u form=%u width=%u offset=%u src=%u dst=%u SR=%x/%x\n",
                                    engine, short_words, form, width, offset, src, dst,
                                    c->registers[DSP_REG_SR], status);
                            exit(1);
                        }
                    }
                    if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                    cases++;
                }
            }
        }
    }
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned form = 0; form < 7; form++) {
        for (unsigned src = 0; src < 2; src++) {
            for (unsigned dst = 0; dst < 2; dst++) {
                uint32_t op = (form ? 0x0c1a80 | ((form + 1) << 1) : 0x0c1880) | (src << 4) | dst;
                for (unsigned seed = 0; seed < 8; seed++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        for (unsigned irq = 0; irq < 2; irq++)
                            check(a, b, op, 0x018018, seed | 0x10000 | 0x1000000 | contexts[ctx], mode, irq);
                    }
                }
            }
        }
    }
}

static uint32_t qq_callback_value, qq_read_address, qq_write_address, qq_written_value;
static unsigned qq_reads, qq_writes;

static uint32_t qq_test_read(dsp_core_t *core, uint32_t address)
{
    (void)core;
    qq_reads++;
    qq_read_address = address;
    return qq_callback_value;
}

static void qq_test_write(dsp_core_t *core, uint32_t address, uint32_t value)
{
    (void)core;
    qq_writes++;
    qq_write_address = address;
    qq_written_value = qq_callback_value = value;
}

static void check_memory_bitbranches(Machine *a, Machine *b, unsigned mode, unsigned form)
{
    bool short_memory = form != 0;
    unsigned space = form == 2;
    uint32_t opcode = short_memory ? 0x0c8080 | (space << 6) : 0x048000;
    static const uint32_t pcs[] = {0, 1, 0x30, 0x7fe};
    static const uint32_t displacements[] = {0, 1, 2, 0x20, 0x7fffff, 0x800000, 0xffffe0, 0xffffff};
    static const uint32_t values[] = {0, 0xffffff, 0xaaaaaa, 0x555555};
    static const uint32_t profiles[] = {0, 0x3100ff};
    for (unsigned set = 0; set < 2; set++) {
        for (unsigned port = 0; port < 64; port++) {
            for (unsigned bit = 0; bit < 24; bit++) {
                uint32_t op = opcode | (set << 5) | (port << 8) | bit;
                for (unsigned value = 0; value < ARRAY_SIZE(values); value++) {
                    uint32_t pc = pcs[(port + bit) & 3];
                    uint32_t displacement = displacements[(port + bit + value) & 7];
                    bool taken = ((values[value] >> bit) & 1) == set;
                    uint32_t target = taken ? (pc + displacement) & 0xffffff : pc + 2;
                    for (unsigned profile = 0; profile < ARRAY_SIZE(profiles); profile++) {
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, displacement, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->pc = pc;
                            c->registers[DSP_REG_SR] = profiles[profile] | (mode << DSP_SR_S0);
                            uint32_t expected[DSP_REG_MAX];
                            memcpy(expected, c->registers, sizeof(expected));
                            m->dsp->ops->write_memory(m->dsp, 'P', pc, op);
                            m->dsp->ops->write_memory(m->dsp, 'P', pc + 1, displacement);
                            uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_peripheral;
                            void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_peripheral;
                            c->read_peripheral = qq_test_read;
                            c->write_peripheral = qq_test_write;
                            qq_callback_value = values[value];
                            if (short_memory) {
                                m->dsp->ops->write_memory(m->dsp, space ? 'Y' : 'X', port, values[value]);
                                m->dsp->ops->write_memory(m->dsp, space ? 'X' : 'Y', port, values[value] ^ 0xffffff);
                            }
                            qq_reads = qq_writes = 0;
                            qq_read_address = 0;
                            if (!engine) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);
                            c->read_peripheral = saved_read;
                            c->write_peripheral = saved_write;
                            if (c->pc != target || c->cycle_count != 6 || memcmp(c->registers, expected, sizeof(expected)) ||
                                qq_reads != !short_memory || qq_writes || qq_read_address != (short_memory ? 0 : 0xffff80 + port) ||
                                (short_memory && m->dsp->ops->read_memory(m->dsp, space ? 'Y' : 'X', port) != values[value])) {
                                fprintf(stderr, "X:qq branch engine=%u set=%u port=%u bit=%u pc=%x displacement=%x target=%x/%x reads=%u writes=%u\n",
                                        engine, set, port, bit, pc, displacement, c->pc, target, qq_reads, qq_writes);
                                exit(1);
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                        cases++;
                    }
                }
            }
        }
        static const unsigned ports[] = {0, 0x33, 63};
        static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
        for (unsigned port = 0; port < ARRAY_SIZE(ports); port++) {
            for (unsigned bit = 0; bit < 24; bit++) {
                for (unsigned seed = 0; seed < 2; seed++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++)
                        check(a, b, opcode | (set << 5) | (ports[port] << 8) | bit, 0x30,
                              seed | 0x10000 | 0x1000000 | contexts[ctx], mode, seed != 0);
                }
            }
        }
        for (unsigned port = 0; port < ARRAY_SIZE(ports); port++) {
            for (unsigned bit_index = 0; bit_index < 2; bit_index++) {
                unsigned bit = bit_index ? 23 : 0;
                for (unsigned path = 0; path < 2; path++) {
                    Machine *machines[] = {a, b};
                    dspjit_test_reset_fallback_count();
                    for (unsigned engine = 0; engine < 2; engine++) {
                        Machine *m = machines[engine];
                        uint32_t op = opcode | (set << 5) | (ports[port] << 8) | bit;
                        prepare(m, op, 0x10, 1, mode, false);
                        dsp_core_t *c = m->dsp->backend;
                        c->pc = 0x20;
                        c->registers[DSP_REG_SR] = 0xff | (mode << DSP_SR_S0) | (1u << DSP_SR_LF);
                        c->registers[DSP_REG_LA] = path ? 0x21 : 0x2f;
                        c->registers[DSP_REG_LC] = 2;
                        c->registers[DSP_REG_SSH] = c->stack[0][2] = 0x40;
                        uint32_t expected[DSP_REG_MAX];
                        memcpy(expected, c->registers, sizeof(expected));
                        expected[DSP_REG_LC] = 1;
                        m->dsp->ops->write_memory(m->dsp, 'P', 0x20, op);
                        m->dsp->ops->write_memory(m->dsp, 'P', 0x21, 0x10);
                        uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_peripheral;
                        void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_peripheral;
                        c->read_peripheral = qq_test_read;
                        c->write_peripheral = qq_test_write;
                        qq_callback_value = ((path == 0) == (set != 0)) ? 1u << bit : 0;
                        if (short_memory) {
                            m->dsp->ops->write_memory(m->dsp, space ? 'Y' : 'X', ports[port], qq_callback_value);
                            m->dsp->ops->write_memory(m->dsp, space ? 'X' : 'Y', ports[port], qq_callback_value ^ 0xffffff);
                        }
                        qq_reads = qq_writes = 0;
                        qq_read_address = 0;
                        if (!engine) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        } else m->dsp->ops->run(m->dsp, 1);
                        c->read_peripheral = saved_read;
                        c->write_peripheral = saved_write;
                        if (c->pc != 0x40 || c->cycle_count != 6 || memcmp(c->registers, expected, sizeof(expected)) ||
                            qq_reads != !short_memory || qq_writes || qq_read_address != (short_memory ? 0 : 0xffff80 + ports[port]) ||
                            (short_memory && m->dsp->ops->read_memory(m->dsp, space ? 'Y' : 'X', ports[port]) != qq_callback_value)) {
                            fprintf(stderr, "X:qq DO branch engine=%u set=%u path=%u bit=%u reads=%u PC=%x LC=%x\n",
                                    engine, set, path, bit, qq_reads, c->pc, c->registers[DSP_REG_LC]);
                            exit(1);
                        }
                    }
                    if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                    cases++;
                }
            }
        }
    }
}

static void check_qq_branches(Machine *a, Machine *b, unsigned mode)
{
    check_memory_bitbranches(a, b, mode, 0);
}

static void check_short_memory_bitbranches(Machine *a, Machine *b, unsigned mode)
{
    check_memory_bitbranches(a, b, mode, 1);
    check_memory_bitbranches(a, b, mode, 2);
}

static void check_qq_jumps(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t pcs[] = {0, 1, 0x30, 0x7fe};
    static const uint32_t displacements[] = {0, 1, 2, 0x20, 0x7fffff, 0x800000, 0xffffe0, 0xffffff};
    static const uint32_t values[] = {0, 0xffffff, 0xaaaaaa, 0x555555};
    static const uint32_t profiles[] = {0, 0x3100ff};
    for (unsigned set = 0; set < 2; set++) {
        for (unsigned port = 0; port < 64; port++) {
            for (unsigned bit = 0; bit < 16; bit++) {
                uint32_t op = 0x018080 | (set << 5) | (port << 8) | bit;
                for (unsigned value = 0; value < ARRAY_SIZE(values); value++) {
                    uint32_t pc = pcs[(port + bit) & 3];
                    uint32_t displacement = displacements[(port + bit + value) & 7];
                    bool taken = ((values[value] >> bit) & 1) == set;
                    uint32_t target = taken ? displacement : pc + 2;
                    for (unsigned profile = 0; profile < ARRAY_SIZE(profiles); profile++) {
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, displacement, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->pc = pc;
                            c->registers[DSP_REG_SR] = profiles[profile] | (mode << DSP_SR_S0);
                            uint32_t expected[DSP_REG_MAX];
                            memcpy(expected, c->registers, sizeof(expected));
                            m->dsp->ops->write_memory(m->dsp, 'P', pc, op);
                            m->dsp->ops->write_memory(m->dsp, 'P', pc + 1, displacement);
                            uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_peripheral;
                            void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_peripheral;
                            c->read_peripheral = qq_test_read;
                            c->write_peripheral = qq_test_write;
                            qq_callback_value = values[value];
                            qq_reads = qq_writes = 0;
                            qq_read_address = 0;
                            if (!engine) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);
                            c->read_peripheral = saved_read;
                            c->write_peripheral = saved_write;
                            if (c->pc != target || c->cycle_count != 6 || memcmp(c->registers, expected, sizeof(expected)) ||
                                qq_reads != 1 || qq_writes || qq_read_address != 0xffff80 + port) {
                                fprintf(stderr, "X:qq jump engine=%u set=%u port=%u bit=%u pc=%x displacement=%x target=%x/%x reads=%u writes=%u\n",
                                        engine, set, port, bit, pc, displacement, c->pc, target, qq_reads, qq_writes);
                                exit(1);
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                        cases++;
                    }
                }
            }
        }
        static const unsigned ports[] = {0, 0x33, 63};
        static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
        for (unsigned port = 0; port < ARRAY_SIZE(ports); port++) {
            for (unsigned bit = 0; bit < 16; bit++) {
                for (unsigned seed = 0; seed < 2; seed++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++)
                        check(a, b, 0x018080 | (set << 5) | (ports[port] << 8) | bit, 0x30,
                              seed | 0x10000 | 0x1000000 | contexts[ctx], mode, seed != 0);
                }
            }
        }
        for (unsigned port = 0; port < ARRAY_SIZE(ports); port++) {
            for (unsigned bit_index = 0; bit_index < 2; bit_index++) {
                unsigned bit = bit_index ? 15 : 0;
                for (unsigned path = 0; path < 2; path++) {
                    Machine *machines[] = {a, b};
                    dspjit_test_reset_fallback_count();
                    for (unsigned engine = 0; engine < 2; engine++) {
                        Machine *m = machines[engine];
                        uint32_t op = 0x018080 | (set << 5) | (ports[port] << 8) | bit;
                        prepare(m, op, 0x30, 1, mode, false);
                        dsp_core_t *c = m->dsp->backend;
                        c->pc = 0x20;
                        c->registers[DSP_REG_SR] = 0xff | (mode << DSP_SR_S0) | (1u << DSP_SR_LF);
                        c->registers[DSP_REG_LA] = path ? 0x21 : 0x2f;
                        c->registers[DSP_REG_LC] = 2;
                        c->registers[DSP_REG_SSH] = c->stack[0][2] = 0x40;
                        uint32_t expected[DSP_REG_MAX];
                        memcpy(expected, c->registers, sizeof(expected));
                        expected[DSP_REG_LC] = 1;
                        m->dsp->ops->write_memory(m->dsp, 'P', 0x20, op);
                        m->dsp->ops->write_memory(m->dsp, 'P', 0x21, 0x30);
                        uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_peripheral;
                        void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_peripheral;
                        c->read_peripheral = qq_test_read;
                        c->write_peripheral = qq_test_write;
                        qq_callback_value = ((path == 0) == (set != 0)) ? 1u << bit : 0;
                        qq_reads = qq_writes = 0;
                        qq_read_address = 0;
                        if (!engine) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        } else m->dsp->ops->run(m->dsp, 1);
                        c->read_peripheral = saved_read;
                        c->write_peripheral = saved_write;
                        if (c->pc != 0x40 || c->cycle_count != 6 || memcmp(c->registers, expected, sizeof(expected)) ||
                            qq_reads != 1 || qq_writes || qq_read_address != 0xffff80 + ports[port]) {
                            fprintf(stderr, "X:qq DO jump engine=%u set=%u path=%u bit=%u reads=%u PC=%x LC=%x\n",
                                    engine, set, path, bit, qq_reads, c->pc, c->registers[DSP_REG_LC]);
                            exit(1);
                        }
                    }
                    if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                    cases++;
                }
            }
        }
    }
}

static void check_operand_bitcalls(Machine *a, Machine *b, unsigned mode, unsigned form)
{
    bool relative = form == 1 || form == 5 || form == 6 || form == 7;
    bool short_memory = form == 3 || form == 4 || form == 6 || form == 7;
    unsigned space = form == 4 || form == 7;
    unsigned bits = short_memory && !relative ? 16 : 24;
    uint32_t opcode = form == 5 ? 0x0dc000 : short_memory ? (relative ? 0x0d8080 : 0x0b0080) | (space << 6) : form == 2 ? 0x0b8080 : relative ? 0x048080 : 0x01c080;
    uint32_t peripheral_base = form == 2 || form == 5 ? 0xffffc0 : 0xffff80;
    static const uint32_t pcs[] = {0, 1, 0x30, 0x7fe};
    static const uint32_t displacements[] = {0, 1, 2, 0x20, 0x7fffff, 0x800000, 0xffffe0, 0xffffff};
    static const uint32_t values[] = {0, 0xffffff, 0xaaaaaa, 0x555555};
    static const uint32_t profiles[] = {0, 0x3100ff};
    for (unsigned set = 0; set < 2; set++) {
        for (unsigned port = 0; port < 64; port++) {
            for (unsigned bit = 0; bit < bits; bit++) {
                uint32_t op = opcode | (set << 5) | (port << 8) | bit;
                for (unsigned value = 0; value < ARRAY_SIZE(values); value++) {
                    uint32_t pc = pcs[(port + bit) & 3];
                    uint32_t displacement = displacements[(port + bit + value) & 7];
                    bool taken = ((values[value] >> bit) & 1) == set;
                    uint32_t target = taken ? (relative ? (pc + displacement) & 0xffffff : displacement) : pc + 2;
                    for (unsigned profile = 0; profile < ARRAY_SIZE(profiles); profile++) {
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, displacement, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->pc = pc;
                            c->registers[DSP_REG_SR] = profiles[profile] | (mode << DSP_SR_S0);
                            uint32_t expected[DSP_REG_MAX];
                            memcpy(expected, c->registers, sizeof(expected));
                            if (taken) {
                                expected[DSP_REG_SP] = 3;
                                expected[DSP_REG_SSH] = (pc + 2) & 0xffff;
                                expected[DSP_REG_SSL] = expected[DSP_REG_SR] & 0xffff;
                            }

                            m->dsp->ops->write_memory(m->dsp, 'P', pc, op);
                            m->dsp->ops->write_memory(m->dsp, 'P', pc + 1, displacement);
                            uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_peripheral;
                            void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_peripheral;
                            c->read_peripheral = qq_test_read;
                            c->write_peripheral = qq_test_write;
                            qq_callback_value = values[value];
                            if (short_memory) {
                                m->dsp->ops->write_memory(m->dsp, space ? 'Y' : 'X', port, values[value]);
                                m->dsp->ops->write_memory(m->dsp, space ? 'X' : 'Y', port, values[value] ^ 0xffffff);
                            }
                            qq_reads = qq_writes = 0;
                            qq_read_address = 0;
                            if (!engine) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);

                            for (unsigned slot = 0; slot < ARRAY_SIZE(c->stack[0]); slot++) {
                                uint32_t high = slot == 2 ? 0x20 : taken && slot == 3 ? (pc + 2) & 0xffff : 0;
                                uint32_t low = taken && slot == 3 ? expected[DSP_REG_SR] & 0xffff : 0;
                                if (c->stack[0][slot] != high || c->stack[1][slot] != low) {
                                    fputs("X:qq call stack regression\n", stderr);
                                    exit(1);
                                }
                            }
                            c->read_peripheral = saved_read;
                            c->write_peripheral = saved_write;
                            if (c->pc != target || c->cycle_count != 6 || memcmp(c->registers, expected, sizeof(expected)) ||
                                qq_reads != !short_memory || qq_writes || qq_read_address != (short_memory ? 0 : peripheral_base + port) ||
                                (short_memory && m->dsp->ops->read_memory(m->dsp, space ? 'Y' : 'X', port) != values[value])) {
                                fprintf(stderr, "X:qq call engine=%u set=%u port=%u bit=%u pc=%x displacement=%x target=%x/%x reads=%u writes=%u\n",
                                        engine, set, port, bit, pc, displacement, c->pc, target, qq_reads, qq_writes);
                                exit(1);
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                        cases++;
                    }
                }
            }
        }
        const unsigned ports[] = {0, short_memory ? 0x20 : 0x33, 63};
        static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
        for (unsigned port = 0; port < ARRAY_SIZE(ports); port++) {
            for (unsigned bit = 0; bit < bits; bit++) {
                for (unsigned seed = 0; seed < 2; seed++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++)
                        check(a, b, opcode | (set << 5) | (ports[port] << 8) | bit, 0x30,
                              seed | 0x10000 | 0x1000000 | contexts[ctx], mode, seed != 0);
                }
            }
        }
        for (unsigned port = 0; port < ARRAY_SIZE(ports); port++) {
            for (unsigned bit_index = 0; bit_index < 2; bit_index++) {
                unsigned bit = bit_index ? bits - 1 : 0;
                for (unsigned path = 0; path < 2; path++) {
                    Machine *machines[] = {a, b};
                    dspjit_test_reset_fallback_count();
                    for (unsigned engine = 0; engine < 2; engine++) {
                        Machine *m = machines[engine];
                        uint32_t op = opcode | (set << 5) | (ports[port] << 8) | bit;
                        prepare(m, op, relative ? 0x10 : 0x30, 1, mode, false);
                        dsp_core_t *c = m->dsp->backend;
                        c->pc = 0x20;
                        c->registers[DSP_REG_SR] = 0xff | (mode << DSP_SR_S0) | (1u << DSP_SR_LF);
                        c->registers[DSP_REG_LA] = path ? 0x21 : 0x2f;
                        c->registers[DSP_REG_LC] = 2;
                        c->registers[DSP_REG_SSH] = c->stack[0][2] = 0x40;
                        uint32_t expected[DSP_REG_MAX];
                        memcpy(expected, c->registers, sizeof(expected));
                        expected[DSP_REG_LC] = 1;
                        if (!path) {
                            expected[DSP_REG_SP] = 3;
                            expected[DSP_REG_SSH] = 0x22;
                            expected[DSP_REG_SSL] = expected[DSP_REG_SR] & 0xffff;
                        }
                        m->dsp->ops->write_memory(m->dsp, 'P', 0x20, op);
                        m->dsp->ops->write_memory(m->dsp, 'P', 0x21, relative ? 0x10 : 0x30);
                        uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_peripheral;
                        void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_peripheral;
                        c->read_peripheral = qq_test_read;
                        c->write_peripheral = qq_test_write;
                        qq_callback_value = ((path == 0) == (set != 0)) ? 1u << bit : 0;
                        if (short_memory) {
                            m->dsp->ops->write_memory(m->dsp, space ? 'Y' : 'X', ports[port], qq_callback_value);
                            m->dsp->ops->write_memory(m->dsp, space ? 'X' : 'Y', ports[port], qq_callback_value ^ 0xffffff);
                        }
                        qq_reads = qq_writes = 0;
                        qq_read_address = 0;
                        if (!engine) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        } else m->dsp->ops->run(m->dsp, 1);
                        c->read_peripheral = saved_read;
                        c->write_peripheral = saved_write;
                        if (c->pc != (path ? 0x40 : 0x22) || c->cycle_count != 6 || memcmp(c->registers, expected, sizeof(expected)) ||
                            qq_reads != !short_memory || qq_writes || qq_read_address != (short_memory ? 0 : peripheral_base + ports[port]) ||
                            (short_memory && m->dsp->ops->read_memory(m->dsp, space ? 'Y' : 'X', ports[port]) != qq_callback_value)) {
                            fprintf(stderr, "X:qq DO call engine=%u set=%u path=%u bit=%u reads=%u PC=%x LC=%x\n",
                                    engine, set, path, bit, qq_reads, c->pc, c->registers[DSP_REG_LC]);
                            exit(1);
                        }
                    }
                    if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                    cases++;
                }
            }
        }
    }
}

static void check_qq_calls(Machine *a, Machine *b, unsigned mode)
{
    check_operand_bitcalls(a, b, mode, 0);
}

static void check_qq_relative_calls(Machine *a, Machine *b, unsigned mode)
{
    check_operand_bitcalls(a, b, mode, 1);
}

static void check_pp_calls(Machine *a, Machine *b, unsigned mode)
{
    check_operand_bitcalls(a, b, mode, 2);
}

static void check_short_memory_bitcalls(Machine *a, Machine *b, unsigned mode)
{
    check_operand_bitcalls(a, b, mode, 3);
    check_operand_bitcalls(a, b, mode, 4);
}

static void check_pp_relative_calls(Machine *a, Machine *b, unsigned mode)
{
    check_operand_bitcalls(a, b, mode, 5);
}

static void check_short_relative_calls(Machine *a, Machine *b, unsigned mode)
{
    check_operand_bitcalls(a, b, mode, 6);
    check_operand_bitcalls(a, b, mode, 7);
}

static void check_register_bitcalls(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t values[] = {0, 0xffffff, 0xaaaaaa, 0x555555};
    static const int64_t accum[] = {-(INT64_C(1) << 55), -(INT64_C(1) << 47), -1, 0,
                                  1, INT64_C(1) << 46, (INT64_C(1) << 47) - 1, (INT64_C(1) << 55) - 1};
    static const uint32_t targets[] = {0, 1, 0x30, 0x7fffff, 0x800000, 0xffffff};
    for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_M7; reg++) {
        bool full_acc = reg == DSP_REG_A || reg == DSP_REG_B;
        for (unsigned set = 0; set < 2; set++) {
            for (unsigned bit = 0; bit < 24; bit++) {
                for (unsigned value = 0; value < (full_acc ? ARRAY_SIZE(accum) : ARRAY_SIZE(values)); value++) {
                    for (unsigned profile = 0; profile < 2; profile++) {
                        uint32_t op = 0x0bc000 | (reg << 8) | (set << 5) | bit;
                        uint32_t pc = (reg + bit) & 1 ? 0x30 : 0x7fe;
                        uint32_t target = targets[(reg + value + bit) % ARRAY_SIZE(targets)];
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, target, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->pc = pc;
                            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (profile ? 0x3100bf : 0);
                            uint32_t operand, extra_flags = 0;
                            if (full_acc) {
                                int64_t raw = accum[value];
                                unsigned d = reg & 1;
                                c->registers[DSP_REG_A0 + d] = (uint64_t)raw & 0xffffff;
                                c->registers[DSP_REG_A1 + d] = ((uint64_t)raw >> 24) & 0xffffff;
                                c->registers[DSP_REG_A2 + d] = ((uint64_t)raw >> 48) & 255;
                                int64_t scaled = mode == 1 ? raw >> 25 : mode == 2 ? raw >> 23 : raw >> 24;
                                if (scaled > 0x7fffff) { operand = 0x7fffff; extra_flags = 1u << DSP_SR_L; }
                                else if (scaled < -0x800000) { operand = 0x800000; extra_flags = 1u << DSP_SR_L; }
                                else operand = scaled & 0xffffff;
                            } else {
                                operand = values[value] & ((reg == DSP_REG_A2 || reg == DSP_REG_B2) ? 255 : 0xffffff);
                                c->registers[reg] = operand;
                            }
                            bool taken = ((operand >> bit) & 1) == set;
                            uint32_t expected[DSP_REG_MAX];
                            memcpy(expected, c->registers, sizeof(expected));
                            expected[DSP_REG_SR] |= extra_flags;
                            if (taken) {
                                expected[DSP_REG_SP] = 3;
                                expected[DSP_REG_SSH] = (pc + 2) & 0xffff;
                                expected[DSP_REG_SSL] = expected[DSP_REG_SR] & 0xffff;
                            }
                            m->dsp->ops->write_memory(m->dsp, 'P', pc, op);
                            m->dsp->ops->write_memory(m->dsp, 'P', pc + 1, target);
                            if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                            else m->dsp->ops->run(m->dsp, 1);
                            if (c->pc != (taken ? target : pc + 2) || c->cycle_count != 6 ||
                                memcmp(c->registers, expected, sizeof(expected))) {
                                fprintf(stderr, "Register bit-call engine=%u reg=%u set=%u bit=%u value=%u mode=%u PC=%x SR=%x/%x\n",
                                        engine, reg, set, bit, value, mode, c->pc, c->registers[DSP_REG_SR], expected[DSP_REG_SR]);
                                exit(1);
                            }
                            for (unsigned slot = 0; slot < ARRAY_SIZE(c->stack[0]); slot++) {
                                uint32_t high = slot == 2 ? 0x20 : taken && slot == 3 ? (pc + 2) & 0xffff : 0;
                                uint32_t low = taken && slot == 3 ? expected[DSP_REG_SR] & 0xffff : 0;
                                if (c->stack[0][slot] != high || c->stack[1][slot] != low) exit(1);
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                        cases++;
                    }
                }
            }
        }
    }
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_LC; reg++) {
        if (reg > DSP_REG_M7 && reg < DSP_REG_SR) continue;
        for (unsigned set = 0; set < 2; set++) {
            for (unsigned bit = 0; bit < 24; bit++) {
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned seed = 0; seed < 2; seed++)
                        check(a, b, 0x0bc000 | (reg << 8) | (set << 5) | bit, 0x30,
                              seed | 0x10000 | 0x1000000 | contexts[ctx], mode, seed != 0);
                }
            }
        }
    }
    /* CLR creates deferred flags; the following call must observe Z=1. */
    for (unsigned set = 0; set < 2; set++) {
        Machine *machines[] = {a, b};
        dspjit_test_reset_fallback_count();
        for (unsigned engine = 0; engine < 2; engine++) {
            Machine *m = machines[engine];
            prepare(m, 0x200013, 0, 1, mode, false);
            dsp_core_t *c = m->dsp->backend;
            c->registers[DSP_REG_SR] = mode << DSP_SR_S0;
            m->dsp->ops->write_memory(m->dsp, 'P', 1, 0x0bc000 | (DSP_REG_SR << 8) | (set << 5) | DSP_SR_Z);
            m->dsp->ops->write_memory(m->dsp, 'P', 2, 0x20);
            if (!engine) {
                for (unsigned step = 0; step < 2; step++) {
                    m->dsp->ops->step(m->dsp);
                    c->cycle_count += c->instr_cycle;
                }
            } else m->dsp->ops->run(m->dsp, 8);
            if (c->pc != (set ? 0x20 : 3) || c->cycle_count != 8 ||
                !(c->registers[DSP_REG_SR] & (1u << DSP_SR_Z)) ||
                c->registers[DSP_REG_SP] != (set ? 3 : 2) ||
                (set && (c->stack[0][3] != 3 || c->stack[1][3] != (c->registers[DSP_REG_SR] & 0xffff)))) {
                fputs("Register bit-call deferred status regression\n", stderr);
                exit(1);
            }
        }
        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
        cases++;
    }
}

static void check_indirect_memory_bitcontrol(Machine *a, Machine *b, unsigned mode, bool branch)
{
    static const uint32_t pcs[] = {0, 1, 0x30, 0x7fe};
    static const uint32_t offsets[] = {0, 1, 2, 0x20, 0x7fffff, 0x800000, 0xffffe0, 0xffffff};
    static const uint32_t values[] = {0, 0xffffff, 0xaaaaaa, 0x555555};
    for (unsigned ea = 0; ea < 64; ea++) {
        unsigned am = ea >> 3, n = ea & 7;
        if (am == 6) continue;
        for (unsigned space = 0; space < 2; space++) {
            for (unsigned set = 0; set < 2; set++) {
                for (unsigned bit = 0; bit < (branch ? 24 : 16); bit++) {
                    uint32_t op = (branch ? 0x0c8000 : 0x0b4080) | (ea << 8) | (space << 6) | (set << 5) | bit;
                    for (unsigned value = 0; value < ARRAY_SIZE(values); value++) {
                        for (unsigned profile = 0; profile < (branch ? 2 : 4); profile++) {
                            bool loop = branch ? profile == 1 : profile >= 2;
                            bool taken = ((values[value] >> bit) & 1) == set;
                            uint32_t address = am == 5 ? 0x23 : 0x20;
                            uint32_t next_r = am == 0 ? 0x1d : am == 1 ? 0x23 : am == 2 ? 0x1f : am == 3 ? 0x21 : 0x20;
                            uint32_t pc = branch ? loop ? 0x20 : pcs[(ea + bit + value) & 3] : 0;
                            uint32_t ext = branch ? loop ? 0x10 : offsets[(ea + bit + value) & 7] : 0x30;
                            uint32_t taken_target = branch ? (pc + ext) & 0xffffff : ext;
                            uint32_t next = pc + 2;
                            uint32_t target = loop ? branch ? 0x40 : taken ? next : 0x40 : taken ? taken_target : next;
                            Machine *machines[] = {a, b};
                            dspjit_test_reset_fallback_count();
                            for (unsigned engine = 0; engine < 2; engine++) {
                                Machine *m = machines[engine];
                                prepare(m, op, ext, 1, mode, false);
                                dsp_core_t *c = m->dsp->backend;
                                c->pc = pc;
                                c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | ((profile & 1) ? 0x3100bf : 0) |
                                                          (loop ? 1u << DSP_SR_LF : 0);
                                c->registers[DSP_REG_R0 + n] = am == 7 ? 0x21 : 0x20;
                                c->registers[DSP_REG_N0 + n] = 3;
                                c->registers[DSP_REG_M0 + n] = 0xffff;
                                if (loop) {
                                    c->registers[DSP_REG_LA] = (taken ? taken_target : next) - 1;
                                    c->registers[DSP_REG_LC] = 2;
                                    c->registers[DSP_REG_SSH] = c->stack[0][2] = 0x40;
                                }
                                uint32_t expected[DSP_REG_MAX];
                                memcpy(expected, c->registers, sizeof(expected));
                                expected[DSP_REG_R0 + n] = next_r;
                                if (loop) expected[DSP_REG_LC] = 1;
                                if (taken && !branch) {
                                    expected[DSP_REG_SP] = 3;
                                    expected[DSP_REG_SSH] = next;
                                    expected[DSP_REG_SSL] = expected[DSP_REG_SR] & 0xffff;
                                }
                                m->dsp->ops->write_memory(m->dsp, space ? 'Y' : 'X', address, values[value]);
                                m->dsp->ops->write_memory(m->dsp, space ? 'X' : 'Y', address, values[value] ^ 0xffffff);
                                m->dsp->ops->write_memory(m->dsp, 'P', pc, op);
                                m->dsp->ops->write_memory(m->dsp, 'P', pc + 1, ext);
                                if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                                else m->dsp->ops->run(m->dsp, 1);
                                if (c->pc != target || c->cycle_count != (am == 5 || am == 7 ? 8 : 6) ||
                                    memcmp(c->registers, expected, sizeof(expected)) ||
                                    m->dsp->ops->read_memory(m->dsp, space ? 'Y' : 'X', address) != values[value]) {
                                    fprintf(stderr, "Indirect bit-call engine=%u ea=%u space=%u set=%u bit=%u profile=%u PC=%x/%x R=%x/%x cycles=%u\n",
                                            engine, ea, space, set, bit, profile, c->pc, target,
                                            c->registers[DSP_REG_R0 + n], next_r, (unsigned)c->cycle_count);
                                    exit(1);
                                }
                                for (unsigned slot = 0; slot < ARRAY_SIZE(c->stack[0]); slot++) {
                                    uint32_t high = slot == 2 ? loop ? 0x40 : 0x20 : taken && !branch && slot == 3 ? next : 0;
                                    uint32_t low = taken && !branch && slot == 3 ? expected[DSP_REG_SR] & 0xffff : 0;
                                    if (c->stack[0][slot] != high || c->stack[1][slot] != low) exit(1);
                                }
                            }
                            if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                            cases++;
                        }
                    }
                    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        for (unsigned seed = 0; seed < 2; seed++)
                            check(a, b, op, 0x30, seed | 0x10000 | 0x1000000 | contexts[ctx], mode, seed != 0);
                    }
                }
            }
        }
    }
}

static void check_indirect_memory_bitcalls(Machine *a, Machine *b, unsigned mode)
{
    check_indirect_memory_bitcontrol(a, b, mode, false);
}

static void check_indirect_memory_bitbranches(Machine *a, Machine *b, unsigned mode)
{
    check_indirect_memory_bitcontrol(a, b, mode, true);
}

static void check_qq_bits(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t codes[] = {0x010000, 0x010020, 0x014000, 0x014020};
    static const uint32_t values[] = {0, 0xffffff, 0xaaaaaa, 0x555555};
    static const uint32_t profiles[] = {0, 0x3100ff};
    for (unsigned operation = 0; operation < 4; operation++) {
        /* Retain the existing opcode-table bit ranges: BCLR/BTST have
         * four bit-number bits; BSET/BCHG accept the 24-bit word range. */
        unsigned bits = operation == 0 || operation == 3 ? 16 : 24;
        for (unsigned port = 0; port < 64; port++) {
            for (unsigned bit = 0; bit < bits; bit++) {
                uint32_t op = codes[operation] | (port << 8) | bit;
                for (unsigned value = 0; value < ARRAY_SIZE(values); value++) {
                    uint32_t old = values[value];
                    uint32_t expected_value = old;
                    /* An independent decision on each bit, rather than
                     * the production mask-based read/modify/write. */
                    bool set = (old >> bit) & 1;
                    if (operation == 0 && set) expected_value -= 1u << bit;
                    if (operation == 1 && !set) expected_value += 1u << bit;
                    if (operation == 2) expected_value = set ? old - (1u << bit) : old + (1u << bit);
                    for (unsigned profile = 0; profile < ARRAY_SIZE(profiles); profile++) {
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, 0, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->registers[DSP_REG_SR] = profiles[profile] | (mode << DSP_SR_S0);
                            uint32_t expected[DSP_REG_MAX];
                            memcpy(expected, c->registers, sizeof(expected));
                            expected[DSP_REG_SR] = (expected[DSP_REG_SR] & ~1u) | set;
                            uint32_t (*saved_read)(dsp_core_t *, uint32_t) = c->read_peripheral;
                            void (*saved_write)(dsp_core_t *, uint32_t, uint32_t) = c->write_peripheral;
                            c->read_peripheral = qq_test_read;
                            c->write_peripheral = qq_test_write;
                            qq_callback_value = old;
                            qq_reads = qq_writes = 0;
                            qq_read_address = qq_write_address = qq_written_value = 0;
                            if (!engine) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);
                            c->read_peripheral = saved_read;
                            c->write_peripheral = saved_write;
                            if (c->pc != 1 || c->cycle_count != 4 || memcmp(c->registers, expected, sizeof(expected)) ||
                                qq_reads != 1 || qq_read_address != 0xffff80 + port ||
                                qq_writes != (operation != 3) ||
                                (operation != 3 && (qq_write_address != 0xffff80 + port || qq_written_value != expected_value))) {
                                fprintf(stderr, "X:qq engine=%u operation=%u port=%u bit=%u reads=%u writes=%u read=%x write=%x value=%x/%x SR=%x/%x\n",
                                        engine, operation, port, bit, qq_reads, qq_writes, qq_read_address, qq_write_address,
                                        qq_written_value, expected_value, c->registers[DSP_REG_SR], expected[DSP_REG_SR]);
                                exit(1);
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                        cases++;
                    }
                }
            }
        }
        static const unsigned ports[] = {0, 0x33, 63};
        static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
        for (unsigned port = 0; port < ARRAY_SIZE(ports); port++) {
            for (unsigned bit = 0; bit < bits; bit++) {
                for (unsigned seed = 0; seed < 2; seed++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++)
                        check(a, b, codes[operation] | (ports[port] << 8) | bit, 0,
                              seed | 0x10000 | 0x1000000 | contexts[ctx], mode, seed != 0);
                }
            }
        }
    }

}

static void check_register_logical_shifts(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned controls[] = {DSP_REG_A1, DSP_REG_B1, DSP_REG_X0, DSP_REG_X1, DSP_REG_Y0, DSP_REG_Y1};
    static const uint32_t values[] = {0, 1, 0x800000, 0xffffff, 0x7fffff, 0xabc123, 0xaaaaaa, 0x555555};
    static const uint32_t profiles[] = {0, 0xff, 0x310080, 0x3100ff};
    static const uint32_t counts[] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
        32,33,64,255,256,0xffff,0x10000,0xffffff};
    for (unsigned short_words = 0; short_words < 2; short_words++) {
        for (unsigned control = 0; control < 6; control++) {
            for (unsigned right = 0; right < 2; right++) {
                for (unsigned dst = 0; dst < 2; dst++) {
                    uint32_t op = (right ? 0x0c1e30 : 0x0c1e10) | ((control + 2) << 1) | dst;
                    for (unsigned count = 0; count < ARRAY_SIZE(counts); count++) {
                        for (unsigned value = 0; value < ARRAY_SIZE(values); value++) {
                            for (unsigned profile = 0; profile < ARRAY_SIZE(profiles); profile++) {
                                Machine *machines[] = {a, b};
                                dspjit_test_reset_fallback_count();
                                for (unsigned engine = 0; engine < 2; engine++) {
                                    Machine *m = machines[engine];
                                    prepare(m, op, 0, 1, mode, false);
                                    dsp_core_t *c = m->dsp->backend;
                                    c->registers[DSP_REG_SR] = profiles[profile] | (mode << DSP_SR_S0) | (short_words << DSP_SR_SA);
                                    c->registers[DSP_REG_A1 + dst] = values[value];
                                    c->registers[controls[control]] = short_words ? ((counts[count] & 0xffff) << 8) | 0xa5 : counts[count];
                                    uint32_t expected[DSP_REG_MAX];
                                    memcpy(expected, c->registers, sizeof(expected));
                                    unsigned bits = short_words ? 16 : 24;
                                    uint32_t word = expected[DSP_REG_A1 + dst] >> (short_words ? 8 : 0);
                                    uint32_t n = expected[controls[control]] >> (short_words ? 8 : 0), carry = 0;
                                    /* Independent one-bit shifts. After width+1
                                     * steps, both word and carry stay zero. */
                                    unsigned steps = n < bits + 1 ? n : bits + 1;
                                    for (unsigned step = 0; step < steps; step++) {
                                        carry = right ? word & 1 : (word >> (bits - 1)) & 1;
                                        word = right ? word >> 1 : (word << 1) & ((1u << bits) - 1);
                                    }
                                    expected[DSP_REG_A1 + dst] = word << (short_words ? 8 : 0);
                                    expected[DSP_REG_SR] = (expected[DSP_REG_SR] & ~0xfu) | carry |
                                        (word == 0 ? 4 : 0) | (((word >> (bits - 1)) & 1) << DSP_SR_N);
                                    if (!engine) {
                                        m->dsp->ops->step(m->dsp);
                                        c->cycle_count += c->instr_cycle;
                                    } else m->dsp->ops->run(m->dsp, 1);
                                    if (c->pc != 1 || c->cycle_count != 2 || memcmp(c->registers, expected, sizeof(expected))) {
                                        fprintf(stderr, "Register logical shift engine=%u SA=%u control=%u right=%u dst=%u count=%x MSP=%x/%x SR=%x/%x\n",
                                                engine, short_words, control, right, dst, n, c->registers[DSP_REG_A1 + dst],
                                                expected[DSP_REG_A1 + dst], c->registers[DSP_REG_SR], expected[DSP_REG_SR]);
                                        exit(1);
                                    }
                                }
                                if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                                cases++;
                            }
                        }
                    }
                }
            }
        }
    }
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned control = 0; control < 6; control++) {
        for (unsigned right = 0; right < 2; right++) {
            for (unsigned dst = 0; dst < 2; dst++) {
                for (unsigned seed = 0; seed < 8; seed++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        for (unsigned irq = 0; irq < 2; irq++)
                            check(a, b, (right ? 0x0c1e30 : 0x0c1e10) | ((control + 2) << 1) | dst,
                                  0, seed | 0x10000 | 0x1000000 | contexts[ctx], mode, irq);
                    }
                }
            }
        }
    }
}

static void check_merge_case(Machine *a, Machine *b, unsigned mode, unsigned short_words,
                             unsigned source, unsigned dst, uint32_t input, uint64_t value, uint32_t flags)
{
    static const unsigned sources[] = {DSP_REG_A1, DSP_REG_B1, DSP_REG_X0, DSP_REG_X1, DSP_REG_Y0, DSP_REG_Y1};
    uint32_t op = 0x0c1b80 | ((source + 2) << 1) | dst;
    Machine *machines[] = {a, b};
    dspjit_test_reset_fallback_count();
    for (unsigned engine = 0; engine < 2; engine++) {
        Machine *m = machines[engine];
        prepare(m, op, 0, 1, mode, false);
        dsp_core_t *c = m->dsp->backend;
        c->registers[DSP_REG_SR] = flags | (mode << DSP_SR_S0) | (short_words << DSP_SR_SA);
        c->registers[DSP_REG_A0 + dst] = value & 0xffffff;
        c->registers[DSP_REG_A1 + dst] = (value >> 24) & 0xffffff;
        c->registers[DSP_REG_A2 + dst] = value >> 48;
        c->registers[sources[source]] = input;
        uint32_t expected[DSP_REG_MAX];
        memcpy(expected, c->registers, sizeof(expected));
        uint32_t before = expected[DSP_REG_A1 + dst], word = 0;
        /* Route individual MSP bits; EXP/LSP and the unselected MSP bits
         * remain exactly as snapshotted, including same-accumulator aliases. */
        for (unsigned bit = 0; bit < 24; bit++) {
            unsigned from = short_words ? (bit >= 16 ? bit - 8 : bit) : (bit >= 12 ? bit - 12 : bit);
            uint32_t operand = bit >= (short_words ? 16u : 12u) ? input : before;
            if ((operand >> from) & 1) word |= 1u << bit;
        }
        expected[DSP_REG_A1 + dst] = word;
        expected[DSP_REG_SR] = (expected[DSP_REG_SR] & ~0xeu) | ((word >> 20) & 8) | (word == 0 ? 4 : 0);
        if (!engine) {
            m->dsp->ops->step(m->dsp);
            c->cycle_count += c->instr_cycle;
        } else m->dsp->ops->run(m->dsp, 1);
        if (c->pc != 1 || c->cycle_count != 2 || memcmp(c->registers, expected, sizeof(expected))) {
            fprintf(stderr, "MERGE engine=%u SA=%u source=%u dst=%u input=%x MSP=%x/%x SR=%x/%x\n",
                    engine, short_words, source, dst, input, c->registers[DSP_REG_A1 + dst], word,
                    c->registers[DSP_REG_SR], expected[DSP_REG_SR]);
            exit(1);
        }
    }
    if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
    cases++;
}

static void check_merge(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t inputs[] = {0, 0xffffff, 1, 0xfff, 0x1000, 0x800, 0x8000, 0x100, 0xabcdef, 0x123456, 0xaaaaaa, 0x555555};
    static const uint64_t values[] = {0, UINT64_C(0xffffffffffffff), 1, UINT64_C(1) << 55,
        UINT64_C(1) << 47, UINT64_C(0x123456abcdef12), UINT64_C(0x80ffffff000001), UINT64_C(0x55555555555555)};
    static const uint32_t flags[] = {0, 0xff, 1, 2, 0x10, 0x20, 0x40, 0x310080};
    for (unsigned short_words = 0; short_words < 2; short_words++) {
        for (unsigned source = 0; source < 6; source++) {
            for (unsigned dst = 0; dst < 2; dst++) {
                for (unsigned input = 0; input < ARRAY_SIZE(inputs); input++)
                    for (unsigned value = 0; value < ARRAY_SIZE(values); value++)
                        for (unsigned flag = 0; flag < ARRAY_SIZE(flags); flag++)
                            check_merge_case(a, b, mode, short_words, source, dst, inputs[input], values[value], flags[flag]);
            }
        }
        /* Exhaust every source halfword on X0, with nonzero ignored bits. */
        for (unsigned half = 0; half < (short_words ? 256u : 4096u); half++) {
            for (unsigned dst = 0; dst < 2; dst++)
                check_merge_case(a, b, mode, short_words, 2, dst,
                    short_words ? (half << 8) | 0xff00ff : half | 0xfff000, values[half & 7], flags[half & 7]);
        }
    }
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned source = 0; source < 6; source++) {
        for (unsigned dst = 0; dst < 2; dst++) {
            for (unsigned seed = 0; seed < 8; seed++) {
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned irq = 0; irq < 2; irq++)
                        check(a, b, 0x0c1b80 | ((source + 2) << 1) | dst, 0,
                              seed | 0x10000 | 0x1000000 | contexts[ctx], mode, irq);
                }
            }
        }
    }
    static const unsigned widths[] = {1, 8, 16, 16}, offsets[] = {0, 7, 16, 24};
    const uint64_t source_value = UINT64_C(0xabcdef01234567);
    for (unsigned short_words = 0; short_words < 2; short_words++) {
        for (unsigned pair = 0; pair < ARRAY_SIZE(widths); pair++) {
            uint64_t result = 0;
            for (unsigned bit = 0; bit < widths[pair]; bit++) {
                unsigned from = offsets[pair] + bit;
                unsigned physical = short_words ? from + (from < 16 ? 8 : 16) : from;
                unsigned to = short_words ? bit + (bit < 16 ? 8 : 16) : bit;
                if ((source_value >> physical) & 1) result |= UINT64_C(1) << to;
            }
            for (unsigned control = 0; control < 2; control++) {
                Machine *machines[] = {a, b};
                dspjit_test_reset_fallback_count();
                for (unsigned engine = 0; engine < 2; engine++) {
                    Machine *m = machines[engine];
                    prepare(m, 0x0c1b88 | control, 0, 1, mode, false);
                    dsp_core_t *c = m->dsp->backend;
                    c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (short_words << DSP_SR_SA);
                    c->registers[DSP_REG_A1 + control] = offsets[pair] << (short_words ? 8 : 0);
                    c->registers[DSP_REG_X0] = widths[pair] << (short_words ? 8 : 0);
                    c->registers[DSP_REG_A0 + (control ^ 1)] = source_value & 0xffffff;
                    c->registers[DSP_REG_A1 + (control ^ 1)] = (source_value >> 24) & 0xffffff;
                    c->registers[DSP_REG_A2 + (control ^ 1)] = source_value >> 48;
                    m->dsp->ops->write_memory(m->dsp, 'P', 1,
                        0x0c1a80 | ((control ^ 1) << 4) | ((control + 2) << 1) | (control ^ 1));
                    if (!engine) {
                        for (unsigned step = 0; step < 2; step++) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        }
                    } else m->dsp->ops->run(m->dsp, 4);
                    uint64_t got = c->registers[DSP_REG_A0 + (control ^ 1)] |
                        ((uint64_t)c->registers[DSP_REG_A1 + (control ^ 1)] << 24) |
                        ((uint64_t)c->registers[DSP_REG_A2 + (control ^ 1)] << 48);
                    if (c->pc != 2 || c->cycle_count != 4 || got != result) {
                        fprintf(stderr, "MERGE/EXTRACTU engine=%u SA=%u pair=%u control=%u\n", engine, short_words, pair, control);
                        exit(1);
                    }
                }
                if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                cases++;
            }
        }
    }
}

static void check_insert(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned controls[] = {DSP_REG_A1, DSP_REG_B1, DSP_REG_X0, DSP_REG_X1, DSP_REG_Y0, DSP_REG_Y1};
    static const unsigned sources[] = {DSP_REG_A0, DSP_REG_B0, DSP_REG_X0, DSP_REG_X1, DSP_REG_Y0, DSP_REG_Y1};
    static const uint64_t values[] = {0, UINT64_C(0xffffffffffffff), UINT64_C(1) << 55,
        UINT64_C(1) << 47, UINT64_C(1) << 24, 1, UINT64_C(0xabcdef01234567), UINT64_C(0x55555555555555)};
    static const uint32_t words[] = {0, 0xffffff, 0x800000, 0xabcdef, 1, 0x555555, 0x00ff00, 0x123456};
    for (unsigned short_words = 0; short_words < 2; short_words++) {
        for (unsigned form = 0; form < 7; form++) {
            for (unsigned width = 0; width < 64; width++) {
                for (unsigned offset = 0; offset < 64; offset++) {
                    unsigned source = (width + 3 * offset + form) % 6, dst = (offset >> 1) & 1;
                    uint32_t control = short_words ? (width << 16) | (offset << 8) | 0xc0c0ff :
                        (width << 12) | offset | 0xc00fc0;
                    uint32_t op = (form ? 0x0c1b00 | ((form + 1) << 1) : 0x0c1900) | ((source + 2) << 4) | dst;
                    uint64_t value = values[(width + 5 * offset + form) & 7];
                    uint32_t sr = ((width + offset + form) & 1 ? 0x3100ff : 0x80) |
                                  (mode << DSP_SR_S0) | (short_words << DSP_SR_SA);
                    Machine *machines[] = {a, b};
                    dspjit_test_reset_fallback_count();
                    for (unsigned engine = 0; engine < 2; engine++) {
                        Machine *m = machines[engine];
                        prepare(m, op, control, 1, mode, false);
                        dsp_core_t *c = m->dsp->backend;
                        c->registers[DSP_REG_SR] = sr;
                        c->registers[DSP_REG_A0 + dst] = value & 0xffffff;
                        c->registers[DSP_REG_A1 + dst] = (value >> 24) & 0xffffff;
                        c->registers[DSP_REG_A2 + dst] = value >> 48;
                        c->registers[sources[source]] = words[(width + offset + form) & 7];
                        if (form) c->registers[controls[form - 1]] = control;
                        uint32_t expected[DSP_REG_MAX];
                        memcpy(expected, c->registers, sizeof(expected));
                        uint64_t before = expected[DSP_REG_A0 + dst] |
                            ((uint64_t)expected[DSP_REG_A1 + dst] << 24) | ((uint64_t)expected[DSP_REG_A2 + dst] << 48);
                        uint32_t input = expected[sources[source]];
                        uint64_t result = 0;
                        unsigned active = short_words ? 40 : 56;
                        /* Independent routing: decide each destination bit
                         * separately from the snapshotted source/control. */
                        for (unsigned bit = 0; bit < active; bit++) {
                            unsigned physical = short_words ? bit + (bit < 16 ? 8 : 16) : bit;
                            bool set = (before >> physical) & 1;
                            int start = short_words ? (int)offset - 16 : (int)offset;
                            if (start >= 0 && bit >= (unsigned)start && bit - start < width) {
                                unsigned from = bit - start;
                                set = from < (short_words ? 16u : 24u) &&
                                    ((input >> (from + (short_words ? 8 : 0))) & 1);
                            }
                            if (set) result |= UINT64_C(1) << physical;
                        }
                        uint32_t status = sr & ~0x3fu;
                        if (mode != 3) {
                            unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                            uint64_t upper = result >> bit;
                            unsigned pair = (result >> (bit - 1)) & 3;
                            status |= ((upper != 0 && upper != (UINT64_C(0xffffffffffffff) >> bit)) << DSP_SR_E) |
                                      ((pair == 0 || pair == 3) << DSP_SR_U) |
                                      ((result == 0) << DSP_SR_Z) | (((result >> 55) & 1) << DSP_SR_N);
                        }
                        expected[DSP_REG_A0 + dst] = result & 0xffffff;
                        expected[DSP_REG_A1 + dst] = (result >> 24) & 0xffffff;
                        expected[DSP_REG_A2 + dst] = result >> 48;
                        expected[DSP_REG_SR] = status;
                        if (!engine) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        } else m->dsp->ops->run(m->dsp, 1);
                        if (c->pc != (form ? 1 : 2) || c->cycle_count != 2 ||
                            memcmp(c->registers, expected, sizeof(expected))) {
                            fprintf(stderr, "INSERT engine=%u SA=%u form=%u source=%u width=%u offset=%u dst=%u SR=%x/%x\n",
                                    engine, short_words, form, source, width, offset, dst, c->registers[DSP_REG_SR], status);
                            exit(1);
                        }
                    }
                    if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                    cases++;
                }
            }
        }
    }
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned form = 0; form < 7; form++) {
        for (unsigned source = 0; source < 6; source++) {
            for (unsigned dst = 0; dst < 2; dst++) {
                uint32_t op = (form ? 0x0c1b00 | ((form + 1) << 1) : 0x0c1900) | ((source + 2) << 4) | dst;
                for (unsigned seed = 0; seed < 8; seed++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        for (unsigned irq = 0; irq < 2; irq++)
                            check(a, b, op, 0x010018, seed | 0x10000 | 0x1000000 | contexts[ctx], mode, irq);
                    }
                }
            }
        }
    }
    static const unsigned widths[] = {1, 8, 16, 16}, offsets[] = {0, 7, 16, 24};
    for (unsigned short_words = 0; short_words < 2; short_words++) {
        for (unsigned pair = 0; pair < ARRAY_SIZE(widths); pair++) {
            for (unsigned dst = 0; dst < 2; dst++) {
                Machine *machines[] = {a, b};
                uint32_t insert_control = short_words ? (widths[pair] << 16) | ((offsets[pair] + 16) << 8) :
                    (widths[pair] << 12) | offsets[pair];
                uint32_t extract_control = short_words ? (widths[pair] << 16) | (offsets[pair] << 8) : insert_control;
                uint32_t expected = ((short_words ? 0xabcd : 0xabcdf1) & ((1u << widths[pair]) - 1)) << (short_words ? 8 : 0);
                dspjit_test_reset_fallback_count();
                for (unsigned engine = 0; engine < 2; engine++) {
                    Machine *m = machines[engine];
                    prepare(m, 0x0c1940 | dst, insert_control, 1, mode, false);
                    dsp_core_t *c = m->dsp->backend;
                    c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (short_words << DSP_SR_SA);
                    c->registers[DSP_REG_X0] = 0xabcdf1;
                    m->dsp->ops->write_memory(m->dsp, 'P', 2, 0x0c1880 | (dst << 4) | (dst ^ 1));
                    m->dsp->ops->write_memory(m->dsp, 'P', 3, extract_control);
                    if (!engine) {
                        for (unsigned step = 0; step < 2; step++) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        }
                    } else m->dsp->ops->run(m->dsp, 4);
                    if (c->pc != 4 || c->cycle_count != 4 || c->registers[DSP_REG_A0 + (dst ^ 1)] != expected ||
                        c->registers[DSP_REG_A1 + (dst ^ 1)] || c->registers[DSP_REG_A2 + (dst ^ 1)]) {
                        fprintf(stderr, "INSERT/EXTRACTU roundtrip engine=%u SA=%u pair=%u dst=%u\n", engine, short_words, pair, dst);
                        exit(1);
                    }
                }
                if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                cases++;
            }
        }
    }
}

static void check_signed_extract(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned controls[] = {DSP_REG_A1, DSP_REG_B1, DSP_REG_X0,
                                       DSP_REG_X1, DSP_REG_Y0, DSP_REG_Y1};
    static const uint64_t values[] = {0, UINT64_C(0xffffffffffffff), UINT64_C(1) << 55,
        UINT64_C(1) << 47, UINT64_C(1) << 24, 1, UINT64_C(0xabcdef01234567), UINT64_C(0x55555555555555)};
    for (unsigned short_words = 0; short_words < 2; short_words++) {
        for (unsigned form = 0; form < 7; form++) {
            for (unsigned width = 0; width < 64; width++) {
                for (unsigned offset = 0; offset < 64; offset++) {
                    unsigned src = (width >> 1) & 1, dst = (offset >> 1) & 1;
                    uint32_t control = short_words ? (width << 16) | (offset << 8) | 0xc0c0ff :
                        (width << 12) | offset | 0xc00fc0;
                    uint32_t op = (form ? 0x0c1a00 | ((form + 1) << 1) : 0x0c1800) | (src << 4) | dst;
                    uint64_t value = values[(width + offset + form) & 7];
                    if (form && controls[form - 1] == DSP_REG_A1 + src)
                        value = (value & ~UINT64_C(0x000000ffffff000000)) | ((uint64_t)control << 24);
                    /* Independent bit routing: enumerate logical field bits
                     * and map each to physical accumulator bits, without a
                     * packed-word shift/mask implementation. */
                    uint64_t result = 0;
                    unsigned active = short_words ? 40 : 56;
                    for (unsigned bit = 0; bit < width; bit++) {
                        unsigned source_bit = offset + bit;
                        if (source_bit >= active) continue;
                        unsigned from = short_words ? source_bit + (source_bit < 16 ? 8 : 16) : source_bit;
                        unsigned to = short_words ? bit + (bit < 16 ? 8 : 16) : bit;
                        if ((value >> from) & 1) result |= UINT64_C(1) << to;
                    }
                    if (width && width <= active && offset + width <= active) {
                        unsigned sign_index = offset + width - 1;
                        unsigned physical = short_words ? sign_index + (sign_index < 16 ? 8 : 16) : sign_index;
                        if ((value >> physical) & 1) {
                            for (unsigned bit = width; bit < active; bit++) {
                                unsigned to = short_words ? bit + (bit < 16 ? 8 : 16) : bit;
                                result |= UINT64_C(1) << to;
                            }
                        }
                    }
                    uint32_t sr = ((width + offset + form) & 1 ? 0x3100ff : 0x80) |
                                  (mode << DSP_SR_S0) | (short_words << DSP_SR_SA);
                    uint32_t status = sr & ~0x3fu;
                    if (mode != 3) {
                        unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                        uint64_t upper = result >> bit;
                        unsigned pair = (result >> (bit - 1)) & 3;
                        status |= ((upper != 0 && upper != (UINT64_C(0xffffffffffffff) >> bit)) << DSP_SR_E) |
                                  ((pair == 0 || pair == 3) << DSP_SR_U) |
                                  ((result == 0) << DSP_SR_Z) | (((result >> 55) & 1) << DSP_SR_N);
                    }
                    Machine *machines[] = {a, b};
                    dspjit_test_reset_fallback_count();
                    for (unsigned engine = 0; engine < 2; engine++) {
                        Machine *m = machines[engine];
                        prepare(m, op, control, 1, mode, false);
                        dsp_core_t *c = m->dsp->backend;
                        c->registers[DSP_REG_SR] = sr;
                        c->registers[DSP_REG_A0 + src] = value & 0xffffff;
                        c->registers[DSP_REG_A1 + src] = (value >> 24) & 0xffffff;
                        c->registers[DSP_REG_A2 + src] = value >> 48;
                        if (form) c->registers[controls[form - 1]] = control;
                        uint32_t expected[DSP_REG_MAX];
                        memcpy(expected, c->registers, sizeof(expected));
                        expected[DSP_REG_A0 + dst] = result & 0xffffff;
                        expected[DSP_REG_A1 + dst] = (result >> 24) & 0xffffff;
                        expected[DSP_REG_A2 + dst] = result >> 48;
                        expected[DSP_REG_SR] = status;
                        if (!engine) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        } else m->dsp->ops->run(m->dsp, 1);
                        if (c->pc != (form ? 1 : 2) || c->cycle_count != 2 ||
                            memcmp(c->registers, expected, sizeof(expected))) {
                            fprintf(stderr, "EXTRACT engine=%u SA=%u form=%u width=%u offset=%u src=%u dst=%u SR=%x/%x\n",
                                    engine, short_words, form, width, offset, src, dst,
                                    c->registers[DSP_REG_SR], status);
                            exit(1);
                        }
                    }
                    if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                    cases++;
                }
            }
        }
    }
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned form = 0; form < 7; form++) {
        for (unsigned src = 0; src < 2; src++) {
            for (unsigned dst = 0; dst < 2; dst++) {
                uint32_t op = (form ? 0x0c1a00 | ((form + 1) << 1) : 0x0c1800) | (src << 4) | dst;
                for (unsigned seed = 0; seed < 8; seed++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        for (unsigned irq = 0; irq < 2; irq++)
                            check(a, b, op, 0x018018, seed | 0x10000 | 0x1000000 | contexts[ctx], mode, irq);
                    }
                }
            }
        }
    }
}

static void check_count_leading_bits(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t profiles[] = {0, 0xff, 1, 0x10, 0x20, 0x40, 0x80, 0x310000};
    for (unsigned short_words = 0; short_words < 2; short_words++) {
        unsigned width = short_words ? 40 : 56;
        uint64_t mask = (UINT64_C(1) << width) - 1;
        for (unsigned run = 1; run <= width; run++) {
            for (unsigned sign = 0; sign < 2; sign++) {
                for (unsigned tail = 0; tail < 2; tail++) {
                    /* Construct a known run, then its first opposite bit.
                     * Lower bits cannot affect the closed-form expectation. */
                    uint64_t active = run == width ? 0 : UINT64_C(1) << (width - run - 1);
                    if (tail && active) active |= active - 1;
                    if (sign) active ^= mask;
                    int result = active ? 9 - (int)run : 0;
                    uint64_t value = short_words ? ((active >> 16) << 32) | ((active & 0xffff) << 8) : active;
                    if (short_words && tail) value |= UINT64_C(0x000000ff0000ff);
                    for (unsigned src = 0; src < 2; src++) {
                        for (unsigned dst = 0; dst < 2; dst++) {
                            uint32_t op = 0x0c1e00 | (src << 1) | dst;
                            for (unsigned profile = 0; profile < ARRAY_SIZE(profiles); profile++) {
                                Machine *machines[] = {a, b};
                                dspjit_test_reset_fallback_count();
                                for (unsigned engine = 0; engine < 2; engine++) {
                                    Machine *m = machines[engine];
                                    prepare(m, op, 0, 1, mode, false);
                                    dsp_core_t *c = m->dsp->backend;
                                    c->registers[DSP_REG_SR] = profiles[profile] | (mode << DSP_SR_S0) |
                                        (short_words << DSP_SR_SA);
                                    c->registers[DSP_REG_A0 + src] = value & 0xffffff;
                                    c->registers[DSP_REG_A1 + src] = (value >> 24) & 0xffffff;
                                    c->registers[DSP_REG_A2 + src] = value >> 48;
                                    uint32_t expected[DSP_REG_MAX];
                                    memcpy(expected, c->registers, sizeof(expected));
                                    expected[DSP_REG_A0 + dst] = 0;
                                    expected[DSP_REG_A1 + dst] = (uint32_t)result & 0xffffff;
                                    expected[DSP_REG_A2 + dst] = result < 0 ? 0xff : 0;
                                    expected[DSP_REG_SR] = (expected[DSP_REG_SR] & ~0xeu) |
                                        (result < 0 ? 8 : 0) | (result == 0 ? 4 : 0);
                                    if (!engine) {
                                        m->dsp->ops->step(m->dsp);
                                        c->cycle_count += c->instr_cycle;
                                    } else m->dsp->ops->run(m->dsp, 1);
                                    if (c->pc != 1 || c->cycle_count != 2 ||
                                        memcmp(c->registers, expected, sizeof(expected))) {
                                        fprintf(stderr, "CLB engine=%u SA=%u run=%u sign=%u tail=%u src=%u dst=%u MSP=%x/%x SR=%x/%x\n",
                                                engine, short_words, run, sign, tail, src, dst,
                                                c->registers[DSP_REG_A1 + dst], expected[DSP_REG_A1 + dst],
                                                c->registers[DSP_REG_SR], expected[DSP_REG_SR]);
                                        exit(1);
                                    }
                                }
                                if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                                cases++;
                            }
                        }
                    }
                }
            }
        }
    }
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned src = 0; src < 2; src++) {
        for (unsigned dst = 0; dst < 2; dst++) {
            for (unsigned seed = 0; seed < 16; seed++) {
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned irq = 0; irq < 2; irq++)
                        check(a, b, 0x0c1e00 | (src << 1) | dst, 0,
                              seed | 0x10000 | 0x1000000 | contexts[ctx], mode, irq);
                }
            }
        }
    }
}

static void check_register_arithmetic_shifts(Machine *a, Machine *b, unsigned mode)
{
    static const uint64_t values[] = {
        0, 1, 0xFFFFFFFFFFFFFF,
        0x7FFFFFFFFFFFFF, 0x80000000000000, 0x80000000000001,
        0x00800000800000, 0xFF000000FFFFFF
    };
    static const uint32_t profiles[] = {0, 0x100FF};
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF);
    Machine *machines[] = {a, b};
    static const unsigned controls[] = {DSP_REG_A1, DSP_REG_B1, DSP_REG_X0,
                                       DSP_REG_X1, DSP_REG_Y0, DSP_REG_Y1};
    for (unsigned control = 0; control < ARRAY_SIZE(controls); control++)
    for (unsigned right = 0; right < 2; right++) {
        for (unsigned shift = 0; shift < 64; shift++) {
            for (unsigned src = 0; src < 2; src++) {
                for (unsigned dst = 0; dst < 2; dst++) {
                    uint32_t op = (right ? 0x0C1E60 : 0x0C1E40) |
                        (src << 4) | ((control + 2) << 1) | dst;
                    for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                        uint64_t value = values[v];
                        if (controls[control] == DSP_REG_A1 + src)
                            value = (value & ~(UINT64_C(63) << 24)) | ((uint64_t)shift << 24);
                        uint64_t result = value;
                        unsigned carry = 0, overflow = 0;
                        /* Independent bit-at-a-time oracle: overflow remembers
                         * every sign transition, not just the final sign. */
                        for (unsigned bit = 0; bit < shift; bit++) {
                            if (right) {
                                carry = result & 1;
                                result = (result >> 1) | (result & UINT64_C(0x80000000000000));
                            } else {
                                uint64_t next = (result << 1) & mask;
                                carry = result >> 55;
                                overflow |= ((result ^ next) >> 55) != 0;
                                result = next;
                            }
                        }
                        for (unsigned f = 0; f < ARRAY_SIZE(profiles); f++) {
                            uint32_t sr = (profiles[f] & ~((3u << DSP_SR_S0) | (1u << DSP_SR_LF))) |
                                (mode << DSP_SR_S0);
                            uint32_t expected = (sr & 0xFFFF & ~0x3F) | carry |
                                (overflow << DSP_SR_V) | (overflow << DSP_SR_L);
                            if (mode != 3) {
                                unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                                uint64_t upper = result >> bit;
                                unsigned pair = (result >> (bit - 1)) & 3;
                                expected |= ((upper != 0 && upper != (mask >> bit)) << DSP_SR_E) |
                                    ((pair == 0 || pair == 3) << DSP_SR_U) |
                                    ((result == 0) << DSP_SR_Z) | (((result >> 55) & 1) << DSP_SR_N);
                            }
                            dspjit_test_reset_fallback_count();
                            for (unsigned engine = 0; engine < 2; engine++) {
                                Machine *m = machines[engine];
                                prepare(m, op, 0, 1, mode, false);
                                dsp_core_t *c = m->dsp->backend;
                                c->registers[DSP_REG_SR] = sr;
                                c->registers[DSP_REG_A0 + src] = value & 0xFFFFFF;
                                c->registers[DSP_REG_A1 + src] = (value >> 24) & 0xFFFFFF;
                                c->registers[DSP_REG_A2 + src] = value >> 48;
                                c->registers[controls[control]] = controls[control] == DSP_REG_A1 + src ?
                                    (value >> 24) & 0xffffff : 0xa50000 | shift;
                                uint32_t registers[DSP_REG_MAX];
                                memcpy(registers, c->registers, sizeof(registers));
                                registers[DSP_REG_A0 + dst] = result & 0xffffff;
                                registers[DSP_REG_A1 + dst] = (result >> 24) & 0xffffff;
                                registers[DSP_REG_A2 + dst] = result >> 48;
                                registers[DSP_REG_SR] = expected;
                                if (engine == 0) {
                                    m->dsp->ops->step(m->dsp);
                                    c->cycle_count += c->instr_cycle;
                                } else {
                                    m->dsp->ops->run(m->dsp, 1);
                                }
                                uint64_t got = c->registers[DSP_REG_A0 + dst] |
                                    ((uint64_t)c->registers[DSP_REG_A1 + dst] << 24) |
                                    ((uint64_t)c->registers[DSP_REG_A2 + dst] << 48);
                                if (got != result || c->registers[DSP_REG_SR] != expected ||
                                    memcmp(registers, c->registers, sizeof(registers)) || c->pc != 1 || c->cycle_count != 2) {
                                    fprintf(stderr, "register shift oracle: engine=%u op=%06x value=%llx result=%llx/%llx SR=%x/%x\n",
                                            engine, op, (unsigned long long)value,
                                            (unsigned long long)got, (unsigned long long)result,
                                            c->registers[DSP_REG_SR], expected);
                                    exit(1);
                                }
                            }
                            assert(!dspjit_test_fallback_count());
                            assert(compare_machines(a, b) == 0);
                            cases++;
                        }
                    }
                }
            }
        }
    }
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
    for (unsigned control = 0; control < ARRAY_SIZE(controls); control++)
    for (unsigned right = 0; right < 2; right++) {
        static const unsigned seed_profiles[] = {0, 56, 63};
        for (unsigned index = 0; index < ARRAY_SIZE(seed_profiles); index++) {
            unsigned profile = seed_profiles[index];
            for (unsigned src = 0; src < 2; src++) {
                for (unsigned dst = 0; dst < 2; dst++) {
                    for (unsigned seed = 0; seed < 4; seed++) {
                        for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                            for (unsigned irq = 0; irq < 2; irq++) {
                                check(a, b, (right ? 0x0C1E60 : 0x0C1E40) |
                                      (src << 4) | ((control + 2) << 1) | dst, 0,
                                      seed | profile | 0x10000 | 0x1000000 | 0x400000 | contexts[ctx], mode, irq);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void check_signed_right_operations(Machine *a, Machine *b, unsigned mode)
{
    static const uint64_t values[] = {
        0xFFFFFFFFFFFFFF, 0, 1, 2, 0xFFFFFF, 0x1000000,
        0x7FFFFFFFFFFFFF, 0x80000000000000, 0x80000000000001,
        0x00800000800000, 0x007FFFFF123456, 0xFF000000FFFFFF
    };
    static const uint32_t profiles[] = {0, 0xFF, 0x100FF, 0x2080};
    static const uint32_t opcodes[] = {0x22, 0x02, 0x06}; /* ASR, ADDR, SUBR */
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF);
    const uint64_t sign = UINT64_C(0x80000000000000);
    Machine *machines[] = {a, b};
    for (unsigned op = 0; op < ARRAY_SIZE(opcodes); op++) {
        for (unsigned d = 0; d < 2; d++) {
            for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                for (unsigned other = 0; other < (op ? ARRAY_SIZE(values) : 1); other++) {
                    uint64_t value = values[v], source = values[other];
                    uint64_t half = (value >> 1) | (value & sign);
                    uint64_t result = op == 0 ? half :
                        op == 1 ? (half + source) & mask : (half - source) & mask;
                    bool overflow = op == 1 ? (~(half ^ source) & (half ^ result) & sign) != 0 :
                        op == 2 ? ((half ^ source) & (half ^ result) & sign) != 0 : false;
                    unsigned carry = (value & 1) | (op == 1 ? half + source > mask :
                                                    op == 2 ? half < source : false);
                    for (unsigned f = 0; f < ARRAY_SIZE(profiles); f++) {
                        uint32_t sr = profiles[f] | (mode << DSP_SR_S0);
                        uint32_t expected = (sr & 0xFFFF & ~0x3F) | carry |
                            (overflow << DSP_SR_V) | (overflow << DSP_SR_L);
                        if (mode != 3) {
                            unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                            uint64_t upper = result >> bit;
                            unsigned pair = (result >> (bit - 1)) & 3;
                            expected |= ((upper != 0 && upper != (mask >> bit)) << DSP_SR_E) |
                                ((pair == 0 || pair == 3) << DSP_SR_U) |
                                ((result == 0) << DSP_SR_Z) | (((result >> 55) & 1) << DSP_SR_N);
                        }
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, 0x200000 | opcodes[op] | (d << 3), 0, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->registers[DSP_REG_SR] = sr;
                            c->registers[DSP_REG_A0 + d] = value & 0xFFFFFF;
                            c->registers[DSP_REG_A1 + d] = (value >> 24) & 0xFFFFFF;
                            c->registers[DSP_REG_A2 + d] = value >> 48;
                            c->registers[DSP_REG_A0 + (d ^ 1)] = source & 0xFFFFFF;
                            c->registers[DSP_REG_A1 + (d ^ 1)] = (source >> 24) & 0xFFFFFF;
                            c->registers[DSP_REG_A2 + (d ^ 1)] = source >> 48;
                            if (engine == 0) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);
                            uint64_t got = c->registers[DSP_REG_A0 + d] |
                                ((uint64_t)c->registers[DSP_REG_A1 + d] << 24) |
                                ((uint64_t)c->registers[DSP_REG_A2 + d] << 48);
                            if (got != result || c->registers[DSP_REG_SR] != expected) {
                                fprintf(stderr, "signed right oracle: engine=%u op=%x value=%llx source=%llx result=%llx/%llx SR=%x/%x\n",
                                        engine, opcodes[op], (unsigned long long)value, (unsigned long long)source,
                                        (unsigned long long)got, (unsigned long long)result,
                                        c->registers[DSP_REG_SR], expected);
                                exit(1);
                            }
                        }
                        assert(!dspjit_test_fallback_count());
                        assert(compare_machines(a, b) == 0);
                        cases++;
                    }
                }
            }
        }
    }
}

static void check_left_shift_operations(Machine *a, Machine *b, unsigned mode)
{
    static const uint64_t values[] = {
        0xFFFFFFFFFFFFFF, 0, 1, 2, 0xFFFFFF, 0x1000000,
        0x7FFFFFFFFFFFFF, 0x80000000000000, 0x80000000000001,
        0x00800000800000, 0x007FFFFF123456, 0xFF000000FFFFFF
    };
    static const uint32_t profiles[] = {0, 0xFF, 0x100FF, 0x2080};
    static const uint32_t opcodes[] = {0x32, 0x12, 0x16}; /* ASL, ADDL, SUBL */
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF);
    const uint64_t sign = UINT64_C(0x80000000000000);
    Machine *machines[] = {a, b};
    for (unsigned op = 0; op < ARRAY_SIZE(opcodes); op++) {
        for (unsigned d = 0; d < 2; d++) {
            for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                for (unsigned other = 0; other < (op ? ARRAY_SIZE(values) : 1); other++) {
                    uint64_t value = values[v], source = values[other];
                    uint64_t shifted = (value << 1) & mask;
                    uint64_t result = op == 0 ? shifted :
                        op == 1 ? (shifted + source) & mask : (shifted - source) & mask;
                    bool overflow = ((value ^ shifted) & sign) != 0;
                    overflow |= op == 1 ? (~(shifted ^ source) & (shifted ^ result) & sign) != 0 :
                        op == 2 ? ((shifted ^ source) & (shifted ^ result) & sign) != 0 : false;
                    unsigned carry = (value >> 55) | (op == 1 ? shifted + source > mask :
                                                    op == 2 ? shifted < source : false);
                    for (unsigned f = 0; f < ARRAY_SIZE(profiles); f++) {
                        uint32_t sr = profiles[f] | (mode << DSP_SR_S0);
                        uint32_t expected = (sr & 0xFFFF & ~0x3F) | carry |
                            (overflow << DSP_SR_V) | (overflow << DSP_SR_L);
                        if (mode != 3) {
                            unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                            uint64_t upper = result >> bit;
                            unsigned pair = (result >> (bit - 1)) & 3;
                            expected |= ((upper != 0 && upper != (mask >> bit)) << DSP_SR_E) |
                                ((pair == 0 || pair == 3) << DSP_SR_U) |
                                ((result == 0) << DSP_SR_Z) | (((result >> 55) & 1) << DSP_SR_N);
                        }
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, 0x200000 | opcodes[op] | (d << 3), 0, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->registers[DSP_REG_SR] = sr;
                            c->registers[DSP_REG_A0 + d] = value & 0xFFFFFF;
                            c->registers[DSP_REG_A1 + d] = (value >> 24) & 0xFFFFFF;
                            c->registers[DSP_REG_A2 + d] = value >> 48;
                            c->registers[DSP_REG_A0 + (d ^ 1)] = source & 0xFFFFFF;
                            c->registers[DSP_REG_A1 + (d ^ 1)] = (source >> 24) & 0xFFFFFF;
                            c->registers[DSP_REG_A2 + (d ^ 1)] = source >> 48;
                            if (engine == 0) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);
                            uint64_t got = c->registers[DSP_REG_A0 + d] |
                                ((uint64_t)c->registers[DSP_REG_A1 + d] << 24) |
                                ((uint64_t)c->registers[DSP_REG_A2 + d] << 48);
                            if (got != result || c->registers[DSP_REG_SR] != expected) {
                                fprintf(stderr, "left shift oracle: engine=%u op=%x value=%llx source=%llx result=%llx/%llx SR=%x/%x\n",
                                        engine, opcodes[op], (unsigned long long)value, (unsigned long long)source,
                                        (unsigned long long)got, (unsigned long long)result,
                                        c->registers[DSP_REG_SR], expected);
                                exit(1);
                            }
                        }
                        assert(!dspjit_test_fallback_count());
                        assert(compare_machines(a, b) == 0);
                        cases++;
                    }
                }
            }
        }
    }
}

static void check_call_chains(Machine *a, Machine *b)
{
    static const uint32_t program[] = {0x0D0003, 0x200040, 0x0C0000, 0x200048, 0x00000C};
    static const unsigned budgets[] = {1024, 1, 2, 3, 31, 32, 63, 64, 127, 1000};
    Machine *machines[] = {a, b};
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
            Machine *m = machines[i];
            prepare(m, program[0], 0, 1, mode, false);
            dsp_core_t *c = m->dsp->backend;
            c->registers[DSP_REG_SR] = mode << DSP_SR_S0;
            c->registers[DSP_REG_LA] = 0xFFF;
            for (unsigned pc = 0; pc < ARRAY_SIZE(program); pc++) {
                m->dsp->ops->write_memory(m->dsp, 'P', pc, program[pc]);
            }
        }
        for (unsigned pass = 0; pass < 4; pass++) {
            for (unsigned n = 0; n < ARRAY_SIZE(budgets); n++) {
                dspjit_test_reset_fallback_count();
                dspjit_test_reset_entry_count();
                a->dsp->ops->run(a->dsp, budgets[n]);
                b->dsp->ops->run(b->dsp, budgets[n]);
                assert(!dspjit_test_fallback_count());
                assert(compare_machines(a, b) == 0);
                if (pass > 0 && budgets[n] >= 1000) {
                    assert(dspjit_test_entry_count() == 1);
                }
                cases++;
            }
        }
    }
}

static void check_loop_cache_identity(Machine *a, Machine *b)
{
    prepare(a, 0x200000, 0, 1, 0, false);
    prepare(b, 0x200000, 0, 1, 0, false);
    Machine *machines[] = {a, b};
    for (unsigned n = 0; n < 64; n++) {
        unsigned mode = (n >> 1) & 3;
        bool loop = (n & 1) != 0;
        if (n == 32) {
            /* All eight identities are warm: a changed P word must drop
             * every variant, not just the four scaling-only entries. */
            for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
                machines[i]->dsp->ops->write_memory(machines[i]->dsp, 'P', 0, 0x200040);
            }
        }
        if (n == 48) {
            for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
                machines[i]->dsp->ops->invalidate_opcache(machines[i]->dsp);
            }
        }
        dspjit_test_reset_fallback_count();
        for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
            Machine *m = machines[i];
            dsp_core_t *c = m->dsp->backend;
            c->pc = 0;
            m->dsp->save_cycles = 0; /* one fresh instruction budget */
            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) |
                (loop ? 1 << DSP_SR_LF : 0);
            c->registers[DSP_REG_LA] = 0;
            c->registers[DSP_REG_LC] = 2;
            c->registers[DSP_REG_SSH] = 0;
            if (i == 0) {
                m->dsp->ops->step(m->dsp);
                c->cycle_count += c->instr_cycle;
            } else {
                m->dsp->ops->run(m->dsp, 1);
            }
            assert(c->pc == (loop ? 0u : 1u));
            if (c->registers[DSP_REG_LC] != (loop ? 1u : 2u)) {
                fprintf(stderr, "loop reuse n=%u engine=%u loop=%u lc=%u sr=%x pc=%u\n", n, i, loop, c->registers[DSP_REG_LC], c->registers[DSP_REG_SR], c->pc);
                exit(1);
            }
        }
        assert(!dspjit_test_fallback_count());
        assert(compare_machines(a, b) == 0);
        cases++;
    }
}

static void check_macr_rounding(Machine *a, Machine *b)
{
    Machine *machines[] = { a, b };
    dspjit_test_reset_fallback_count();
    for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
        Machine *m = machines[i];
        prepare(m, 0x2000EB, 0, 1, 0, false);
        dsp_core_t *c = m->dsp->backend;
        c->registers[DSP_REG_SR] = 0;
        c->registers[DSP_REG_X1] = 0x400000;
        c->registers[DSP_REG_Y0] = 0x400000;
        c->registers[DSP_REG_B2] = c->registers[DSP_REG_B1] = 0;
        c->registers[DSP_REG_B0] = 0x800001;
        if (i == 0) {
            m->dsp->ops->step(m->dsp);
            c->cycle_count += c->instr_cycle;
        } else {
            m->dsp->ops->run(m->dsp, 1);
        }
        /* 2*(0x400000*0x400000)+0x800001 rounds above the tie. */
        if (c->registers[DSP_REG_B2] != 0 ||
            c->registers[DSP_REG_B1] != 0x200001 ||
            c->registers[DSP_REG_B0] != 0) {
            fprintf(stderr, "MACR rounding regression in %s\n",
                    i == 0 ? "interpreter" : "JIT");
            exit(1);
        }
    }
    assert(dspjit_test_fallback_count() == 0);
    assert(compare_machines(a, b) == 0);
    cases++;
}

static void check_rotate_carry(Machine *a, Machine *b)
{
    static const uint32_t words[] = { 0, 1, 0x800000, 0xFFFFFF };
    for (unsigned d = 0; d < 2; d++) {
        for (unsigned left = 0; left < 2; left++) {
            for (unsigned carry = 0; carry < 2; carry++) {
                for (unsigned w = 0; w < ARRAY_SIZE(words); w++) {
                    uint32_t value = words[w];
                    uint32_t result = left ? ((value << 1) | carry) & 0xFFFFFF :
                                            (value >> 1) | (carry << 23);
                    uint32_t out_c = left ? value >> 23 : value & 1;
                    uint32_t sr = (1 << DSP_SR_E) | (1 << DSP_SR_U) | (1 << DSP_SR_L);
                    uint32_t expected_sr = sr | out_c |
                        ((result >> 23) << DSP_SR_N) | ((result == 0) << DSP_SR_Z);
                    Machine *machines[] = { a, b };
                    dspjit_test_reset_fallback_count();
                    for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
                        Machine *m = machines[i];
                        prepare(m, 0x200027 | (left << 4) | (d << 3), 0, 1, 0, false);
                        dsp_core_t *c = m->dsp->backend;
                        c->registers[DSP_REG_SR] = sr | carry | (1 << DSP_SR_V);
                        c->registers[DSP_REG_A0 + d] = 0x123456;
                        c->registers[DSP_REG_A1 + d] = value;
                        c->registers[DSP_REG_A2 + d] = 0xAB;
                        if (i == 0) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        } else {
                            m->dsp->ops->run(m->dsp, 1);
                        }
                        if (c->registers[DSP_REG_A1 + d] != result ||
                            c->registers[DSP_REG_A0 + d] != 0x123456 ||
                            c->registers[DSP_REG_A2 + d] != 0xAB ||
                            c->registers[DSP_REG_SR] != expected_sr) {
                            fprintf(stderr, "rotate-through-carry regression: engine=%u left=%u d=%u carry=%u word=%06x\n",
                                    i, left, d, carry, value);
                            exit(1);
                        }
                    }
                    assert(dspjit_test_fallback_count() == 0);
                    assert(compare_machines(a, b) == 0);
                    cases++;
                }
            }
        }
    }
}

static void check_max_transfer(Machine *a, Machine *b)
{
    static const uint64_t values[] = {
        0, 1, 0xFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFF,
        0x80000000000000, 0x123456789ABCDE,
        0xFEDCBA98765432, 0x80000000000001
    };
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned carry = 0; carry < 2; carry++) {
            for (unsigned ai = 0; ai < ARRAY_SIZE(values); ai++) {
                for (unsigned bi = 0; bi < ARRAY_SIZE(values); bi++) {
                    uint64_t va = values[ai], vb = values[bi];
                    /* MAX tests B-A in 56 bits, including wraparound. C is
                     * cleared on transfer (DSP56300FM 13-106). */
                    uint64_t diff = (vb - va) & 0xFFFFFFFFFFFFFF;
                    bool transfer = diff == 0 || (diff & 0x80000000000000);
                    uint64_t expected_b = transfer ? va : vb;
                    uint32_t sr = 0xFE | (mode << DSP_SR_S0);
                    Machine *machines[] = { a, b };
                    dspjit_test_reset_fallback_count();
                    for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
                        Machine *m = machines[i];
                        prepare(m, 0x20001D, 0, 1, mode, false);
                        dsp_core_t *c = m->dsp->backend;
                        c->registers[DSP_REG_SR] = sr | carry;
                        for (unsigned d = 0; d < 2; d++) {
                            uint64_t v = d == 0 ? va : vb;
                            c->registers[DSP_REG_A0 + d] = v & 0xFFFFFF;
                            c->registers[DSP_REG_A1 + d] = (v >> 24) & 0xFFFFFF;
                            c->registers[DSP_REG_A2 + d] = v >> 48;
                        }
                        if (i == 0) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        } else {
                            m->dsp->ops->run(m->dsp, 1);
                        }
                        uint64_t actual_a = ((uint64_t)c->registers[DSP_REG_A2] << 48) |
                            ((uint64_t)c->registers[DSP_REG_A1] << 24) | c->registers[DSP_REG_A0];
                        uint64_t actual_b = ((uint64_t)c->registers[DSP_REG_B2] << 48) |
                            ((uint64_t)c->registers[DSP_REG_B1] << 24) | c->registers[DSP_REG_B0];
                        if (actual_a != va || actual_b != expected_b ||
                            c->registers[DSP_REG_SR] != (sr | !transfer)) {
                            fprintf(stderr, "MAX regression: engine=%u a=%014llx b=%014llx mode=%u carry=%u\n",
                                    i, (unsigned long long)va, (unsigned long long)vb, mode, carry);
                            exit(1);
                        }
                    }
                    assert(dspjit_test_fallback_count() == 0);
                    assert(compare_machines(a, b) == 0);
                    cases++;
                }
            }
        }
    }
}

static void check_tcc(Machine *a, Machine *b)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned cc = 0; cc < 16; cc++) {
            for (unsigned pair = 0; pair < 16; pair++) {
                if (pair >= 2 && pair < 8) {
                    continue; /* reserved accumulator selector */
                }
                for (unsigned sr = 0; sr < 256; sr++) {
                    check(a, b, 0x020000 | (cc << 12) | (pair << 3),
                          0, 0x20000 | sr, mode, false);
                }
                for (unsigned src = 0; src < 8; src++) {
                    for (unsigned dst = 0; dst < 8; dst++) {
                        for (unsigned sr = 0; sr < 2; sr++) {
                            uint32_t op = 0x030000 | (cc << 12) |
                                          (src << 8) | (pair << 3) | dst;
                            check(a, b, op, 0, 0x20000 | (sr ? 255 : 0), mode, false);
                        }
                    }
                }
                /* Complete the transfer at a DO boundary as well. */
                check(a, b, 0x020000 | (cc << 12) | (pair << 3),
                      0, 0x60001, mode, false);
            }
            for (unsigned src = 0; src < 8; src++) {
                for (unsigned dst = 0; dst < 8; dst++) {
                    check(a, b, 0x020800 | (cc << 12) | (src << 8) | dst,
                          0, 0x20055, mode, false);
                }
            }
        }
    }
}

static void check_data_write_invalidation(Machine *a, Machine *b)
{
    Machine *machines[] = {a, b};
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
            prepare(machines[i], 0x0140CA, 0x800000, 1, mode, false);
        }
        a->dsp->ops->run(a->dsp, 1);
        b->dsp->ops->run(b->dsp, 1);
        assert(compare_machines(a, b) == 0);
        uint64_t scans = dspjit_test_invalidation_scans();
        for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
            machines[i]->dsp->ops->write_memory(machines[i]->dsp, 'P', DSP_PRAM_SIZE - 2, 0x654321);
        }
        assert(dspjit_test_invalidation_scans() == scans);
        for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
            Machine *m = machines[i];
            dsp_core_t *c = m->dsp->backend;
            m->dsp->ops->write_memory(m->dsp, 'P', 1, 1);
            c->pc = 0;
            m->dsp->save_cycles = 0;
        }
        assert(dspjit_test_invalidation_scans() == scans + 1);
        a->dsp->ops->run(a->dsp, 1);
        b->dsp->ops->run(b->dsp, 1);
        assert(compare_machines(a, b) == 0);
        cases += 2;
    }
}

static void check_hot_control_registers(Machine *a, Machine *b)
{
    static const uint32_t bits[] = {0x0AC040, 0x0AC060, 0x0BC040};
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned seed = 0; seed < 16; seed++) {
            for (unsigned op = 0; op < ARRAY_SIZE(bits); op++) {
                for (unsigned bit = 16; bit < 32; bit++) {
                    check(a, b, bits[op] | (DSP_REG_SR << 8) | bit, 0,
                          seed | 0x1000000 | (seed & 1 ? 0x40000 : 0), mode, false);
                }
            }
            for (unsigned reg = DSP_REG_M0; reg <= DSP_REG_M7; reg++) {
                for (unsigned ea = 0; ea < 64; ea++) {
                    if ((ea >> 3) == 6 && (ea & 7) != 0 && (ea & 7) != 4) continue;
                    check(a, b, 0x05C020 | (ea << 8) | reg, 0x30, seed, mode, false);
                }
            }
        }
    }
}

static void check_program_memory_reads(Machine *a, Machine *b)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_M7; reg++) {
            for (unsigned ea = 0; ea < 64; ea++) {
                if ((ea >> 3) == 6 && (ea & 7) != 0) continue;
                for (unsigned seed = 0; seed < 4; seed++) {
                    check(a, b, 0x07C080 | (ea << 8) | reg, 0x30,
                          seed | 0x10000 | 0x8000000, mode, false);
                }
            }
        }
    }
}

static void check_status_restore_chains(Machine *a, Machine *b)
{
    static const unsigned budgets[] = {1, 2, 3, 7, 31, 63, 127, 1000};
    static const uint32_t program[] = {0x200040, 0x04C4B9, 0x200048, 0x0C0000};
    Machine *machines[] = {a, b};
    for (unsigned before = 0; before < 8; before++) {
        for (unsigned after = 0; after < 8; after++) {
            for (unsigned flags = 0; flags < 16; flags++) {
                for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
                    Machine *m = machines[i];
                    prepare(m, program[0], 0, 1, before & 3, false);
                    dsp_core_t *c = m->dsp->backend;
                    c->registers[DSP_REG_SR] = ((before & 3) << DSP_SR_S0) |
                        (before & 4 ? 1 << DSP_SR_LF : 0);
                    c->registers[DSP_REG_X0] = ((after & 3) << DSP_SR_S0) |
                        (after & 4 ? 1 << DSP_SR_LF : 0) | flags | 0xF00000;
                    c->registers[DSP_REG_LA] = 0xFFF;
                    for (unsigned pc = 0; pc < ARRAY_SIZE(program); pc++) {
                        m->dsp->ops->write_memory(m->dsp, 'P', pc, program[pc]);
                    }
                }
                for (unsigned n = 0; n < ARRAY_SIZE(budgets); n++) {
                    dspjit_test_reset_fallback_count();
                    a->dsp->ops->run(a->dsp, budgets[n]);
                    b->dsp->ops->run(b->dsp, budgets[n]);
                    assert(!dspjit_test_fallback_count());
                    assert(compare_machines(a, b) == 0);
                    cases++;
                }
            }
        }
    }
}

static void check_movec(Machine *a, Machine *b)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned r1 = DSP_REG_M0; r1 <= DSP_REG_SR; r1++) {
            if (r1 > DSP_REG_M7 && r1 != DSP_REG_SR) {
                continue;
            }
            for (unsigned r2 = DSP_REG_X0; r2 <= DSP_REG_M7; r2++) {
                for (unsigned dir = 0; dir < 2; dir++) {
                    for (unsigned seed = 0; seed < 16; seed++) {
                        check(a, b, 0x0440A0 | (dir << 15) | (r2 << 8) | r1,
                              0, seed, mode, false);
                        if (dir && r1 == DSP_REG_SR) {
                            check(a, b, 0x0440A0 | (dir << 15) | (r2 << 8) | r1,
                                  0, seed | 0x40000, mode, false);
                        }
                    }
                }
            }
        }
    }
}

/* Manual 13-68/OR: unsigned short source, only A1/B1 changes;
 * N/Z follow the 24-bit result, V clears, other CCR bits are preserved. */
static void check_immediate_logic(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t values[] = {0, 1, 31, 63, 0x7FFFFF, 0x800000, 0xFFFFFF, 0x123456};
    static const uint32_t flags[] = {0, 0xFF, 0x55, 0xAA};
    for (unsigned exclusive = 0; exclusive < 2; exclusive++) {
        for (unsigned long_form = 0; long_form < 2; long_form++) {
            for (unsigned d = 0; d < 2; d++) {
                unsigned sources = long_form ? ARRAY_SIZE(values) : 64;
                for (unsigned i = 0; i < sources; i++) {
                    uint32_t src = long_form ? values[i] : i;
                    uint32_t op = (long_form ? 0x0140C2 : 0x014082 | (src << 8)) |
                        exclusive | (d << 3);
                    for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                        for (unsigned f = 0; f < ARRAY_SIZE(flags); f++) {
                            for (unsigned repeated = 0; repeated < 2; repeated++) {
                                uint32_t result = exclusive ? values[v] ^ src : values[v] | src;
                                if (repeated && exclusive) result = values[v];
                                uint32_t sr = (mode << DSP_SR_S0) | flags[f];
                                uint32_t expected = (sr & ~0xEu) |
                                    ((result >> 23) << DSP_SR_N) | ((result == 0) << DSP_SR_Z);
                                Machine *machines[] = {a, b};
                                dspjit_test_reset_fallback_count();
                                for (unsigned engine = 0; engine < 2; engine++) {
                                    Machine *m = machines[engine];
                                    prepare(m, op, src, 1, mode, false);
                                    dsp_core_t *c = m->dsp->backend;
                                    c->registers[DSP_REG_SR] = sr;
                                    c->registers[DSP_REG_A0 + d] = 0x123456;
                                    c->registers[DSP_REG_A1 + d] = values[v];
                                    c->registers[DSP_REG_A2 + d] = 0xAB;
                                    if (repeated) {
                                        uint32_t program[] = {0x0602A0, op, src, 0x08F484, 1};
                                        if (!long_form) {
                                            program[2] = 0x08F484;
                                            program[3] = 1;
                                        }
                                        for (unsigned pc = 0; pc < ARRAY_SIZE(program); pc++)
                                            m->dsp->ops->write_memory(m->dsp, 'P', pc, program[pc]);
                                        assert(run_frame(m, 100000, engine != 0) == 0);
                                    } else if (engine == 0) {
                                        m->dsp->ops->step(m->dsp);
                                        c->cycle_count += c->instr_cycle;
                                    } else m->dsp->ops->run(m->dsp, 1);
                                    if (c->registers[DSP_REG_A1 + d] != result ||
                                        c->registers[DSP_REG_A0 + d] != 0x123456 ||
                                        c->registers[DSP_REG_A2 + d] != 0xAB ||
                                        c->registers[DSP_REG_SR] != expected) {
                                        fprintf(stderr, "Immediate logic engine=%u xor=%u long=%u d=%u src=%x old=%x rep=%u result=%x/%x sr=%x/%x\n",
                                                engine, exclusive, long_form, d, src, values[v], repeated,
                                                c->registers[DSP_REG_A1 + d], result,
                                                c->registers[DSP_REG_SR], expected);
                                        exit(1);
                                    }
                                }
                                assert(!dspjit_test_fallback_count());
                                assert(compare_machines(a, b) == 0);
                                cases++;
                            }
                        }
                    }
                }
            }
        }
    }
}

/* Walk one bit at a time, independently of either engine's shift helpers. */
static void check_logical_shift_immediates(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t words[] = {0, 1, 2, 0x800000, 0xFFFFFF, 0x7FFFFF, 0x123456, 0x800001};
    static const uint32_t flags[] = {0, 0xFF, 0x55, 0xAA};
    for (unsigned right = 0; right < 2; right++) {
        for (unsigned d = 0; d < 2; d++) {
            for (unsigned shift = 0; shift < 32; shift++) {
                uint32_t op = (right ? 0x0C1EC0 : 0x0C1E80) | (shift << 1) | d;
                for (unsigned w = 0; w < ARRAY_SIZE(words); w++) {
                    for (unsigned f = 0; f < ARRAY_SIZE(flags); f++) {
                        for (unsigned repeated = 0; repeated < 2; repeated++) {
                            uint32_t result = words[w], carry = 0;
                            for (unsigned iteration = 0; iteration <= repeated; iteration++) {
                                carry = 0;
                                for (unsigned bit = 0; bit < shift; bit++) {
                                    carry = right ? result & 1 : result >> 23;
                                    result = right ? result >> 1 : (result * 2) & 0xFFFFFF;
                                }
                            }
                            uint32_t sr = (mode << DSP_SR_S0) | flags[f];
                            uint32_t expected = (sr & ~0xFu) | carry |
                                ((result >> 23) << DSP_SR_N) | ((result == 0) << DSP_SR_Z);
                            Machine *machines[] = {a, b};
                            dspjit_test_reset_fallback_count();
                            for (unsigned engine = 0; engine < 2; engine++) {
                                Machine *m = machines[engine];
                                prepare(m, op, 0, 1, mode, false);
                                dsp_core_t *c = m->dsp->backend;
                                c->registers[DSP_REG_SR] = sr;
                                c->registers[DSP_REG_A0 + d] = 0x123456;
                                c->registers[DSP_REG_A1 + d] = words[w];
                                c->registers[DSP_REG_A2 + d] = 0xAB;
                                if (repeated) {
                                    uint32_t program[] = {0x0602A0, op, 0x08F484, 1};
                                    for (unsigned pc = 0; pc < ARRAY_SIZE(program); pc++)
                                        m->dsp->ops->write_memory(m->dsp, 'P', pc, program[pc]);
                                    assert(run_frame(m, 100000, engine != 0) == 0);
                                } else if (engine == 0) {
                                    m->dsp->ops->step(m->dsp);
                                    c->cycle_count += c->instr_cycle;
                                } else m->dsp->ops->run(m->dsp, 1);
                                if (c->registers[DSP_REG_A1 + d] != result ||
                                    c->registers[DSP_REG_A0 + d] != 0x123456 ||
                                    c->registers[DSP_REG_A2 + d] != 0xAB ||
                                    c->registers[DSP_REG_SR] != expected) {
                                    fprintf(stderr, "Logical shift engine=%u right=%u d=%u count=%u old=%x rep=%u result=%x/%x sr=%x/%x\n",
                                            engine, right, d, shift, words[w], repeated,
                                            c->registers[DSP_REG_A1 + d], result,
                                            c->registers[DSP_REG_SR], expected);
                                    exit(1);
                                }
                            }
                            assert(!dspjit_test_fallback_count());
                            assert(compare_machines(a, b) == 0);
                            cases++;
                        }
                    }
                }
            }
        }
    }
}

/* LRA adds the instruction's current PC, not the following PC or a
 * stale block-entry PC. Register writes retain the core's width rules. */
static void check_pc_relative_loads(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t pcs[] = {0, 1, 0x30, 0x7FE};
    static const uint32_t displacements[] = {0, 1, 0x7FFF, 0x8000, 0x7FFFFF, 0x800000, 0xFFFFE0, 0xFFFFFF};
    static const uint32_t flags[] = {0, 0xFF, 0x100FF, 0x2080};
    for (unsigned form = 0; form < 9; form++) {
        for (unsigned dst = 0; dst < 32; dst++) {
            uint32_t op = form ? 0x04C000 | ((form - 1) << 8) | dst : 0x044040 | dst;
            for (unsigned p = 0; p < ARRAY_SIZE(pcs); p++) {
                for (unsigned x = 0; x < ARRAY_SIZE(displacements); x++) {
                    uint32_t value = (pcs[p] + displacements[x]) & 0xFFFFFF;
                    for (unsigned f = 0; f < ARRAY_SIZE(flags); f++) {
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, displacements[x], 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->pc = pcs[p];
                            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | flags[f];
                            c->registers[dst] = 0xABCDEF;
                            if (form) c->registers[DSP_REG_R0 + form - 1] = displacements[x];
                            for (unsigned i = 0; i < 2; i++) {
                                c->registers[DSP_REG_A0 + i] = 0x123456;
                                c->registers[DSP_REG_A1 + i] = 0x789ABC;
                                c->registers[DSP_REG_A2 + i] = 0xAB;
                            }
                            uint32_t expected[DSP_REG_MAX];
                            memcpy(expected, c->registers, sizeof(expected));
                            if (dst == DSP_REG_A || dst == DSP_REG_B) {
                                unsigned d = dst & 1;
                                expected[DSP_REG_A0 + d] = 0;
                                expected[DSP_REG_A1 + d] = value;
                                expected[DSP_REG_A2 + d] = value & 0x800000 ? 0xFF : 0;
                            } else {
                                uint32_t mask = dst < DSP_REG_X0 ? 0 :
                                    dst == DSP_REG_A2 || dst == DSP_REG_B2 ? 0xFF :
                                    dst >= DSP_REG_R0 ? 0xFFFF : 0xFFFFFF;
                                expected[dst] = value & mask;
                            }
                            m->dsp->ops->write_memory(m->dsp, 'P', pcs[p], op);
                            if (!form) m->dsp->ops->write_memory(m->dsp, 'P', pcs[p] + 1, displacements[x]);
                            if (!engine) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);
                            if (memcmp(c->registers, expected, sizeof(expected)) ||
                                c->pc != pcs[p] + (form ? 1 : 2) || c->cycle_count != 2) {
                                fprintf(stderr, "LRA engine=%u form=%u dst=%u pc=%x disp=%x value=%x actual=%x sr=%x/%x\n",
                                        engine, form, dst, pcs[p], displacements[x], value,
                                        c->registers[dst], c->registers[DSP_REG_SR], expected[DSP_REG_SR]);
                                exit(1);
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) {
                            fputs("LRA state/fallback regression\n", stderr);
                            exit(1);
                        }
                        cases++;
                    }
                }
            }
        }
    }
}

static void check_logical_immediates(Machine *a, Machine *b)
{
    static const uint32_t words[] = {
        0, 1, 2, 0x800000, 0xFFFFFF, 0x7FFFFF, 0x123456, 0x800001
    };
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned d = 0; d < 2; d++) {
            for (unsigned shift = 0; shift < 32; shift++) {
                for (unsigned carry = 0; carry < 2; carry++) {
                    for (unsigned w = 0; w < ARRAY_SIZE(words); w++) {
                        uint32_t word = words[w];
                        uint32_t sr = 0xFE | (mode << DSP_SR_S0) | carry;
                        uint32_t result = shift >= 24 ? 0 : (word << shift) & 0xFFFFFF;
                        unsigned c = shift && shift <= 24 ? (word >> (24 - shift)) & 1 : 0;
                        uint32_t expected_sr = (sr & ~0xF) | c |
                            ((result >> 23) << DSP_SR_N) | ((result == 0) << DSP_SR_Z);
                        Machine *machines[] = { a, b };
                        dspjit_test_reset_fallback_count();
                        for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
                            Machine *m = machines[i];
                            prepare(m, 0x0C1E80 | (shift << 1) | d, 0, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->registers[DSP_REG_SR] = sr;
                            c->registers[DSP_REG_A0 + d] = 0x123456;
                            c->registers[DSP_REG_A1 + d] = word;
                            c->registers[DSP_REG_A2 + d] = 0xAB;
                            if (i == 0) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else {
                                m->dsp->ops->run(m->dsp, 1);
                            }
                            if (c->registers[DSP_REG_A1 + d] != result ||
                                c->registers[DSP_REG_A0 + d] != 0x123456 ||
                                c->registers[DSP_REG_A2 + d] != 0xAB ||
                                c->registers[DSP_REG_SR] != expected_sr) {
                                fprintf(stderr, "LSL immediate regression: engine=%u shift=%u d=%u word=%06x\n",
                                        i, shift, d, word);
                                exit(1);
                            }
                        }
                        assert(dspjit_test_fallback_count() == 0);
                        assert(compare_machines(a, b) == 0);
                        cases++;
                    }
                }
            }
            for (unsigned seed = 0; seed < 64; seed++) {
                check(a, b, 0x014086 | (seed << 8) | (d << 3), 0, seed, mode, false);
                for (unsigned w = 0; w < ARRAY_SIZE(words); w++) {
                    check(a, b, 0x0140C2 | (d << 3), words[w], seed, mode, false);
                }
            }
        }
    }
}

/* Independent arithmetic/flags oracle, including negative long immediates.
 * Do not use the interpreter's subtraction or CCR helpers here. */
static void check_compare_immediates(Machine *a, Machine *b, unsigned mode)
{
    static const uint64_t accum[] = {
        0, 1, 0xFFFFFF, 0x1000000, 0xFFFFFFFFFFFFFF,
        0x7FFFFFFFFFFFFF, 0x80000000000000, 0x80000000000001,
        0xFFFFFF00000000, 0x007FFFFF123456, 0x00800000800000, 0xFF000000FFFFFF
    };
    static const uint32_t words[] = {
        0, 1, 2, 31, 32, 63, 0xFFFF, 0x7FFFFE, 0x7FFFFF,
        0x800000, 0x800001, 0xFFFFFE, 0xFFFFFF
    };
    static const uint32_t status[] = {0, 0xFF, 0x100FF, 0x2080};
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF);
    const uint64_t sign = UINT64_C(0x80000000000000);
    for (unsigned d = 0; d < 2; d++) {
        for (unsigned n = 0; n < ARRAY_SIZE(accum); n++) {
            for (unsigned w = 0; w < ARRAY_SIZE(words); w++) {
                for (unsigned short_form = 0; short_form < 2; short_form++) {
                    if (short_form && words[w] > 63) continue;
                    uint32_t op = short_form ?
                        0x014085 | (words[w] << 8) | (d << 3) : 0x0140C5 | (d << 3);
                    uint64_t src = (uint64_t)words[w] << 24;
                    if (!short_form && (words[w] & 0x800000)) src |= UINT64_C(0xFF000000000000);
                    uint64_t dst = accum[n], result = (dst - src) & mask;
                    bool overflow = ((dst ^ src) & (dst ^ result) & sign) != 0;
                    for (unsigned f = 0; f < ARRAY_SIZE(status); f++) {
                        uint32_t sr = status[f] | (mode << DSP_SR_S0);
                        uint32_t expected = (sr & 0xFFFF & ~0x3F) |
                            (dst < src) | (overflow << DSP_SR_V) |
                            (overflow << DSP_SR_L);
                        if (mode != 3) {
                            unsigned eb = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                            uint64_t upper = result >> eb;
                            bool extension = upper != 0 && upper != (mask >> eb);
                            bool unnormalized = ((result >> (eb - 1)) & 3) == 0 ||
                                                ((result >> (eb - 1)) & 3) == 3;
                            expected |= (extension << DSP_SR_E) | (unnormalized << DSP_SR_U) |
                                ((result == 0) << DSP_SR_Z) | (((result >> 55) & 1) << DSP_SR_N);
                        }
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, words[w], 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->registers[DSP_REG_SR] = sr;
                            c->registers[DSP_REG_A0 + d] = dst & 0xFFFFFF;
                            c->registers[DSP_REG_A1 + d] = (dst >> 24) & 0xFFFFFF;
                            c->registers[DSP_REG_A2 + d] = dst >> 48;
                            if (!engine) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else {
                                m->dsp->ops->run(m->dsp, 1);
                            }
                            if (c->registers[DSP_REG_SR] != expected ||
                                c->registers[DSP_REG_A0 + d] != (dst & 0xFFFFFF) ||
                                c->registers[DSP_REG_A1 + d] != ((dst >> 24) & 0xFFFFFF) ||
                                c->registers[DSP_REG_A2 + d] != (dst >> 48)) {
                                fprintf(stderr, "CMP immediate: engine=%u d=%u short=%u src=%06x dst=%014llx sr=%x expected=%x got=%x\n",
                                    engine, d, short_form, words[w], (unsigned long long)dst,
                                    sr, expected, c->registers[DSP_REG_SR]);
                                exit(1);
                            }
                        }
                        assert(dspjit_test_fallback_count() == 0);
                        assert(compare_machines(a, b) == 0);
                        cases++;
                    }
                }
            }
        }
    }
    /* Post-processing when a two-word compare lands on a DO boundary. */
    for (unsigned seed = 0; seed < 16; seed++) {
        for (unsigned d = 0; d < 2; d++) {
            for (unsigned w = 0; w < ARRAY_SIZE(words); w++) {
                check(a, b, 0x0140C5 | (d << 3), words[w],
                      seed | 0x10000 | 0x40000, mode, false);
            }
        }
    }
}

static void check_inc_dec_words(Machine *a, Machine *b, unsigned mode)
{
    static const uint64_t values[] = {0, 1, 0xFFFFFF, 0x1000000,
        0xFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFF, 0x80000000000000, 0x80000000000001};
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF), sign = UINT64_C(0x80000000000000);
    for (unsigned d = 0; d < 2; d++) for (unsigned sub = 0; sub < 2; sub++) {
        for (unsigned n = 0; n < ARRAY_SIZE(values); n++) for (unsigned sticky = 0; sticky < 2; sticky++) {
            uint64_t v = values[n], result = (sub ? v - 1 : v + 1) & mask;
            bool overflow = sub ? v == sign : v == sign - 1;
            unsigned expected = ((sub ? v == 0 : v == mask) << DSP_SR_C) |
                                (overflow << DSP_SR_V) | ((sticky || overflow) << DSP_SR_L);
            Machine *machines[] = {a, b};
            dspjit_test_reset_fallback_count();
            for (unsigned engine = 0; engine < 2; engine++) {
                Machine *m = machines[engine];
                prepare(m, (sub ? 0xA : 8) | d, 0, 1, mode, false);
                dsp_core_t *c = m->dsp->backend;
                c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (sticky << DSP_SR_L);
                c->registers[DSP_REG_A0 + d] = v & 0xFFFFFF;
                c->registers[DSP_REG_A1 + d] = (v >> 24) & 0xFFFFFF;
                c->registers[DSP_REG_A2 + d] = v >> 48;
                if (!engine) { m->dsp->ops->step(m->dsp); c->cycle_count += c->instr_cycle; }
                else m->dsp->ops->run(m->dsp, 1);
                uint64_t actual = c->registers[DSP_REG_A0 + d] |
                    ((uint64_t)c->registers[DSP_REG_A1 + d] << 24) |
                    ((uint64_t)c->registers[DSP_REG_A2 + d] << 48);
                assert(actual == result && (c->registers[DSP_REG_SR] & 0x43) == expected);
            }
            assert(!dspjit_test_fallback_count() && !compare_machines(a, b));
            cases++;
        }
    }
}

static void check_effective_control_stores(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
    for (unsigned reg = DSP_REG_M0; reg < DSP_REG_MAX; reg++) {
        for (unsigned ea = 0; ea < 64; ea++) {
            if ((ea >> 3) == 6 && (ea & 7) != 0 && (ea & 7) != 4) continue;
            for (unsigned space = 0; space < 2; space++) {
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned seed = 0; seed < 2; seed++) {
                        check(a, b, 0x054020 | (ea << 8) | (space << 6) | reg,
                              space || seed ? 0x30 : 0xFFFF85,
                              0x400000 | 0x1000000 | contexts[ctx] | (seed ? 9 : 0),
                              mode, seed != 0);
                    }
                }
            }
        }
    }
}

static void check_register_control_transfers(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
    for (unsigned control = DSP_REG_M0; control < DSP_REG_MAX; control++) {
        for (unsigned general = 0; general < DSP_REG_MAX; general++) {
            for (unsigned dir = 0; dir < 2; dir++) {
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned seed = 0; seed < 2; seed++) {
                        check(a, b, 0x0440A0 | (dir << 15) | (general << 8) | control,
                              0x30, 0x400000 | 0x1000000 | 0x2000000 |
                              contexts[ctx] | (seed ? 9 : 0), mode, seed != 0);
                    }
                }
            }
        }
    }
}

static void check_peripheral_control_transfers(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
    for (unsigned reg = 0; reg < DSP_REG_MAX; reg++) {
        if (reg >= DSP_REG_X0 && reg <= DSP_REG_M7) continue;
        for (unsigned port = 0; port < 64; port++) {
            for (unsigned write = 0; write < 2; write++) {
                if (write && port == 0x16) continue;
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned seed = 0; seed < 2; seed++) {
                        check(a, b, 0x084000 | (write << 15) | (reg << 8) | port, 0x30,
                              0x400000 | 0x1000000 | 0x2000000 | contexts[ctx] |
                              (seed ? 9 : 0), mode, seed != 0);
                    }
                }
            }
        }
        for (unsigned action = 0; action < 2; action++) {
            check(a, b, 0x08C016 | (reg << 8), 0x30,
                  0x80000000u | 0x400000 | 0x20000000 | (action << 8), mode, false);
        }
    }
}

static void check_immediate_multiply_kind(Machine *a, Machine *b, unsigned mode, bool rounded)
{
    static const unsigned regs[] = {DSP_REG_X0, DSP_REG_Y0, DSP_REG_X1, DSP_REG_Y1};
    static const uint32_t words[] = {
        0, 1, 2, 0x7FFFFF, 0x800000, 0xFFFFFF,
        0x400000, 0xC00000, 0x123456, 0xABCD01, 0x80, 0xFFFF80, 0x200000, 0x600000
    };
    unsigned word_count = ARRAY_SIZE(words) - (rounded ? 0 : 2);
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF);
    for (unsigned q = 0; q < 4; q++) {
        for (unsigned d = 0; d < 2; d++) {
            for (unsigned minus = 0; minus < 2; minus++) {
                uint32_t op = 0x0141C0 | rounded | (q << 4) | (d << 3) | (minus << 2);
                for (unsigned x = 0; x < word_count; x++) {
                    for (unsigned y = 0; y < word_count; y++) {
                        int64_t sx = words[x] & 0x800000 ? (int64_t)words[x] - 0x1000000 : words[x];
                        int64_t sy = words[y] & 0x800000 ? (int64_t)words[y] - 0x1000000 : words[y];
                        int64_t product = sx * sy * 2;
                        if (minus) product = -product;
                        if (rounded) {
                            /* Independent nearest-even oracle using quotient/remainder,
                             * including negative values; no emulator rounding helpers. */
                            int64_t unit = INT64_C(1) << (mode & 1 ? 25 : mode == 2 ? 23 : 24);
                            int64_t quotient = product / unit, remainder = product % unit;
                            if (remainder < 0) { quotient--; remainder += unit; }
                            if (remainder > unit / 2 ||
                                (remainder == unit / 2 && quotient % 2 != 0)) quotient++;
                            product = quotient * unit;
                        }
                        uint64_t result = (uint64_t)product & mask;
                        for (unsigned carry = 0; carry < 2; carry++) {
                            for (unsigned sticky = 0; sticky < 2; sticky++) {
                                uint32_t sr = (mode << DSP_SR_S0) | 0x80 | 0x3E |
                                    (sticky << DSP_SR_L) | carry | (1u << DSP_SR_FV);
                                uint32_t expected = sr & 0xFFFF & ~0x3E;
                                if (mode != 3) {
                                    unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                                    int64_t limit = INT64_C(1) << bit;
                                    bool extension = product < -limit || product >= limit;
                                    unsigned top = (result >> (bit - 1)) & 3;
                                    expected |= (extension << DSP_SR_E) |
                                        ((top == 0 || top == 3) << DSP_SR_U) |
                                        ((product == 0) << DSP_SR_Z) | ((product < 0) << DSP_SR_N);
                                }
                                Machine *machines[] = {a, b};
                                dspjit_test_reset_fallback_count();
                                for (unsigned engine = 0; engine < 2; engine++) {
                                    Machine *m = machines[engine];
                                    prepare(m, op, words[x], 1, mode, false);
                                    dsp_core_t *c = m->dsp->backend;
                                    c->registers[regs[q]] = words[y];
                                    c->registers[DSP_REG_SR] = sr;
                                    if (engine == 0) {
                                        m->dsp->ops->step(m->dsp);
                                        c->cycle_count += c->instr_cycle;
                                    } else m->dsp->ops->run(m->dsp, 1);
                                    uint64_t actual = c->registers[DSP_REG_A0 + d] |
                                        ((uint64_t)c->registers[DSP_REG_A1 + d] << 24) |
                                        ((uint64_t)c->registers[DSP_REG_A2 + d] << 48);
                                    if (actual != result || c->registers[DSP_REG_SR] != expected) {
                                        fprintf(stderr, "Immediate multiply engine=%u round=%u mode=%u q=%u d=%u minus=%u x=%x y=%x result=%llx/%llx sr=%x/%x\n",
                                                engine, rounded, mode, q, d, minus, words[x], words[y],
                                                (unsigned long long)actual, (unsigned long long)result,
                                                c->registers[DSP_REG_SR], expected);
                                        exit(1);
                                    }
                                }
                                assert(!dspjit_test_fallback_count());
                                assert(compare_machines(a, b) == 0);
                                cases++;
                            }
                        }
                    }
                    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        for (unsigned seed = 0; seed < 8; seed++) {
                            check(a, b, op, words[x], 0x400000 | 0x1000000 | 0x10000 |
                                  contexts[ctx] | (seed << 1) | (seed << 4), mode, seed & 1);
                        }
                    }
                }
            }
        }
    }
}

static void check_immediate_multiply(Machine *a, Machine *b, unsigned mode)
{
    check_immediate_multiply_kind(a, b, mode, false);
}

static void check_immediate_rounded_multiply(Machine *a, Machine *b, unsigned mode)
{
    check_immediate_multiply_kind(a, b, mode, true);
}

static void check_norm_carry_consumers(Machine *a, Machine *b, unsigned mode)
{
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF);
    Machine *machines[] = {a, b};
    for (unsigned path = 0; path < 5; path++) {
        for (unsigned carry = 0; carry < 2; carry++) {
            uint32_t program[] = {0x01D815, 0x200021, 0x08F484, 1, 0};
            if (path == 1) {
                program[0] = 0x0603A0;
                program[1] = 0x01D815;
                program[2] = 0x200021;
                program[3] = 0x08F484;
                program[4] = 1;
            }
            uint64_t initial = path == 2 ? 1 : path == 3 ? 2 : path == 4 ? mask : 0;
            uint64_t normalized = path == 2 ? 2 : path == 3 ? 1 : initial;
            uint64_t expected = (normalized + carry) & mask;
            for (unsigned engine = 0; engine < 2; engine++) {
                Machine *m = machines[engine];
                prepare(m, program[0], 0, 1, mode, false);
                dsp_core_t *c = m->dsp->backend;
                c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | carry |
                    (path < 2 ? 1u << DSP_SR_Z : path == 2 ? 1u << DSP_SR_U : 1u << DSP_SR_E);
                c->registers[DSP_REG_A0] = initial & 0xFFFFFF;
                c->registers[DSP_REG_A1] = (initial >> 24) & 0xFFFFFF;
                c->registers[DSP_REG_A2] = initial >> 48;
                c->registers[DSP_REG_R0] = 0x40;
                c->registers[DSP_REG_X0] = c->registers[DSP_REG_X1] = 0;
                for (unsigned pc = 0; pc < ARRAY_SIZE(program); pc++)
                    m->dsp->ops->write_memory(m->dsp, 'P', pc, program[pc]);
                dspjit_test_reset_fallback_count();
                assert(run_frame(m, 100000, engine != 0) == 0);
                assert(!dspjit_test_fallback_count());
                uint64_t got = c->registers[DSP_REG_A0] |
                    ((uint64_t)c->registers[DSP_REG_A1] << 24) |
                    ((uint64_t)c->registers[DSP_REG_A2] << 48);
                if (got != expected) {
                    fprintf(stderr, "NORM carry consumer: engine=%u path=%u carry=%u result=%llx/%llx\n",
                            engine, path, carry, (unsigned long long)got, (unsigned long long)expected);
                    exit(1);
                }
                assert(c->registers[DSP_REG_R0] == (path < 2 ? 0x40 : path == 2 ? 0x3F : 0x41));
            }
            assert(compare_machines(a, b) == 0);
            cases++;
        }
    }
}

static void check_normalize(Machine *a, Machine *b, unsigned mode)
{
    check_norm_carry_consumers(a, b, mode);
    static const uint64_t values[] = {
        0, 1, 0xFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFF, 0x80000000000000,
        0x40000000000000, 0xC0000000000000, 0x00000080000000,
        0xFFFFFF00000000, 0x0123456789ABCD, 0xFEDCBA98765432, 0x08000000000000
    };
    static const unsigned addresses[] = {0, 1, 0xFFFF, 0x1234};
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF);
    for (unsigned n = 0; n < 8; n++) {
        for (unsigned d = 0; d < 2; d++) {
            uint32_t op = 0x01D815 | (n << 8) | (d << 3);
            for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                for (unsigned f = 0; f < 8; f++) {
                    bool e = f & 1, u = f & 2, z = f & 4;
                    bool left = !e && u && !z, right = e;
                    uint64_t old = values[v];
                    uint64_t result = left ? (old * 2) & mask : right ? (old >> 1) | (old & UINT64_C(0x80000000000000)) : old;
                    bool overflow = left && ((old ^ result) & (UINT64_C(1) << 55));
                    for (unsigned ar = 0; ar < ARRAY_SIZE(addresses); ar++) {
                        unsigned address = (addresses[ar] + (right ? 1 : left ? -1 : 0)) & 0xFFFF;
                        for (unsigned carry = 0; carry < 2; carry++) {
                            for (unsigned sticky = 0; sticky < 2; sticky++) {
                                unsigned sr = (mode << DSP_SR_S0) | 0x8A | carry |
                                    (sticky << DSP_SR_L) | (e << DSP_SR_E) |
                                    (u << DSP_SR_U) | (z << DSP_SR_Z) | (1u << DSP_SR_FV);
                                unsigned expected = (sr & 0xFFFF & ~0x3E) |
                                    (overflow << DSP_SR_V) | (overflow << DSP_SR_L);
                                if (mode != 3) {
                                    unsigned bit = mode == 1 ? 48 : mode == 2 ? 46 : 47;
                                    uint64_t upper = result >> bit;
                                    unsigned top = (result >> (bit - 1)) & 3;
                                    expected |= ((upper != 0 && upper != (mask >> bit)) << DSP_SR_E) |
                                        ((top == 0 || top == 3) << DSP_SR_U) |
                                        ((result == 0) << DSP_SR_Z) | (((result >> 55) & 1) << DSP_SR_N);
                                }
                                Machine *machines[] = {a, b};
                                dspjit_test_reset_fallback_count();
                                for (unsigned engine = 0; engine < 2; engine++) {
                                    Machine *m = machines[engine];
                                    prepare(m, op, 0x30, 1, mode, false);
                                    dsp_core_t *c = m->dsp->backend;
                                    c->registers[DSP_REG_A0 + d] = old & 0xFFFFFF;
                                    c->registers[DSP_REG_A1 + d] = (old >> 24) & 0xFFFFFF;
                                    c->registers[DSP_REG_A2 + d] = old >> 48;
                                    c->registers[DSP_REG_R0 + n] = addresses[ar];
                                    c->registers[DSP_REG_M0 + n] = n & 1 ? 0x3F : 0;
                                    c->registers[DSP_REG_N0 + n] = 0x1234;
                                    c->registers[DSP_REG_SR] = sr;
                                    if (engine == 0) {
                                        m->dsp->ops->step(m->dsp);
                                        c->cycle_count += c->instr_cycle;
                                    } else m->dsp->ops->run(m->dsp, 1);
                                    uint64_t actual = c->registers[DSP_REG_A0 + d] |
                                        ((uint64_t)c->registers[DSP_REG_A1 + d] << 24) |
                                        ((uint64_t)c->registers[DSP_REG_A2 + d] << 48);
                                    if (actual != result || c->registers[DSP_REG_SR] != expected ||
                                        c->registers[DSP_REG_R0 + n] != address) {
                                        fprintf(stderr, "NORM engine=%u n=%u d=%u v=%014llx f=%u mode=%u result=%014llx/%014llx sr=%x/%x Rn=%x/%x\n",
                                                engine, n, d, (unsigned long long)old, f, mode,
                                                (unsigned long long)actual, (unsigned long long)result,
                                                c->registers[DSP_REG_SR], expected,
                                                c->registers[DSP_REG_R0 + n], address);
                                        exit(1);
                                    }
                                }
                                assert(!dspjit_test_fallback_count());
                                assert(compare_machines(a, b) == 0);
                                cases++;
                            }
                        }
                    }
                }
            }
            static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
            for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                for (unsigned seed = 0; seed < 16; seed++) {
                    check(a, b, op, 0x30, 0x400000 | 0x20000 | 0x1000000 | 0x10000 |
                          contexts[ctx] | seed | (seed << 4), mode, seed & 1);
                }
            }
        }
    }
}

static void check_divide_step(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned regs[] = {DSP_REG_X0, DSP_REG_Y0, DSP_REG_X1, DSP_REG_Y1};
    static const uint64_t values[] = {
        0, 1, 0xFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFF, 0x80000000000000,
        0x40000000000000, 0xC0000000000000, 0x00000080000000,
        0xFFFFFF00000000, 0x0123456789ABCD, 0xFEDCBA98765432, 0x08000000000000
    };
    static const uint32_t words[] = {0, 1, 2, 0x7FFFFF, 0x800000, 0xFFFFFF, 0x400000, 0xC00000};
    static const unsigned flags[] = {0, 0x3C, 0x14, 0x28};
    const uint64_t mask = UINT64_C(0xFFFFFFFFFFFFFF), sign = UINT64_C(1) << 55;
    for (unsigned q = 0; q < 4; q++) {
        for (unsigned d = 0; d < 2; d++) {
            uint32_t op = 0x018040 | (q << 4) | (d << 3);
            for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                for (unsigned w = 0; w < ARRAY_SIZE(words); w++) {
                    uint64_t old = values[v], shifted = (old * 2) & mask;
                    int64_t signed_word = words[w] & 0x800000 ? (int64_t)words[w] - 0x1000000 : words[w];
                    uint64_t source = (uint64_t)(signed_word * 0x1000000) & mask;
                    bool add = ((old ^ source) & sign) != 0;
                    uint64_t partial = (add ? shifted + source : shifted - source) & mask;
                    bool overflow = ((old ^ shifted) & sign) != 0;
                    for (unsigned f = 0; f < ARRAY_SIZE(flags); f++) {
                        for (unsigned carry = 0; carry < 2; carry++) {
                            uint64_t result = partial | carry;
                            for (unsigned sticky = 0; sticky < 2; sticky++) {
                                unsigned sr = (mode << DSP_SR_S0) | 0x82 | flags[f] | carry |
                                    (sticky << DSP_SR_L) | (1u << DSP_SR_FV);
                                unsigned expected = (sr & 0xFFFF & ~3u) |
                                    ((result & sign) == 0) | (overflow << DSP_SR_V) |
                                    (overflow << DSP_SR_L);
                                Machine *machines[] = {a, b};
                                dspjit_test_reset_fallback_count();
                                for (unsigned engine = 0; engine < 2; engine++) {
                                    Machine *m = machines[engine];
                                    prepare(m, op, 0x30, 1, mode, false);
                                    dsp_core_t *c = m->dsp->backend;
                                    c->registers[DSP_REG_A0 + d] = old & 0xFFFFFF;
                                    c->registers[DSP_REG_A1 + d] = (old >> 24) & 0xFFFFFF;
                                    c->registers[DSP_REG_A2 + d] = old >> 48;
                                    c->registers[regs[q]] = words[w];
                                    c->registers[DSP_REG_SR] = sr;
                                    if (engine == 0) {
                                        m->dsp->ops->step(m->dsp);
                                        c->cycle_count += c->instr_cycle;
                                    } else m->dsp->ops->run(m->dsp, 1);
                                    uint64_t actual = c->registers[DSP_REG_A0 + d] |
                                        ((uint64_t)c->registers[DSP_REG_A1 + d] << 24) |
                                        ((uint64_t)c->registers[DSP_REG_A2 + d] << 48);
                                    if (actual != result || c->registers[DSP_REG_SR] != expected) {
                                        fprintf(stderr, "DIV engine=%u q=%u d=%u old=%014llx word=%06x sr=%x result=%014llx/%014llx flags=%x/%x\n",
                                                engine, q, d, (unsigned long long)old, words[w], sr,
                                                (unsigned long long)actual, (unsigned long long)result,
                                                c->registers[DSP_REG_SR], expected);
                                        exit(1);
                                    }
                                }
                                assert(!dspjit_test_fallback_count());
                                assert(compare_machines(a, b) == 0);
                                cases++;
                            }
                        }
                    }
                }
            }
            static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
            for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                for (unsigned seed = 0; seed < 16; seed++) {
                    check(a, b, op, 0x30, 0x400000 | 0x20000 | 0x1000000 | 0x10000 |
                          contexts[ctx] | seed | (seed << 4), mode, seed & 1);
                }
            }
        }
    }
}

static void check_status_byte_logic(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned status[] = {0, 0xFF, 0x1234, 0xAF7F};
    static const unsigned omrs[] = {0, 0xC7, 0x38, 0x1234FF};
    for (unsigned is_or = 0; is_or < 2; is_or++) {
        for (unsigned sel = 0; sel < 4; sel++) {
            for (unsigned value = 0; value < 256; value++) {
                uint32_t op = (is_or ? 0xF8 : 0xB8) | (value << 8) | sel;
                for (unsigned f = 0; f < ARRAY_SIZE(status); f++) {
                    unsigned sr = (status[f] & ~(3u << DSP_SR_S0)) |
                        (mode << DSP_SR_S0) | (1u << DSP_SR_FV);
                    unsigned expected_sr = sr, expected_omr = omrs[f];
                    if (sel == 0) expected_sr = is_or ? sr | (value << 8) : sr & ((value << 8) | 255);
                    if (sel == 1) expected_sr = is_or ? sr | value : sr & (0xFF00 | value);
                    if (sel == 2) expected_omr = is_or ? omrs[f] | value : omrs[f] & value;
                    Machine *machines[] = {a, b};
                    dspjit_test_reset_fallback_count();
                    for (unsigned engine = 0; engine < 2; engine++) {
                        Machine *m = machines[engine];
                        prepare(m, op, 0x30, 1, mode, false);
                        dsp_core_t *c = m->dsp->backend;
                        c->registers[DSP_REG_SR] = sr;
                        c->registers[DSP_REG_OMR] = omrs[f];
                        c->registers[DSP_REG_LA] = 0xFFF;
                        c->registers[DSP_REG_LC] = 2;
                        if (engine == 0) {
                            m->dsp->ops->step(m->dsp);
                            c->cycle_count += c->instr_cycle;
                        } else m->dsp->ops->run(m->dsp, 1);
                        if (c->registers[DSP_REG_SR] != expected_sr ||
                            c->registers[DSP_REG_OMR] != expected_omr) {
                            fprintf(stderr, "status byte engine=%u op=%06x mode=%u sr=%x/%x omr=%x/%x\n",
                                    engine, op, mode, c->registers[DSP_REG_SR], expected_sr,
                                    c->registers[DSP_REG_OMR], expected_omr);
                            exit(1);
                        }
                    }
                    assert(!dspjit_test_fallback_count());
                    assert(compare_machines(a, b) == 0);
                    cases++;
                }
                static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned seed = 0; seed < 2; seed++) {
                        check(a, b, op, 0x30, 0x400000 | 0x1000000 |
                              contexts[ctx] | (seed ? 9 : 0), mode, seed != 0);
                    }
                }
            }
        }
    }
}

static void check_memory_bits(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t codes[] = {0x0A0000, 0x0A0020, 0x0B0000, 0x0B0020};
    static const uint32_t values[] = {0, 0xFFFFFF, 0xAAAAAA, 0x555555, 1, 0x800000, 0x10000, 0x7FFFFF};
    static const unsigned status[] = {0, 0xFF, 0x1234, 0xAF7F};
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
    static const unsigned edge_ea[] = {0, 7, 24, 32, 40, 48, 52, 56};
    for (unsigned operation = 0; operation < 4; operation++) {
        for (unsigned bit = 0; bit < 16; bit++) {
            uint32_t mask = 1u << bit;
            for (unsigned space = 0; space < 2; space++) {
                uint32_t op = codes[operation] | (0x20 << 8) | (space << 6) | bit;
                for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
                    uint32_t old = values[v];
                    uint32_t result = operation == 0 ? old & ~mask : operation == 1 ? old | mask :
                                      operation == 2 ? old ^ mask : old;
                    for (unsigned f = 0; f < ARRAY_SIZE(status); f++) {
                        unsigned sr = (status[f] & ~(3u << DSP_SR_S0)) |
                            (mode << DSP_SR_S0) | (1u << DSP_SR_FV);
                        unsigned expected = (sr & 0xFFFE) | ((old >> bit) & 1);
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, 0x30, 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            uint32_t *ram = space ? c->yram : c->xram;
                            ram[0x20] = old;
                            c->registers[DSP_REG_SR] = sr;
                            c->registers[DSP_REG_LA] = 0xFFF;
                            c->registers[DSP_REG_LC] = 2;
                            if (engine == 0) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);
                            if (ram[0x20] != result || c->registers[DSP_REG_SR] != expected) {
                                fprintf(stderr, "memory bit engine=%u op=%06x old=%06x value=%06x/%06x sr=%x/%x\n",
                                        engine, op, old, ram[0x20], result, c->registers[DSP_REG_SR], expected);
                                exit(1);
                            }
                        }
                        assert(!dspjit_test_fallback_count());
                        assert(compare_machines(a, b) == 0);
                        cases++;
                    }
                }
                for (unsigned form = 0; form < 2; form++) {
                    for (unsigned addr = 0; addr < 64; addr++) {
                        if (form && (addr >> 3) == 6 && (addr & 7) != 0 && (addr & 7) != 4) continue;
                        for (unsigned seed = 0; seed < 2; seed++) {
                            check(a, b, codes[operation] | (form << 14) | (addr << 8) |
                                  (space << 6) | bit, 0x30,
                                  0x400000 | 0x1000000 | 0x20000000 | (seed ? 9 : 0), mode, seed != 0);
                        }
                    }
                }
                if (bit == 0 || bit == 15) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        for (unsigned seed = 0; seed < 2; seed++) {
                            for (unsigned i = 0; i < ARRAY_SIZE(edge_ea); i++) {
                                check(a, b, codes[operation] | 0x4000 | (edge_ea[i] << 8) |
                                      (space << 6) | bit, 0x30,
                                      0x400000 | 0x1000000 | contexts[ctx] | (seed ? 9 : 0), mode, seed != 0);
                            }
                            for (unsigned addr = 0; addr < 64; addr += 63) {
                                check(a, b, codes[operation] | (addr << 8) | (space << 6) | bit,
                                      0x30, 0x400000 | 0x1000000 | contexts[ctx] | (seed ? 9 : 0), mode, seed != 0);
                            }
                        }
                    }
                }
            }
            for (unsigned port = 0; port < 64; port++) {
                if (port == 0x16 && operation != 3) continue;
                for (unsigned seed = 0; seed < 2; seed++) {
                    check(a, b, codes[operation] | 0x8000 | (port << 8) | bit, 0x30,
                          0x400000 | 0x1000000 | 0x20000000 | (seed ? 9 : 0), mode, seed != 0);
                }
            }
            for (unsigned immediate = 0; immediate < 2; immediate++) {
                for (unsigned seed = 0; seed < 2; seed++) {
                    check(a, b, codes[operation] | 0x4000 | ((immediate ? 52 : 48) << 8) | bit,
                          0xFFFFC5, 0x400000 | 0x1000000 | 0x20000000 | (seed ? 9 : 0), mode, seed != 0);
                }
            }
            static const unsigned mix_addresses[] = {0x1401, 0xC01};
            for (unsigned i = 0; i < ARRAY_SIZE(mix_addresses); i++) {
                for (unsigned seed = 0; seed < 2; seed++) {
                    check(a, b, codes[operation] | 0x7000 | bit, mix_addresses[i],
                          0x400000 | 0x1000000 | 0x20000000 | (seed ? 9 : 0), mode, seed != 0);
                }
            }
        }
    }
    /* RMW to DMA control must encode a valid STOP or UNFREEZE action. */
    for (unsigned operation = 1; operation <= 2; operation++) {
        for (unsigned bit = 1; bit <= 2; bit++) {
            check(a, b, codes[operation] | 0x8000 | (0x16 << 8) | bit, 0x30,
                  0x400000 | 0x1000000 | 0x20000000 | 1, mode, false);
        }
    }
}

static void check_bitcall_decode(void)
{
    Machine *m = machine_new(false);
    prepare(m, 0, 0, 1, 0, false);
    dsp_core_t *c = m->dsp->backend;
    static const unsigned pcs[] = {0, 1, 0x7fe, DSP_PRAM_SIZE - 1};
    unsigned probes = 0;
    uint32_t registers[DSP_REG_MAX];
    memcpy(registers, c->registers, sizeof(registers));
    uint64_t reads = peripheral_read_count, writes = peripheral_write_count;
    for (unsigned indirect = 0; indirect < 2; indirect++) {
        for (unsigned address = 0; address < 64; address++) {
            bool accepted = !indirect || (address >> 3) != 6;
            for (unsigned space = 0; space < 2; space++) {
                for (unsigned set = 0; set < 2; set++) {
                    for (unsigned bit = 0; bit < 16; bit++) {
                        for (unsigned p = 0; p < ARRAY_SIZE(pcs); p++) {
                            unsigned pc = pcs[p];
                            uint32_t op = (indirect ? 0x0b4080 : 0x0b0080) |
                                          (address << 8) | (space << 6) | (set << 5) | bit;
                            c->pram[pc] = op;
                            if (pc + 1 < DSP_PRAM_SIZE) c->pram[pc + 1] = 0x30;
                            c->pc = 0x22;
                            DspDecoded decoded;
                            dsp56k_decode(c, pc, &decoded);
                            const char *name = indirect ? set ? "jsset #n, [X or Y]:ea, xxxx" : "jsclr #n, [X or Y]:ea, xxxx" :
                                                         set ? "jsset #n, [X or Y]:aa, xxxx" : "jsclr #n, [X or Y]:aa, xxxx";
                            if (!accepted) name = "move Y:(Rn + xxxx) <-> R";
                            if (c->pc != 0x22 || memcmp(registers, c->registers, sizeof(registers)) ||
                                peripheral_read_count != reads || peripheral_write_count != writes ||
                                !decoded.name || strcmp(decoded.name, name) || decoded.control != accepted || decoded.len != 2 ||
                                (decoded.emu != NULL) != (pc + 1 < DSP_PRAM_SIZE)) {
                                fprintf(stderr, "Bit-call decode op=%06x pc=%x accepted=%u len=%u name=%s\n", op, pc, accepted, decoded.len, decoded.name ? decoded.name : "null");
                                exit(1);
                            }
                            probes++;
                        }
                    }
                }
            }
        }
    }
    if (probes != 32768) exit(1);
    m->dsp->ops->finalize(m->dsp);
    free(m->dsp);
    free(m);
}

static void check_speculative_decode(void)
{
    Machine *m = machine_new(false);
    prepare(m, 0, 0, 1, 0, false);
    dsp_core_t *c = m->dsp->backend;
    static const uint32_t instructions[] = {0x110, UINT32_MAX, 0x0141C0, 0x0141C0, 0};
    for (unsigned test = 0; test < ARRAY_SIZE(instructions); test++) {
        unsigned pc = test == 3 ? DSP_PRAM_SIZE - 1 : 0;
        c->pram[pc] = instructions[test];
        if (pc + 1 < DSP_PRAM_SIZE) c->pram[pc + 1] = UINT32_MAX;
        c->pc = 0x22;
        DspDecoded decoded;
        dsp56k_decode(c, pc, &decoded);
        assert(c->pc == 0x22);
        assert((decoded.emu != NULL) == (test == 4));
        if (test == 4) assert(decoded.len == 1);
    }
    m->dsp->ops->finalize(m->dsp);
    free(m->dsp);
    free(m);
}

static void check_startup_operations(Machine *a, Machine *b, unsigned mode)
{
    check_left_shift_operations(a, b, mode);
    check_signed_right_operations(a, b, mode);
    check_immediate_shift_boundaries(a, b, mode);
    check_offset_control_moves(a, b, mode);
    check_control_register_bits(a, b, mode);
    check_memory_bits(a, b, mode);
    check_status_byte_logic(a, b, mode);
    check_divide_step(a, b, mode);
    check_normalize(a, b, mode);
    check_immediate_multiply(a, b, mode);
    check_peripheral_control_transfers(a, b, mode);
    check_register_control_transfers(a, b, mode);
    check_inc_dec_words(a, b, mode);
    static const unsigned values[] = {0, 1, 0x20, 0x3F, 0x80, 0xFF};
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
    for (unsigned reg = DSP_REG_M0; reg < DSP_REG_MAX; reg++) {
        for (unsigned v = 0; v < ARRAY_SIZE(values); v++) {
            for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                for (unsigned irq = 0; irq < 2; irq++) {
                    check(a, b, 0x0500A0 | (values[v] << 8) | reg, 0x30,
                          0x400000 | 0x1000000 | contexts[ctx], mode, irq);
                }
            }
        }
        if (reg <= DSP_REG_M7) continue; /* existing native EA register coverage */
        for (unsigned ea = 0; ea < 64; ea++) {
            if ((ea >> 3) == 6 && (ea & 7) != 0 && (ea & 7) != 4) continue;
            for (unsigned space = 0; space < 2; space++) {
                for (unsigned seed = 0; seed < 2; seed++) {
                    check(a, b, 0x05C020 | (ea << 8) | (space << 6) | reg,
                          seed || space ? 0x30 : 0xFFFF85, 0x400000 | 0x1000000 | seed,
                          mode, false);
                }
            }
        }
    }
    /* Register-sourced control writes must flush flags, limit accumulator
     * sources, preserve stack side effects and retire only once. */
    for (unsigned dst = DSP_REG_M7 + 1; dst < DSP_REG_MAX; dst++) {
        for (unsigned src = DSP_REG_X0; src <= DSP_REG_SR; src++) {
            if (src > DSP_REG_M7 && src != DSP_REG_SR) continue;
            for (unsigned seed = 0; seed < 8; seed++) {
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned irq = 0; irq < 2; irq++) {
                        check(a, b, 0x04C0A0 | (src << 8) | dst, 0x30,
                              0x400000 | 0x1000000 | 0x10000 | (seed << 1) | (seed << 4) |
                              contexts[ctx], mode, irq);
                    }
                }
            }
        }
    }
    for (unsigned reg = DSP_REG_M0; reg < DSP_REG_MAX; reg++) {
        for (unsigned addr = 0; addr < 64; addr++) {
            if ((addr >> 3) == 6 && (addr & 7) != 0 && (addr & 7) != 4) continue;
            for (unsigned space = 0; space < 2; space++) {
                for (unsigned write = 0; write < 2; write++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        for (unsigned seed = 0; seed < 2; seed++) {
                            check(a, b, 0x050020 | (write << 15) | (addr << 8) |
                                  (space << 6) | reg, 0x30,
                                  0x400000 | 0x1000000 | contexts[ctx] | seed,
                                  mode, seed != 0);
                        }
                    }
                }
            }
        }
    }
    check_effective_control_stores(a, b, mode);
    static const unsigned ports[] = {0, 0x30, 0x31, 0x32, 0x3F};
    for (unsigned ea = 0; ea < 64; ea++) {
        if ((ea >> 3) == 6 && (ea & 7) != 0 && (ea & 7) != 4) continue;
        for (unsigned port = 0; port < ARRAY_SIZE(ports); port++) {
            for (unsigned space = 0; space < 2; space++) {
                for (unsigned write = 0; write < 2; write++) {
                    for (unsigned seed = 0; seed < 2; seed++) {
                        check(a, b, 0x074000 | (write << 15) | (ea << 8) |
                              (space << 6) | ports[port], 0x30, seed, mode, false);
                    }
                }
            }
        }
    }
    for (unsigned port = 0; port < 64; port++) {
        for (unsigned space = 0; space < 2; space++) {
            for (unsigned write = 0; write < 2; write++) {
                check(a, b, 0x077400 | (write << 15) | (space << 6) | port,
                      0x30, 1, mode, false);
            }
        }
    }
    for (unsigned seed = 0; seed < 8; seed++) {
        for (unsigned d = 0; d < 2; d++) {
            unsigned input = 0x10000 | 0x1000000 | (seed << 1);
            for (unsigned value = 0; value < 64; value++) {
                check(a, b, 0x014084 | (value << 8) | (d << 3), 0, input, mode, false);
            }
            static const unsigned words[] = {0, 1, 63, 0x7FFFFF, 0x800000, 0xFFFFFF};
            for (unsigned w = 0; w < ARRAY_SIZE(words); w++) {
                check(a, b, 0x0140C4 | (d << 3), words[w], input, mode, false);
            }
            check(a, b, 0x000008 | d, 0, input, mode, false);
            check(a, b, 0x00000A | d, 0, input, mode, false);
        }
        for (unsigned op = 0; op < 4; op++) {
            static const uint32_t codes[] = {0x0AC040, 0x0AC060, 0x0BC040, 0x0BC060};
            for (unsigned bit = 0; bit < (op == 3 ? 16u : 32u); bit++) {
                check(a, b, codes[op] | (DSP_REG_OMR << 8) | bit,
                      0, seed | 0x1000000, mode, false);
            }
        }
    }
}

static void check_loop_address_reload(Machine *a, Machine *b)
{
    static const uint32_t ends[] = { 0x100, 0, 1, 0x123, 0, 0, 0, 0x100 };
    Machine *machines[] = { a, b };
    prepare(a, 0, 0, 1, 0, false);
    prepare(b, 0, 0, 1, 0, false);
    for (unsigned step = 0; step < ARRAY_SIZE(ends); step++) {
        dspjit_test_reset_fallback_count();
        for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
            Machine *m = machines[i];
            dsp_core_t *c = m->dsp->backend;
            c->pc = 0;
            c->registers[DSP_REG_SR] = step == 5 ? 0 : 1 << DSP_SR_LF;
            c->registers[DSP_REG_LA] = ends[step];
            c->registers[DSP_REG_LC] = step == 6 ? 1 : 2;
            c->registers[DSP_REG_SSH] = 0;
            m->dsp->save_cycles = 0;
            if (i == 0) {
                m->dsp->ops->step(m->dsp);
                c->cycle_count += c->instr_cycle;
            } else {
                m->dsp->ops->run(m->dsp, 1);
            }
        }
        /* P is not rewritten: reuse the same compiled entry as LA and LF
         * change, including a completed DO loop that restores the stack. */
        assert(dspjit_test_fallback_count() == 0);
        assert(compare_machines(a, b) == 0);
        cases++;
    }
}

static bool register_branch_condition(unsigned cc, unsigned sr)
{
    bool c = sr & 1, v = sr & 2, z = sr & 4, n = sr & 8;
    bool u = sr & 16, e = sr & 32, l = sr & 64;
    bool conditions[] = {!c, n == v, !z, !n, !z && (u || e), !e, !l,
                         !z && n == v, c, n != v, z, n, z || (!u && !e), e, l,
                         z || n != v, true};
    return conditions[cc];
}

static void check_relative_calls(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t offsets[] = {0, 1, 2, 0x20, 0x7fffff, 0x800000, 0xffffe0, 0xffffff};
    static const int short_offsets[] = {-256, -1, 0, 1, 2, 255};
    static const uint32_t pcs[] = {0, 1, 0x30, 0x7fe};
    /* Ten conditional forms and eight unconditional register forms. The
     * expected PC, registers and stack are computed without either engine. */
    for (unsigned form = 0; form < 18; form++) {
        for (unsigned cc = 0; cc < (form < 10 ? 16 : 1); cc++) {
            for (unsigned flags = 0; flags < 256; flags++) {
                unsigned n = form < 2 ? 0 : (form - 2) & 7;
                uint32_t offset = offsets[(flags + n) & 7];
                uint32_t op;
                if (form == 0) op = 0x0d1000 | cc;
                else if (form == 1) {
                    offset = short_offsets[flags % ARRAY_SIZE(short_offsets)];
                    op = 0x050000 | (cc << 12) | (offset & 31) | ((offset & 0x1e0) << 1);
                } else op = (form < 10 ? 0x0d1800 | cc : 0x0d1880) | (n << 8);
                uint32_t pc = pcs[(flags >> 6) & 3];
                uint32_t next = pc + (form == 0 ? 2 : 1);
                bool taken = form >= 10 || register_branch_condition(cc, flags);
                uint32_t target = taken ? (pc + offset) & 0xffffff : next;
                Machine *machines[] = {a, b};
                dspjit_test_reset_fallback_count();
                for (unsigned engine = 0; engine < 2; engine++) {
                    Machine *m = machines[engine];
                    prepare(m, op, offset & 0xffffff, 1, mode, false);
                    dsp_core_t *c = m->dsp->backend;
                    c->pc = pc;
                    c->registers[DSP_REG_R0 + n] = offset;
                    c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | flags;
                    uint32_t expected[DSP_REG_MAX];
                    memcpy(expected, c->registers, sizeof(expected));
                    if (taken) {
                        expected[DSP_REG_SP] = 3;
                        expected[DSP_REG_SSH] = next & 0xffff;
                        expected[DSP_REG_SSL] = expected[DSP_REG_SR] & 0xffff;
                    }
                    m->dsp->ops->write_memory(m->dsp, 'P', pc, op);
                    if (form == 0) m->dsp->ops->write_memory(m->dsp, 'P', pc + 1, offset & 0xffffff);
                    if (!engine) {
                        m->dsp->ops->step(m->dsp);
                        c->cycle_count += c->instr_cycle;
                    } else m->dsp->ops->run(m->dsp, 1);
                    if (c->pc != target || c->cycle_count != (form == 0 ? 6 : 4) ||
                        memcmp(c->registers, expected, sizeof(expected))) {
                        fprintf(stderr, "Relative call engine=%u form=%u cc=%u flags=%x PC=%x/%x cycles=%u\n",
                                engine, form, cc, flags, c->pc, target, (unsigned)c->cycle_count);
                        exit(1);
                    }
                    for (unsigned slot = 0; slot < ARRAY_SIZE(c->stack[0]); slot++) {
                        uint32_t high = slot == 2 ? 0x20 : taken && slot == 3 ? next & 0xffff : 0;
                        uint32_t low = taken && slot == 3 ? expected[DSP_REG_SR] & 0xffff : 0;
                        if (c->stack[0][slot] != high || c->stack[1][slot] != low) {
                            fputs("Relative call stack regression\n", stderr);
                            exit(1);
                        }
                    }
                }
                if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                cases++;
            }
            static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
            for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                for (unsigned seed = 0; seed < 2; seed++) {
                    uint32_t op = form == 0 ? 0x0d1000 | cc : form == 1 ? 0x050010 | (cc << 12) :
                        (form < 10 ? 0x0d1800 | cc : 0x0d1880) | (((form - 2) & 7) << 8);
                    check(a, b, op, 0x30, 0x400000 | 0x1000000 | contexts[ctx] | seed, mode, seed != 0);
                }
            }
        }
    }
}

static void check_register_relative_branches(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t pcs[] = {0, 1, 0x30, 0x7FE};
    static const uint32_t offsets[] = {0, 1, 2, 0x20, 0x7FFFFF, 0x800000, 0xFFFFE0, 0xFFFFFF};
    for (unsigned n = 0; n < 8; n++) {
        for (unsigned cc = 0; cc <= 16; cc++) {
            uint32_t op = (cc == 16 ? 0x0D18C0 : 0x0D1840 | cc) | (n << 8);
            for (unsigned flags = 0; flags < 256; flags++) {
                uint32_t pc = pcs[(flags >> 6) & 3];
                uint32_t offset = offsets[(flags + n) & 7];
                uint32_t target = register_branch_condition(cc, flags) ? (pc + offset) & 0xFFFFFF : pc + 1;
                Machine *machines[] = {a, b};
                dspjit_test_reset_fallback_count();
                for (unsigned engine = 0; engine < 2; engine++) {
                    Machine *m = machines[engine];
                    prepare(m, op, 0, 1, mode, false);
                    dsp_core_t *c = m->dsp->backend;
                    c->pc = pc;
                    c->registers[DSP_REG_R0 + n] = offset;
                    c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | flags | (n & 1 ? 1u << DSP_SR_FV : 0);
                    uint32_t expected[DSP_REG_MAX];
                    memcpy(expected, c->registers, sizeof(expected));
                    m->dsp->ops->write_memory(m->dsp, 'P', pc, op);
                    if (!engine) {
                        m->dsp->ops->step(m->dsp);
                        c->cycle_count += c->instr_cycle;
                    } else m->dsp->ops->run(m->dsp, 1);
                    if (c->pc != target || c->cycle_count != 2 ||
                        memcmp(c->registers, expected, sizeof(expected))) {
                        fprintf(stderr, "Register branch engine=%u n=%u cc=%u flags=%x pc=%x offset=%x target=%x/%x\n",
                                engine, n, cc, flags, pc, offset, c->pc, target);
                        exit(1);
                    }
                }
                if (dspjit_test_fallback_count() || compare_machines(a, b)) {
                    fputs("Register branch state/fallback regression\n", stderr);
                    exit(1);
                }
                cases++;
            }
            static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
            for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                for (unsigned seed = 0; seed < 2; seed++)
                    check(a, b, op, 0x30, 0x400000 | 0x1000000 | contexts[ctx] | seed,
                          mode, seed != 0);
            }
        }
        /* Both the taken target and the untaken fallthrough can finish DO.
         * LC must decrement once, then PC resumes at SSH. */
        for (unsigned path = 0; path < 3; path++) {
            uint32_t op = (path == 0 ? 0x0D18C0 : 0x0D1840) | (n << 8);
            Machine *machines[] = {a, b};
            dspjit_test_reset_fallback_count();
            for (unsigned engine = 0; engine < 2; engine++) {
                Machine *m = machines[engine];
                prepare(m, op, 0, 1, mode, false);
                dsp_core_t *c = m->dsp->backend;
                c->registers[DSP_REG_R0 + n] = 0x10;
                c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (1u << DSP_SR_LF) | (path == 2);
                c->registers[DSP_REG_LA] = path == 2 ? 0 : 0xF;
                c->registers[DSP_REG_LC] = 2;
                c->registers[DSP_REG_SSH] = c->stack[0][2] = 0x40;
                if (!engine) {
                    m->dsp->ops->step(m->dsp);
                    c->cycle_count += c->instr_cycle;
                } else m->dsp->ops->run(m->dsp, 1);
                if (c->pc != 0x40 || c->registers[DSP_REG_LC] != 1 ||
                    c->registers[DSP_REG_R0 + n] != 0x10 || c->cycle_count != 2) {
                    fprintf(stderr, "Register branch DO retirement engine=%u n=%u path=%u pc=%x LC=%u\n",
                            engine, n, path, c->pc, c->registers[DSP_REG_LC]);
                    exit(1);
                }
            }
            if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
            cases++;
        }
    }
}

static void check_relative_branches(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t status[] = { 0, 0xFF, 2, 8, 0x10, 0x20, 0x40, 4 };
    static const uint32_t profiles[] = { 0x20000000, 0x10000000, 0x40000 };
    static const int offsets[] = { -256, -1, 0, 1, 2, 255 };
    for (unsigned s = 0; s < ARRAY_SIZE(status); s++) {
        for (unsigned p = 0; p < ARRAY_SIZE(profiles); p++) {
            unsigned seed = 0x20000 | status[s] | profiles[p];
            for (unsigned o = 0; o < ARRAY_SIZE(offsets); o++) {
                uint32_t ext = offsets[o] & 0xFFFFFF;
                uint32_t short_imm = (offsets[o] & 0x1F) | ((offsets[o] & 0x1E0) << 1);
                check(a, b, 0x050C00 | short_imm, ext, seed, mode, false);
                check(a, b, 0x0D10C0, ext, seed, mode, false);
                for (unsigned cc = 0; cc < 16; cc++) {
                    check(a, b, 0x050400 | (cc << 12) | short_imm, ext, seed, mode, false);
                    check(a, b, 0x0D1040 | cc, ext, seed, mode, false);
                }
                for (unsigned irq = 0; irq < 2; irq++) {
                    check(a, b, 0x050800 | short_imm, ext, seed, mode, irq);
                    check(a, b, 0x0D1080, ext, seed, mode, irq);
                    check(a, b, 0x050800 | short_imm, ext, seed | 0x80000, mode, irq);
                    check(a, b, 0x0D1080, ext, seed | 0x80000, mode, irq);
                }
            }
        }
    }
    static const unsigned regs[] = { DSP_REG_X0, DSP_REG_A, DSP_REG_B, DSP_REG_R0 };
    for (unsigned p = 0; p < ARRAY_SIZE(profiles); p++) {
        for (unsigned seed = 0; seed < 4; seed++) {
            for (unsigned bit = 0; bit < 32; bit++) {
                for (unsigned set = 0; set < 2; set++) {
                    uint32_t low = (set << 5) | bit;
                    unsigned input = seed | profiles[p];
                    for (unsigned r = 0; r < ARRAY_SIZE(regs); r++) {
                        check(a, b, 0x0CC080 | (regs[r] << 8) | low, 0x30, input, mode, false);
                    }
                    /* These peripherals are mapped in X space. */
                    static const unsigned ports[] = { 0, 5, 0x14, 0x15 };
                    for (unsigned port = 0; port < ARRAY_SIZE(ports); port++) {
                        check(a, b, 0x0CC000 | (ports[port] << 8) | low,
                              0x30, input, mode, false);
                    }
                }
            }
        }
    }
}

static void check_loop_setup(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned counts[] = { 0, 1, 2, 3, 255, 256, 4095 };
    static const unsigned ends[] = { 0, 1, 2, 3, 0xFFFF, 0x10001 };
    static const unsigned loops[] = { 0x20000000, 0x10000000, 0x40000 };
    static const unsigned stacks[] = { 0, 0x80000, 0x40000000 };
    for (unsigned n = 0; n < ARRAY_SIZE(counts); n++) {
        uint32_t imm = ((counts[n] & 0xFF) << 8) | (counts[n] >> 8);
        for (unsigned l = 0; l < ARRAY_SIZE(loops); l++) {
            for (unsigned sp = 0; sp < ARRAY_SIZE(stacks); sp++) {
                for (unsigned irq = 0; irq < 2; irq++) {
                    for (unsigned sr = 0; sr <= 0xFF; sr += 0xFF) {
                        unsigned seed = 0x20000 | sr | loops[l] | stacks[sp];
                        check(a, b, 0x0600A0 | imm, 0x30, seed, mode, irq);
                        for (unsigned end = 0; end < ARRAY_SIZE(ends); end++) {
                            check(a, b, 0x060090 | imm, ends[end], seed, mode, irq);
                        }
                    }
                }
            }
        }
    }
}

static void check_short_memory_loops(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t counts[] = {0, 1, 2, 255, 0xffff, 0x10000, 0xffffff};
    static const uint32_t ends[] = {0x40, 0x10040, 0xffffe0};
    for (unsigned relative = 0; relative < 2; relative++) {
        for (unsigned space = 0; space < 2; space++) {
            for (unsigned addr = 0; addr < 64; addr++) {
                uint32_t op = 0x060000 | (relative << 4) | (space << 6) | (addr << 8);
                for (unsigned n = 0; n < ARRAY_SIZE(counts); n++) {
                    for (unsigned end = 0; end < ARRAY_SIZE(ends); end++) {
                        uint32_t pc = 0x20;
                        uint32_t la = ((relative ? pc : 0) + ends[end]) & 0xffff;
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, ends[end], 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->pc = pc;
                            c->registers[DSP_REG_LA] = 0x1234;
                            c->registers[DSP_REG_LC] = 0x5678;
                            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (n & 1 ? 0xff : 0);
                            uint32_t expected[DSP_REG_MAX];
                            memcpy(expected, c->registers, sizeof(expected));
                            expected[DSP_REG_SP] = 4;
                            expected[DSP_REG_SSH] = pc + 2;
                            expected[DSP_REG_SSL] = expected[DSP_REG_SR] & 0xffff;
                            expected[DSP_REG_LA] = la;
                            expected[DSP_REG_LC] = counts[n] & 0xffff;
                            expected[DSP_REG_SR] |= 1 << DSP_SR_LF;
                            m->dsp->ops->write_memory(m->dsp, space ? 'Y' : 'X', addr, counts[n]);
                            m->dsp->ops->write_memory(m->dsp, 'P', pc, op);
                            m->dsp->ops->write_memory(m->dsp, 'P', pc + 1, ends[end]);
                            if (!engine) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);
                            if (c->pc != pc + 2 || c->cycle_count != 6 ||
                                memcmp(c->registers, expected, sizeof(expected))) {
                                fprintf(stderr, "Short memory loop engine=%u relative=%u space=%u addr=%u count=%x end=%x PC=%x LC=%x LA=%x\n",
                                        engine, relative, space, addr, counts[n], ends[end], c->pc,
                                        c->registers[DSP_REG_LC], c->registers[DSP_REG_LA]);
                                exit(1);
                            }
                            for (unsigned slot = 0; slot < ARRAY_SIZE(c->stack[0]); slot++) {
                                uint32_t high = slot == 2 ? 0x20 : slot == 3 ? 0x1234 : slot == 4 ? pc + 2 : 0;
                                uint32_t low = slot == 3 ? 0x5678 : slot == 4 ? expected[DSP_REG_SSL] : 0;
                                if (c->stack[0][slot] != high || c->stack[1][slot] != low) {
                                    fputs("Short memory loop stack regression\n", stderr);
                                    exit(1);
                                }
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                        cases++;
                    }
                }
                static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
                static const uint32_t boundaries[] = {0, 1, 2, 0xffff, 0x10001};
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned end = 0; end < ARRAY_SIZE(boundaries); end++) {
                        for (unsigned irq = 0; irq < 2; irq++)
                            check(a, b, op, boundaries[end], contexts[ctx] | 0x20000 | 0xff, mode, irq);
                    }
                }
            }
        }
    }
}

static void check_indirect_memory_loops(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t counts[] = {0, 1, 2, 255, 0xffff, 0x10000, 0xffffff};
    static const uint32_t ends[] = {0x40, 0x10040, 0xffffe0};
    for (unsigned relative = 0; relative < 2; relative++) {
        for (unsigned space = 0; space < 2; space++) {
            for (unsigned addr = 0; addr < 64; addr++) {
                if ((addr >> 3) == 6) continue;
                unsigned ea_mode = addr >> 3, r = addr & 7;
                uint32_t op = 0x064000 | (relative << 4) | (space << 6) | (addr << 8);
                for (unsigned profile = 0; profile < 2; profile++)
                for (unsigned n = 0; n < ARRAY_SIZE(counts); n++) {
                    for (unsigned end = 0; end < ARRAY_SIZE(ends); end++) {
                        uint32_t pc = 0x20;
                        uint32_t la = ((relative ? pc : 0) + ends[end]) & 0xffff;
                        Machine *machines[] = {a, b};
                        dspjit_test_reset_fallback_count();
                        for (unsigned engine = 0; engine < 2; engine++) {
                            Machine *m = machines[engine];
                            prepare(m, op, ends[end], 1, mode, false);
                            dsp_core_t *c = m->dsp->backend;
                            c->pc = pc;
                            c->registers[DSP_REG_R0 + r] = 0x7f;
                            c->registers[DSP_REG_N0 + r] = 3;
                            c->registers[DSP_REG_M0 + r] = profile ? 0x3f : 0xffff;
                            int delta = ea_mode == 0 ? -3 : ea_mode == 1 || ea_mode == 5 ? 3 :
                                        ea_mode == 2 || ea_mode == 7 ? -1 : ea_mode == 3 ? 1 : 0;
                            uint32_t updated = profile ? 0x40 | ((0x3f + delta) & 0x3f) : 0x7f + delta;
                            uint32_t address = ea_mode == 5 || ea_mode == 7 ? updated : 0x7f;
                            c->registers[DSP_REG_LA] = 0x1234;
                            c->registers[DSP_REG_LC] = 0x5678;
                            c->registers[DSP_REG_SR] = (mode << DSP_SR_S0) | (n & 1 ? 0xff : 0);
                            uint32_t expected[DSP_REG_MAX];
                            memcpy(expected, c->registers, sizeof(expected));
                            if (ea_mode != 4 && ea_mode != 5) expected[DSP_REG_R0 + r] = updated;
                            expected[DSP_REG_SP] = 4;
                            expected[DSP_REG_SSH] = pc + 2;
                            expected[DSP_REG_SSL] = expected[DSP_REG_SR] & 0xffff;
                            expected[DSP_REG_LA] = la;
                            expected[DSP_REG_LC] = counts[n] & 0xffff;
                            expected[DSP_REG_SR] |= 1 << DSP_SR_LF;
                            m->dsp->ops->write_memory(m->dsp, space ? 'Y' : 'X', address, counts[n]);
                            m->dsp->ops->write_memory(m->dsp, 'P', pc, op);
                            m->dsp->ops->write_memory(m->dsp, 'P', pc + 1, ends[end]);
                            if (!engine) {
                                m->dsp->ops->step(m->dsp);
                                c->cycle_count += c->instr_cycle;
                            } else m->dsp->ops->run(m->dsp, 1);
                            if (c->pc != pc + 2 || c->cycle_count != (ea_mode == 5 || ea_mode == 7 ? 8 : 6) ||
                                memcmp(c->registers, expected, sizeof(expected))) {
                                fprintf(stderr, "Indirect memory loop engine=%u relative=%u space=%u addr=%u count=%x end=%x PC=%x LC=%x LA=%x\n",
                                        engine, relative, space, addr, counts[n], ends[end], c->pc,
                                        c->registers[DSP_REG_LC], c->registers[DSP_REG_LA]);
                                exit(1);
                            }
                            for (unsigned slot = 0; slot < ARRAY_SIZE(c->stack[0]); slot++) {
                                uint32_t high = slot == 2 ? 0x20 : slot == 3 ? 0x1234 : slot == 4 ? pc + 2 : 0;
                                uint32_t low = slot == 3 ? 0x5678 : slot == 4 ? expected[DSP_REG_SSL] : 0;
                                if (c->stack[0][slot] != high || c->stack[1][slot] != low) {
                                    fputs("Indirect memory loop stack regression\n", stderr);
                                    exit(1);
                                }
                            }
                        }
                        if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                        cases++;
                    }
                }
                static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000, 0x40000000};
                static const uint32_t boundaries[] = {0, 1, 2, 0xffff, 0x10001};
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned end = 0; end < ARRAY_SIZE(boundaries); end++) {
                        for (unsigned irq = 0; irq < 2; irq++)
                            for (unsigned modifier = 0; modifier < 2; modifier++)
                                check(a, b, op, boundaries[end], contexts[ctx] | 0x20000 |
                                      (modifier ? 8 : 0), mode, irq);
                    }
                }
            }
        }
    }
    /* An X peripheral count must be consumed once, including when setup
     * itself reaches the new loop endpoint and requires post-processing. */
    for (unsigned relative = 0; relative < 2; relative++) {
        for (unsigned r = 0; r < 8; r++) {
            for (unsigned boundary = 0; boundary < 2; boundary++) {
                uint32_t op = 0x066000 | (r << 8) | (relative << 4);
                Machine *machines[] = {a, b};
                dspjit_test_reset_fallback_count();
                for (unsigned engine = 0; engine < 2; engine++) {
                    Machine *m = machines[engine];
                    prepare(m, op, boundary ? 1 : 0x40, 1, mode, false);
                    dsp_core_t *c = m->dsp->backend;
                    c->registers[DSP_REG_SR] = mode << DSP_SR_S0;
                    c->registers[DSP_REG_R0 + r] = 0xffffc5;
                    uint64_t before = peripheral_read_count;
                    if (!engine) {
                        m->dsp->ops->step(m->dsp);
                        c->cycle_count += c->instr_cycle;
                    } else m->dsp->ops->run(m->dsp, 1);
                    if (peripheral_read_count - before != 1 || c->pc != 2 ||
                        c->registers[DSP_REG_LC] != 0x82 - boundary || c->cycle_count != 6) {
                        fprintf(stderr, "Indirect loop peripheral read engine=%u boundary=%u reads=%llu PC=%x LC=%x\n",
                                engine, boundary, (unsigned long long)(peripheral_read_count - before),
                                c->pc, c->registers[DSP_REG_LC]);
                        exit(1);
                    }
                }
                if (dspjit_test_fallback_count() || compare_machines(a, b)) exit(1);
                cases++;
            }
        }
    }
}

static void check_register_loop_setup(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned ends[] = { 0, 1, 2, 0xFFFF, 0x10001, 0xFFFFFF };
    static const unsigned loops[] = { 0x20000000, 0x10000000, 0x40000 };
    static const unsigned stacks[] = { 0, 0x80000, 0x40000000 };
    for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_LC; reg++) {
        if (reg > DSP_REG_M7 && reg < DSP_REG_SR) continue;
        for (unsigned n = 0; n < 16; n++) {
            for (unsigned l = 0; l < ARRAY_SIZE(loops); l++) {
                for (unsigned sp = 0; sp < ARRAY_SIZE(stacks); sp++) {
                    for (unsigned irq = 0; irq < 2; irq++) {
                        unsigned seed = 0x100000 | 0x20000 | (n << 8) |
                            (n & 1 ? 0xFF : 0) | loops[l] | stacks[sp];
                        check(a, b, 0x06C020 | (reg << 8), 0x30, seed, mode, irq);
                        for (unsigned e = 0; e < ARRAY_SIZE(ends); e++) {
                            check(a, b, 0x06C010 | (reg << 8), ends[e], seed, mode, irq);
                            check(a, b, 0x06C000 | (reg << 8), ends[e], seed, mode, irq);
                        }
                    }
                }
            }
        }
    }
}

static void check_program_control_transfers(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
    for (unsigned reg = 0; reg < DSP_REG_MAX; reg++) {
        if (reg >= DSP_REG_X0 && reg <= DSP_REG_M7) continue;
        for (unsigned form = 0; form < 2; form++) {
            for (unsigned addr = 0; addr < 64; addr++) {
                if (form && (addr >> 3) == 6 && (addr & 7) != 0 && (addr & 7) != 4) continue;
                for (unsigned read = 0; read < 2; read++) {
                    for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                        for (unsigned seed = 0; seed < 2; seed++) {
                            check(a, b, 0x070000 | (form ? 0x4080 : 0) |
                                  (addr << 8) | (read << 15) | reg, 0x30,
                                  0x400000 | 0x1000000 | 0x2000000 | 0x8000000 |
                                  contexts[ctx] | (seed ? 9 : 0), mode, seed != 0);
                        }
                    }
                }
            }
        }
    }
}

static void check_program_peripheral_transfers(Machine *a, Machine *b, unsigned mode)
{
    static const unsigned contexts[] = {0x20000000, 0x40000, 0x80000};
    for (unsigned ea = 0; ea < 64; ea++) {
        if ((ea >> 3) == 6 && (ea & 7) != 0 && (ea & 7) != 4) continue;
        for (unsigned port = 0; port < 64; port++) {
            for (unsigned write = 0; write < 2; write++) {
                if (write && port == 0x16) continue;
                for (unsigned ctx = 0; ctx < ARRAY_SIZE(contexts); ctx++) {
                    for (unsigned seed = 0; seed < 2; seed++) {
                        check(a, b, 0x084040 | (write << 15) | (ea << 8) | port,
                              0x30, 0x400000 | 0x8000000 | contexts[ctx] |
                              (seed ? 9 : 0), mode, seed != 0);
                    }
                }
            }
        }
    }
    for (unsigned action = 0; action < 2; action++) {
        check(a, b, 0x08F056, 0x30,
              0x80000000u | 0x400000 | 0x8000000 | 0x20000000 | (action << 8), mode, false);
    }
}

static void check_program_transfers(Machine *a, Machine *b, unsigned mode)
{
    check_program_control_transfers(a, b, mode);
    check_program_peripheral_transfers(a, b, mode);
    for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_M7; reg++) {
        for (unsigned ea = 0; ea < 64; ea++) {
            if ((ea >> 3) == 6 && (ea & 7) != 0 && (ea & 7) != 4) continue;
            for (unsigned seed = 0; seed < 4; seed++) {
                check(a, b, 0x074080 | (ea << 8) | reg, 0x30,
                      seed | 0x10000 | 0x8000000, mode, false);
            }
        }
        for (unsigned addr = 0; addr < 64; addr++) {
            for (unsigned read = 0; read < 2; read++) {
                for (unsigned seed = 0; seed < 2; seed++) {
                    check(a, b, 0x070000 | (addr << 8) | (read << 15) | reg,
                          0x30, seed | 0x10000 | 0x8000000 |
                          (seed ? 0x4000000 : 0), mode, false);
                }
            }
        }
    }
}

static void check_program_write_sequences(Machine *a, Machine *b, unsigned mode)
{
    static const uint32_t programs[][5] = {
        /* Replace an already cached next opcode (CLR A -> CLR B). */
        {0x070104, 0x200013, 0x08F484, 1, 0},
        /* REP replaces its own body, then must execute the new CLR A. */
        {0x0603A0, 0x070104, 0x08F484, 1, 0},
        /* REP changes its EA extension from P:2 to P:$40. */
        {0x0602A0, 0x077084, 2, 0x08F484, 1},
        /* An unchanged cached extension must not invalidate code. */
        {0x070304, 0, 0x08F484, 1, 0},
    };
    Machine *machines[] = {a, b};
    for (unsigned which = 0; which < ARRAY_SIZE(programs); which++) {
        for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
            Machine *m = machines[i];
            prepare(m, programs[which][0], 0, 1, mode, false);
            dsp_core_t *c = m->dsp->backend;
            c->registers[DSP_REG_SR] = mode << DSP_SR_S0;
            c->registers[DSP_REG_LA] = 0xFFF;
            for (unsigned pc = 0; pc < ARRAY_SIZE(programs[which]); pc++) {
                m->dsp->ops->write_memory(m->dsp, 'P', pc, programs[which][pc]);
            }
            m->dsp->ops->write_memory(m->dsp, 'P', 0x40, 0);
        }
        if (which == 0 || which == 3) {
            /* Populate the destination's translation before the write. */
            for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
                dsp_core_t *c = machines[i]->dsp->backend;
                c->pc = which == 0 ? 1 : 2;
                assert(run_frame(machines[i], 100000, false) == 0);
            }
            assert(compare_machines(a, b) == 0);
        }
        for (unsigned i = 0; i < ARRAY_SIZE(machines); i++) {
            dsp_core_t *c = machines[i]->dsp->backend;
            c->pc = 0;
            c->registers[DSP_REG_A0] = c->registers[DSP_REG_A2] = 0;
            c->registers[DSP_REG_B0] = c->registers[DSP_REG_B2] = 0;
            c->registers[DSP_REG_A1] = 0x123456;
            c->registers[DSP_REG_B1] = 0x654321;
            c->registers[DSP_REG_X0] = which == 0 ? 0x20001B :
                which == 1 ? 0x200013 : which == 2 ? 0x40 : 1;
            machines[i]->dsp->save_cycles = 0;
        }
        uint64_t scans = dspjit_test_invalidation_scans();
        dspjit_test_reset_fallback_count();
        assert(run_frame(a, 100000, false) == 0);
        assert(run_frame(b, 100000, true) == 0);
        assert(!dspjit_test_fallback_count());
        assert(compare_machines(a, b) == 0);
        dsp_core_t *c = b->dsp->backend;
        if (which == 0) assert(c->registers[DSP_REG_B1] == 0);
        if (which == 1) assert(c->registers[DSP_REG_A1] == 0);
        if (which == 2) assert(b->dsp->ops->read_memory(b->dsp, 'P', 0x40) == 0x40);
        if (which == 3) assert(dspjit_test_invalidation_scans() == scans);
        cases++;
    }
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--bitcall-decode")) {
        check_bitcall_decode();
        puts("native DSP: 32768 bit-call decode probes agree");
        return 0;
    }
    bool short_relative_calls_only = argc == 2 && !strcmp(argv[1], "--short-relative-calls");
    bool pp_relative_calls_only = argc == 2 && !strcmp(argv[1], "--pp-relative-calls");
    bool indirect_bitbranches_only = argc == 2 && !strcmp(argv[1], "--indirect-memory-bitbranches");
    bool short_bitbranches_only = argc == 2 && !strcmp(argv[1], "--short-memory-bitbranches");
    bool indirect_bitcalls_only = argc == 2 && !strcmp(argv[1], "--indirect-memory-bitcalls");
    bool short_bitcalls_only = argc == 2 && !strcmp(argv[1], "--short-memory-bitcalls");
    bool register_bitcalls_only = argc == 2 && !strcmp(argv[1], "--register-bitcalls");
    bool pp_calls_only = argc == 2 && !strcmp(argv[1], "--pp-calls");
    bool qq_relative_calls_only = argc == 2 && !strcmp(argv[1], "--qq-relative-calls");
    bool qq_calls_only = argc == 2 && !strcmp(argv[1], "--qq-calls");
    bool qq_jumps_only = argc == 2 && !strcmp(argv[1], "--qq-jumps");
    bool qq_branches_only = argc == 2 && !strcmp(argv[1], "--qq-branches");
    bool qq_bits_only = argc == 2 && !strcmp(argv[1], "--qq-bits");
    bool register_logical_only = argc == 2 && !strcmp(argv[1], "--register-logical-shifts");
    bool merge_only = argc == 2 && !strcmp(argv[1], "--merge");
    bool insert_only = argc == 2 && !strcmp(argv[1], "--insert");
    bool extract_only = argc == 2 && !strcmp(argv[1], "--extract");
    bool extractu_only = argc == 2 && !strcmp(argv[1], "--extractu");
    bool leading_bits_only = argc == 2 && !strcmp(argv[1], "--leading-bits");
    bool register_shifts_only = argc == 2 && !strcmp(argv[1], "--register-shifts");
    bool indirect_memory_loops_only = argc == 2 && !strcmp(argv[1], "--indirect-memory-loops");
    bool short_memory_loops_only = argc == 2 && !strcmp(argv[1], "--short-memory-loops");
    bool relative_calls_only = argc == 2 && !strcmp(argv[1], "--relative-calls");
    bool register_branches_only = argc == 2 && !strcmp(argv[1], "--register-branches");
    bool pc_relative_only = argc == 2 && !strcmp(argv[1], "--pc-relative-loads");
    bool rounded_multiply_only = argc == 2 && !strcmp(argv[1], "--rounded-multiply");
    bool y_long_only = argc == 2 && !strcmp(argv[1], "--y-long-offset");
    bool logical_shifts_only = argc == 2 && !strcmp(argv[1], "--logical-shifts");
    bool immediate_logic_only = argc == 2 && !strcmp(argv[1], "--immediate-logic");
    bool relative_only = argc == 2 && !strcmp(argv[1], "--relative");
    bool loops_only = argc == 2 && !strcmp(argv[1], "--loops");
    bool compare_only = argc == 2 && !strcmp(argv[1], "--compare");
    bool left_flags_only = argc == 2 && !strcmp(argv[1], "--left-flags");
    bool signed_right_only = argc == 2 && !strcmp(argv[1], "--signed-right");
    bool shifts_only = argc == 2 && !strcmp(argv[1], "--shift-boundaries");
    bool offset_control_only = argc == 2 && !strcmp(argv[1], "--offset-control");
    bool control_bits_only = argc == 2 && !strcmp(argv[1], "--control-bits");
    bool memory_bits_only = argc == 2 && !strcmp(argv[1], "--memory-bits");
    bool byte_logic_only = argc == 2 && !strcmp(argv[1], "--status-byte");
    bool divide_only = argc == 2 && !strcmp(argv[1], "--divide");
    bool normalize_only = argc == 2 && !strcmp(argv[1], "--normalize");
    bool multiply_only = argc == 2 && !strcmp(argv[1], "--multiply");
    bool program_peripheral_only = argc == 2 && !strcmp(argv[1], "--program-peripheral");
    bool peripheral_only = argc == 2 && !strcmp(argv[1], "--peripheral-control");
    bool program_control_only = argc == 2 && !strcmp(argv[1], "--program-control");
    bool register_only = argc == 2 && !strcmp(argv[1], "--register-control");
    bool effective_only = argc == 2 && !strcmp(argv[1], "--effective-control");
    bool startup_only = argc == 2 && !strcmp(argv[1], "--startup");
    bool program_only = argc == 2 && !strcmp(argv[1], "--program");
    if (argc != 1 && !short_relative_calls_only && !pp_relative_calls_only && !indirect_bitbranches_only && !short_bitbranches_only && !indirect_bitcalls_only && !short_bitcalls_only && !register_bitcalls_only && !pp_calls_only && !qq_relative_calls_only && !qq_calls_only && !qq_jumps_only && !qq_branches_only && !qq_bits_only && !register_logical_only && !merge_only && !insert_only && !extract_only && !extractu_only && !leading_bits_only && !register_shifts_only && !indirect_memory_loops_only && !short_memory_loops_only && !relative_calls_only && !register_branches_only && !pc_relative_only && !rounded_multiply_only && !y_long_only && !logical_shifts_only && !immediate_logic_only && !relative_only && !loops_only && !program_only && !compare_only && !startup_only && !effective_only && !register_only && !program_control_only && !peripheral_only && !program_peripheral_only && !multiply_only && !normalize_only && !divide_only && !byte_logic_only && !memory_bits_only && !control_bits_only && !offset_control_only && !shifts_only && !signed_right_only && !left_flags_only) {
        fputs("usage: dspnative [--short-relative-calls|--pp-relative-calls|--indirect-memory-bitbranches|--short-memory-bitbranches|--bitcall-decode|--indirect-memory-bitcalls|--short-memory-bitcalls|--register-bitcalls|--pp-calls|--qq-relative-calls|--qq-calls|--qq-jumps|--qq-branches|--qq-bits|--register-logical-shifts|--merge|--insert|--extract|--extractu|--leading-bits|--register-shifts|--indirect-memory-loops|--short-memory-loops|--relative-calls|--register-branches|--pc-relative-loads|--rounded-multiply|--y-long-offset|--logical-shifts|--immediate-logic|--relative|--loops|--program|--compare|--startup|--effective-control|--register-control|--program-control|--peripheral-control|--program-peripheral|--multiply|--normalize|--divide|--status-byte|--memory-bits|--control-bits|--offset-control|--shift-boundaries|--signed-right|--left-flags]\n", stderr);
        return 2;
    }
    Machine *a = machine_new(false), *b = machine_new(true);
    if (a->dsp->ops == b->dsp->ops) {
        fputs("native DSP test requires a JIT host\n", stderr);
        return 2;
    }
    DspJit *jit = ((dsp_core_t *)b->dsp->backend)->jit;
    assert(dspjit_test_mode_count(jit) == 0);
    uint64_t initially_committed = dspjit_test_committed_bytes(jit);
#ifdef _WIN32
    assert(initially_committed == (64u << 10));
#endif
    check_speculative_decode();
    check_bitcall_decode();
    if (short_relative_calls_only || pp_relative_calls_only || indirect_bitbranches_only || short_bitbranches_only || indirect_bitcalls_only || short_bitcalls_only || register_bitcalls_only || pp_calls_only || qq_relative_calls_only || qq_calls_only || qq_jumps_only || qq_branches_only || qq_bits_only || register_logical_only || merge_only || insert_only || extract_only || extractu_only || leading_bits_only || register_shifts_only || indirect_memory_loops_only || short_memory_loops_only || relative_calls_only || register_branches_only || pc_relative_only || rounded_multiply_only || y_long_only || logical_shifts_only || immediate_logic_only || relative_only || loops_only || program_only || compare_only || startup_only || effective_only || register_only || program_control_only || peripheral_only || program_peripheral_only || multiply_only || normalize_only || divide_only || byte_logic_only || memory_bits_only || control_bits_only || offset_control_only || shifts_only || signed_right_only || left_flags_only) {
        for (unsigned mode = 0; mode < 4; mode++) {
            if (short_relative_calls_only) check_short_relative_calls(a, b, mode);
            if (pp_relative_calls_only) check_pp_relative_calls(a, b, mode);
            if (indirect_bitbranches_only) check_indirect_memory_bitbranches(a, b, mode);
            if (short_bitbranches_only) check_short_memory_bitbranches(a, b, mode);
            if (indirect_bitcalls_only) check_indirect_memory_bitcalls(a, b, mode);
            if (short_bitcalls_only) check_short_memory_bitcalls(a, b, mode);
            if (register_bitcalls_only) check_register_bitcalls(a, b, mode);
            if (pp_calls_only) check_pp_calls(a, b, mode);
            if (qq_relative_calls_only) check_qq_relative_calls(a, b, mode);
            if (qq_calls_only) check_qq_calls(a, b, mode);
            if (qq_jumps_only) check_qq_jumps(a, b, mode);
            if (qq_branches_only) check_qq_branches(a, b, mode);
            if (qq_bits_only) check_qq_bits(a, b, mode);
            if (register_logical_only) check_register_logical_shifts(a, b, mode);
            if (merge_only) check_merge(a, b, mode);
            if (insert_only) check_insert(a, b, mode);
            if (extract_only) check_signed_extract(a, b, mode);
            if (extractu_only) check_unsigned_extract(a, b, mode);
            if (leading_bits_only) check_count_leading_bits(a, b, mode);
            if (register_shifts_only) check_register_arithmetic_shifts(a, b, mode);
            if (indirect_memory_loops_only) check_indirect_memory_loops(a, b, mode);
            if (short_memory_loops_only) check_short_memory_loops(a, b, mode);
            if (relative_calls_only) check_relative_calls(a, b, mode);
            if (register_branches_only) check_register_relative_branches(a, b, mode);
            if (pc_relative_only) check_pc_relative_loads(a, b, mode);
            if (rounded_multiply_only) check_immediate_rounded_multiply(a, b, mode);
            if (y_long_only) check_y_long_offset_moves(a, b, mode);
            if (logical_shifts_only) check_logical_shift_immediates(a, b, mode);
            if (immediate_logic_only) check_immediate_logic(a, b, mode);
            if (left_flags_only) {
                check_left_shift_operations(a, b, mode);
                check_immediate_shift_boundaries(a, b, mode);
                check_normalize(a, b, mode);
                check_divide_step(a, b, mode);
            }
            if (signed_right_only) {
                check_signed_right_operations(a, b, mode);
                check_immediate_shift_boundaries(a, b, mode);
                check_normalize(a, b, mode);
            }
            if (shifts_only) check_immediate_shift_boundaries(a, b, mode);
            if (offset_control_only) check_offset_control_moves(a, b, mode);
            if (control_bits_only) check_control_register_bits(a, b, mode);
            if (memory_bits_only) check_memory_bits(a, b, mode);
            if (byte_logic_only) check_status_byte_logic(a, b, mode);
            if (divide_only) check_divide_step(a, b, mode);
            if (normalize_only) check_normalize(a, b, mode);
            if (multiply_only) check_immediate_multiply(a, b, mode);
            if (program_peripheral_only) check_program_peripheral_transfers(a, b, mode);
            if (peripheral_only) check_peripheral_control_transfers(a, b, mode);
            if (program_control_only) check_program_control_transfers(a, b, mode);
            if (register_only) check_register_control_transfers(a, b, mode);
            if (effective_only) check_effective_control_stores(a, b, mode);
            if (startup_only) check_startup_operations(a, b, mode);
            if (compare_only) check_compare_immediates(a, b, mode);
            if (relative_only) check_relative_branches(a, b, mode);
            if (loops_only) {
                check_loop_setup(a, b, mode);
                check_register_loop_setup(a, b, mode);
            }
            if (program_only) check_program_transfers(a, b, mode);
            if (program_only) check_program_write_sequences(a, b, mode);
        }
        goto done;
    }
    check_macr_rounding(a, b);
    check_rotate_carry(a, b);
    check_max_transfer(a, b);
    check_tcc(a, b);
    check_movec(a, b);
    check_data_write_invalidation(a, b);
    check_hot_control_registers(a, b);
    check_program_memory_reads(a, b);
    for (unsigned mode = 0; mode < 4; mode++) {
        check_startup_operations(a, b, mode);
        check_compare_immediates(a, b, mode);
        check_program_transfers(a, b, mode);
        check_program_write_sequences(a, b, mode);
    }
    check_status_restore_chains(a, b);
    check_logical_immediates(a, b);
    for (unsigned mode = 0; mode < 4; mode++) check_short_relative_calls(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_pp_relative_calls(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_indirect_memory_bitbranches(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_short_memory_bitbranches(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_indirect_memory_bitcalls(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_short_memory_bitcalls(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_register_bitcalls(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_pp_calls(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_qq_relative_calls(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_qq_calls(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_qq_jumps(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_qq_branches(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_qq_bits(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_register_logical_shifts(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_merge(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_insert(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_signed_extract(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_unsigned_extract(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_count_leading_bits(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_register_arithmetic_shifts(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_indirect_memory_loops(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_short_memory_loops(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_relative_calls(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_register_relative_branches(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_pc_relative_loads(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_immediate_rounded_multiply(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_y_long_offset_moves(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_logical_shift_immediates(a, b, mode);
    for (unsigned mode = 0; mode < 4; mode++) check_immediate_logic(a, b, mode);
    check_loop_address_reload(a, b);
    for (unsigned sr = 0; sr < 256; sr++) {
        for (unsigned cc = 0; cc < 16; cc++) {
            check(a, b, 0x0E0030 | (cc << 12), 0x30, 0x20000 | sr, 0, false);
            check(a, b, 0x0F0030 | (cc << 12), 0x30, 0x20000 | sr, 0, false);
        }
    }
    for (unsigned mode = 0; mode < 4; mode++) {
        /* Sweep every legal parallel ALU encoding. */
        check_relative_branches(a, b, mode);
        check_loop_setup(a, b, mode);
        check_register_loop_setup(a, b, mode);
        for (unsigned cc = 0; cc < 16; cc++) {
            for (unsigned sr = 0; sr <= 0xFF; sr += 0xFF) {
                unsigned input = 0xE0000 | sr;
                check(a, b, 0x0E0030 | (cc << 12), 0x30, input, mode, false);
                check(a, b, 0x0F0030 | (cc << 12), 0x30, input, mode, false);
                check(a, b, 0x0F0030 | (cc << 12), 0x30, input, mode, true);
                for (unsigned ea = 0; ea < 64; ea++) {
                    if ((ea >> 3) == 6 && (ea & 7) != 0 && (ea & 7) != 4) {
                        continue;
                    }
                    check(a, b, 0x0AC0A0 | (ea << 8) | cc, 0x30, input, mode, false);
                    check(a, b, 0x0BC0A0 | (ea << 8) | cc, 0x30, input, mode, false);
                    check(a, b, 0x0BC0A0 | (ea << 8) | cc, 0x30, input, mode, true);
                }
            }
        }
        for (unsigned seed = 0; seed < 64; seed++) {
            check(a, b, 0x0C0030, 0x30, seed, mode, false);
            for (unsigned cc = 0; cc < 16; cc++) {
                check(a, b, 0x0E0030 | (cc << 12), 0x30, seed, mode, false);
                if (seed < 16) {
                    check(a, b, 0x0F0030 | (cc << 12), 0x30, seed, mode, false);
                    check(a, b, 0x0F0030 | (cc << 12), 0x30, seed, mode, true);
                }
            }
            for (unsigned ea = 0; ea < 64; ea++) {
                if ((ea >> 3) == 6 && (ea & 7) != 0 && (ea & 7) != 4) {
                    continue;
                }
                check(a, b, 0x0AC080 | (ea << 8), 0x30, seed, mode, false);
                for (unsigned cc = 0; cc < 16; cc++) {
                    check(a, b, 0x0AC0A0 | (ea << 8) | cc, 0x30, seed, mode, false);
                    if (seed < 16) {
                        check(a, b, 0x0BC0A0 | (ea << 8) | cc, 0x30, seed, mode, false);
                        check(a, b, 0x0BC0A0 | (ea << 8) | cc, 0x30, seed, mode, true);
                    }
                }
            }
        }
        for (unsigned alu = 0; alu < 256; alu++) {
            if (alu == 0x04 || alu == 0x08 || alu == 0x0C ||
                alu == 0x15) {
                continue;
            }
            for (unsigned seed = 0; seed < 16; seed++) {
                check(a, b, 0x200000 | alu, 0, seed, mode, false);
            }
        }
        for (unsigned seed = 0; seed < 192; seed++) {
            unsigned input = seed < 64 ? seed : 0x10000 | (seed - 64);
            static const uint32_t max_moves[] = {
                0x200000, 0x800000, 0xC08000, 0x808000, 0xC00000
            };
            for (unsigned pm = 0; pm < ARRAY_SIZE(max_moves); pm++) {
                check(a, b, max_moves[pm] | 0x1D, 0, input, mode, false);
            }
            for (unsigned d = 0; d < 2; d++) {
                for (unsigned src = 0; src < 7; src++) {
                    uint32_t alu = src == 0 ? 0x07 :
                                   src < 5 ? 0x47 + ((src - 1) << 4) :
                                   src == 5 ? 0x27 : 0x37;
                    alu |= d << 3;
                    /* No move, both stores, both loads and mixed X/Y
                     * transfers also test operand snapshots and liveness. */
                    static const uint32_t moves[] = {
                        0x200000, 0x800000, 0xC08000, 0x808000, 0xC00000
                    };
                    for (unsigned pm = 0; pm < ARRAY_SIZE(moves); pm++) {
                        check(a, b, moves[pm] | alu, 0, input, mode, false);
                    }
                }
            }
        }
        for (unsigned edge = 0; edge < 128; edge++) {
            for (unsigned d = 0; d < 2; d++) {
                for (unsigned y = 0; y < 2; y++) {
                    for (unsigned sub = 0; sub < 2; sub++) {
                        uint32_t alu = 0x21 | (y << 4) | (sub << 2) | (d << 3);
                        check(a, b, 0x200000 | alu, 0, 0x10000 | edge, mode, false);
                        check(a, b, 0x800000 | alu, 0, 0x10000 | edge, mode, false);
                    }
                }
            }
        }
        for (unsigned seed = 0; seed < 64; seed++) {
            for (unsigned d = 0; d < 2; d++) {
                for (unsigned y = 0; y < 2; y++) {
                    for (unsigned sub = 0; sub < 2; sub++) {
                        uint32_t alu = 0x21 | (y << 4) | (sub << 2) | (d << 3);
                        check(a, b, 0x200000 | alu, 0, seed, mode, false);
                        /* Parallel X/Y stores must keep both addresses and
                         * both source values live across the carry ALU. */
                        check(a, b, 0x800000 | alu, 0, seed, mode, false);
                    }
                }
                for (unsigned g = 0; g < 8; g++) {
                    if (g == 0 || g >= 4) {
                        check(a, b, 0x0C1FF0 | (g << 1) | d, 0, seed, mode, false);
                    }
                }
            }
            for (unsigned reg = DSP_REG_X0; reg <= DSP_REG_M7; reg++) {
                for (unsigned dir = 0; dir < 2; dir++) {
                    /* DMA next/start registers have no launch side effect. */
                    check(a, b, 0x084014 | (dir << 15) | (reg << 8), 0, seed, mode, false);
                }
            }
            check(a, b, 0x0D0030, 0, seed, mode, false);
            check(a, b, 0x0D0030, 0, seed, mode, true);
            for (unsigned ea = 0; ea < 64; ea++) {
                if ((ea >> 3) == 6 && (ea & 7) != 0) {
                    continue;
                }
                check(a, b, 0x0BC080 | (ea << 8), 0x30, seed, mode, false);
                check(a, b, 0x0BC080 | (ea << 8), 0x30, seed, mode, true);
            }
            for (unsigned bit = 0; bit < 16; bit++) {
                for (unsigned set = 0; set < 2; set++) {
                    uint32_t btest = (set << 5) | bit;
                    check(a, b, 0x0A8580 | btest, 0x30, seed, mode, false);
                    check(a, b, 0x0A0080 | (0x20 << 8) | btest, 0x30, seed, mode, false);
                    check(a, b, 0x0AC000 | (DSP_REG_A << 8) | btest, 0x30, seed, mode, false);
                    check(a, b, 0x0A4080 | (0x20 << 8) | btest, 0x30, seed, mode, false);
                    check(a, b, 0x0A4080 | (0x30 << 8) | btest, 0x30, seed, mode, false);
                }
            }
        }
    }
    check_parallel_discard_and_self_moves(a, b);
    check_register_bits(a, b);
    check_call_chains(a, b);
    check_loop_cache_identity(a, b);
done:
    assert(dspjit_test_mode_count(jit) == 8);
    assert(dspjit_test_committed_bytes(jit) >= initially_committed);
    assert(dspjit_test_committed_bytes(jit) <= (4u << 20));
#ifdef _WIN32
    /* A focused matrix can fit in the initial committed page. */
    if (argc == 1) assert(dspjit_test_committed_bytes(jit) > initially_committed);
#endif
    a->dsp->ops->finalize(a->dsp);
    b->dsp->ops->finalize(b->dsp);
    free(a->dsp);
    free(b->dsp);
    free(a);
    free(b);
    printf("native DSP: %u instruction boundaries agree; zero interpreter calls\n", cases);
    return 0;
}
