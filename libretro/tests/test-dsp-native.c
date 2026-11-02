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
                        uint32_t expected_sr = sr;
                        if (shift) {
                            unsigned c = shift <= 24 ? (word >> (24 - shift)) & 1 : 0;
                            expected_sr = (sr & ~0xF) | c |
                                ((result >> 23) << DSP_SR_N) | ((result == 0) << DSP_SR_Z);
                        }
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

static void check_startup_operations(Machine *a, Machine *b, unsigned mode)
{
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

static void check_program_transfers(Machine *a, Machine *b, unsigned mode)
{
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
    bool relative_only = argc == 2 && !strcmp(argv[1], "--relative");
    bool loops_only = argc == 2 && !strcmp(argv[1], "--loops");
    bool compare_only = argc == 2 && !strcmp(argv[1], "--compare");
    bool register_only = argc == 2 && !strcmp(argv[1], "--register-control");
    bool effective_only = argc == 2 && !strcmp(argv[1], "--effective-control");
    bool startup_only = argc == 2 && !strcmp(argv[1], "--startup");
    bool program_only = argc == 2 && !strcmp(argv[1], "--program");
    if (argc != 1 && !relative_only && !loops_only && !program_only && !compare_only && !startup_only && !effective_only && !register_only) {
        fputs("usage: dspnative [--relative|--loops|--program|--compare|--startup|--effective-control|--register-control]\n", stderr);
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
    if (relative_only || loops_only || program_only || compare_only || startup_only || effective_only || register_only) {
        for (unsigned mode = 0; mode < 4; mode++) {
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
    assert(dspjit_test_committed_bytes(jit) > initially_committed);
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
