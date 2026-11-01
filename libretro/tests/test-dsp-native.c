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
}

static void check(Machine *a, Machine *b, uint32_t op, uint32_t ext,
                  unsigned seed, unsigned mode, bool long_irq)
{
    prepare(a, op, ext, seed, mode, long_irq);
    prepare(b, op, ext, seed, mode, long_irq);
    dsp_core_t *ca = a->dsp->backend;
    a->dsp->ops->step(a->dsp);
    ca->cycle_count += ca->instr_cycle;
    dspjit_test_reset_fallback_count();
    b->dsp->ops->run(b->dsp, 1);
    if (dspjit_test_fallback_count() || compare_machines(a, b)) {
        fprintf(stderr, "native DSP case %u: op=%06x ext=%x seed=%u mode=%u irq=%d fallbacks=%llu\n",
                cases, op, ext, seed, mode, long_irq,
                (unsigned long long)dspjit_test_fallback_count());
        exit(1);
    }
    cases++;
}

/* Reuse the same translated PC across LF changes without resetting or
 * flushing the JIT. LA deliberately matches even when LF is clear. */
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

static void check_movec(Machine *a, Machine *b)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned r1 = DSP_REG_M0; r1 <= DSP_REG_SR; r1++) {
            if (r1 > DSP_REG_M7 && r1 != DSP_REG_SR) {
                continue;
            }
            for (unsigned r2 = DSP_REG_X0; r2 <= DSP_REG_M7; r2++) {
                for (unsigned dir = 0; dir < 2; dir++) {
                    if (dir && r1 == DSP_REG_SR) {
                        continue; /* writes may change the JIT scaling mode */
                    }
                    for (unsigned seed = 0; seed < 16; seed++) {
                        check(a, b, 0x0440A0 | (dir << 15) | (r2 << 8) | r1,
                              0, seed, mode, false);
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

int main(void)
{
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
    check_macr_rounding(a, b);
    check_rotate_carry(a, b);
    check_max_transfer(a, b);
    check_tcc(a, b);
    check_movec(a, b);
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
    check_loop_cache_identity(a, b);
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
