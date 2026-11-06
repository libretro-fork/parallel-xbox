/* SPDX-License-Identifier: GPL-2.0-or-later */
int dspdiff_main(int argc, char **argv);
#define main dspdiff_main
#include "dspdiff.c"
#undef main
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
static unsigned checks;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"DSP flush line %u: %s\n",__LINE__,#x); exit(1); } checks++; } while (0)
static void dispose(Machine *m)
{
    m->dsp->ops->finalize(m->dsp);
    free(m->dsp);
    free(m);
}
static void normal_at(Machine *m, unsigned mode, unsigned pc, bool clear)
{
    dsp_core_t *c = m->dsp->backend;
    c->pc = pc;
    c->registers[DSP_REG_SR] = ((mode & 3) << DSP_SR_S0) | ((mode & 4) ? (1 << DSP_SR_LF) : 0);
    c->registers[DSP_REG_LA] = 0xfff;
    c->registers[DSP_REG_LC] = 5;
    c->registers[DSP_REG_A0] = c->registers[DSP_REG_A2] = 0;
    c->registers[DSP_REG_A1] = 0x123456;
    c->is_idle = false;
    c->loop_rep = 0;
    m->dsp->save_cycles = 0;
    m->dsp->ops->run(m->dsp, 10000);
    REQUIRE(c->is_idle);
    REQUIRE(c->registers[DSP_REG_A1] == (clear ? 0 : 0x123456));
}
static void normal(Machine *m, unsigned mode, bool clear)
{
    normal_at(m, mode, 0, clear);
}
static void partial(Machine *m, unsigned pc, bool rep, bool clear)
{
    dsp_core_t *c = m->dsp->backend;
    c->pc = pc;
    c->registers[DSP_REG_SR] = 0;
    c->registers[DSP_REG_SP] = 0;
    c->registers[DSP_REG_LC] = 4;
    c->registers[DSP_REG_A0] = c->registers[DSP_REG_A2] = 0;
    c->registers[DSP_REG_A1] = 0x123456;
    c->loop_rep = rep;
    c->interrupt_state = rep ? DSP_INTERRUPT_NONE : DSP_INTERRUPT_DISABLED;
    c->interrupt_pipeline_count = 0;
    c->interrupt_instr_fetch = rep ? -1 : 0x20;
    c->interrupt_save_pc = 0x60;
    c->interrupt_ipl_to_raise = 2;
    c->is_idle = false;
    m->dsp->save_cycles = 0;
    m->dsp->ops->run(m->dsp, 1);
    REQUIRE(c->registers[DSP_REG_A1] == (clear ? 0 : 0x123456));
}
static double seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER tick, frequency;
    QueryPerformanceCounter(&tick); QueryPerformanceFrequency(&frequency);
    return (double)tick.QuadPart / frequency.QuadPart;
#else
    struct timespec tick;
    clock_gettime(CLOCK_MONOTONIC, &tick);
    return tick.tv_sec + tick.tv_nsec * 1e-9;
#endif
}
int main(int argc, char **argv)
{
    bool bench_single = argc == 2 && !strcmp(argv[1], "--bench-single");
    bool bench = bench_single || (argc == 2 && !strcmp(argv[1], "--bench"));
    if (argc != 1 && !bench) return 2;
    Machine *m = machine_new(true);
    dsp_core_t *c = m->dsp->backend;
    REQUIRE(c->jit);
    const uint64_t entry_bytes = sizeof(void *);
    uint64_t bytes = dspjit_test_flush_pointer_bytes();
    uint64_t resets = dspjit_test_flush_resets();
    if (!bench) {
        for (unsigned i = 0; i < 32; i++) dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_resets() == resets);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes);
    }
    c->pram[0] = 0x200013;
    c->pram[1] = 0x08f484;
    c->pram[2] = 1;
    for (unsigned mode = 0; mode < 8; mode++) normal(m, mode, true);
    REQUIRE(dspjit_test_mode_count(c->jit) == 8);
    if (bench) {
        dspjit_flush(c->jit);
        resets = dspjit_test_flush_resets();
        bytes = dspjit_test_flush_pointer_bytes();
        unsigned iterations = bench_single ? 10000 : 100000;
        double start = seconds();
        for (unsigned i = 0; i < iterations; i++) {
            if (bench_single) normal(m, 0, true);
            dspjit_flush(c->jit);
        }
        printf("%s flush: %.9f seconds, %" PRIu64 " table resets, %" PRIu64 " pointer bytes\n",
               bench_single ? "single-mode" : "empty", seconds()-start,
               dspjit_test_flush_resets()-resets, dspjit_test_flush_pointer_bytes()-bytes);
        dispose(m);
        return 0;
    }
    if (argc != 1) return 2;
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 1);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 8 * entry_bytes);
    for (unsigned i = 0; i < 32; i++) dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 1);
    /* Deliberately replace PRAM directly: only flush can retire the old CLR. */
    c->pram[0] = 0;
    for (unsigned mode = 0; mode < 8; mode++) normal(m, mode, false);
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 2);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 16 * entry_bytes);
    for (unsigned i = 0; i < 32; i++) dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 2);
    /* All modes remain allocated. Publish one entry in one identity, then
     * retire only that entry, including LF-set identities. */
    for (unsigned mode = 0; mode < 8; mode++) {
        c->pram[0] = (mode & 1) ? 0x200013 : 0;
        normal(m, mode, !!(mode & 1));
        bytes = dspjit_test_flush_pointer_bytes();
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
        REQUIRE(dspjit_test_flush_resets() == resets + 3 + mode);
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
    }
    /* Entry bitmaps must include far/boundary PCs and deduplicate an entry
     * invalidated and recompiled before the eventual flush. */
    static const unsigned pcs[] = {0,3,63,66,127,130,4093};
    for (unsigned i = 0; i < sizeof(pcs)/sizeof(*pcs); i++) {
        unsigned pc = pcs[i];
        c->pram[pc] = 0x200013;
        c->pram[pc + 1] = 0x08f484;
        c->pram[pc + 2] = 1;
        normal_at(m, 0, pc, true);
    }
    m->dsp->ops->write_memory(m->dsp, 'P', 3, 0);
    normal_at(m, 0, 3, false);
    m->dsp->ops->write_memory(m->dsp, 'P', 66, 0); /* invalidated, not republished */
    bytes = dspjit_test_flush_pointer_bytes();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + sizeof(pcs)/sizeof(*pcs) * entry_bytes);
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + sizeof(pcs)/sizeof(*pcs) * entry_bytes);
    for (unsigned i = 0; i < sizeof(pcs)/sizeof(*pcs); i++) {
        c->pram[pcs[i]] = 0;
        normal_at(m, 0, pcs[i], false);
    }
    bytes = dspjit_test_flush_pointer_bytes();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + sizeof(pcs)/sizeof(*pcs) * entry_bytes);
    /* A later disjoint publication must not inherit old bitmap entries. */
    c->pram[300] = 0;
    c->pram[301] = 0x08f484;
    c->pram[302] = 1;
    normal_at(m, 0, 300, false);
    bytes = dspjit_test_flush_pointer_bytes();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
    dispose(m);

    /* A cached REP fallback need not allocate native arena bytes. It must
     * still dirty the tables and be reconsidered after flush. */
    for (unsigned mode = 0; mode < 4; mode++) {
        m = machine_new(true);
        c = m->dsp->backend;
        c->registers[DSP_REG_SR] = mode << DSP_SR_S0;
        c->pram[0] = 0x0ace78; /* deliberate legacy out-of-width BSET #24,A */
        c->loop_rep = 1;
        c->pc = 0;
        c->registers[DSP_REG_LC] = 4;
        c->is_idle = false;
        m->dsp->save_cycles = 0;
        uint64_t fallback = dspjit_test_fallback_count();
        m->dsp->ops->run(m->dsp, 1);
        REQUIRE(dspjit_test_fallback_count() == fallback + 1);
        REQUIRE(c->loop_rep);
        resets = dspjit_test_flush_resets();
        bytes = dspjit_test_flush_pointer_bytes();
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_resets() == resets + 1);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_resets() == resets + 1);
        c->pram[0] = 0x200013;
        c->pc = 0;
        c->registers[DSP_REG_LC] = 4;
        c->registers[DSP_REG_A0] = c->registers[DSP_REG_A2] = 0;
        c->registers[DSP_REG_A1] = 0x123456;
        m->dsp->save_cycles = 0;
        m->dsp->ops->run(m->dsp, 1);
        REQUIRE(c->registers[DSP_REG_A1] == 0);
        REQUIRE(dspjit_test_fallback_count() == fallback + 1);
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_resets() == resets + 2);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 2 * entry_bytes);
        dispose(m);
    }

    /* Start with interrupt-only cache entries, without a normal-block
     * publication that could conceal a missing dirty marker. */
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned stage = 0; stage < 6; stage++) {
            m = machine_new(true);
            c = m->dsp->backend;
            memset(c->pram, 0, sizeof(c->pram));
            resets = dspjit_test_flush_resets();
            uint64_t fallback = dspjit_test_fallback_count();
            for (unsigned phase = 0; phase < 2; phase++) {
                c->pram[0x40] = phase ? 0 : 0x200013;
                c->pc = 0x40;
                c->registers[DSP_REG_SR] = mode << DSP_SR_S0;
                c->registers[DSP_REG_SP] = 0;
                c->registers[DSP_REG_A0] = c->registers[DSP_REG_A2] = 0;
                c->registers[DSP_REG_A1] = 0x123456;
                c->interrupt_state = DSP_INTERRUPT_DISABLED;
                c->interrupt_pipeline_count = stage;
                c->interrupt_instr_fetch = 0x20;
                c->interrupt_save_pc = 0x60;
                c->interrupt_ipl_to_raise = 2;
                c->is_idle = false;
                m->dsp->save_cycles = 0;
                m->dsp->ops->run(m->dsp, 1);
                REQUIRE(c->registers[DSP_REG_A1] == (phase ? 0x123456 : 0));
                REQUIRE(dspjit_test_fallback_count() == fallback);
                bytes = dspjit_test_flush_pointer_bytes();
                dspjit_flush(c->jit);
                REQUIRE(dspjit_test_flush_resets() == resets + phase + 1);
                REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
                dspjit_flush(c->jit);
                REQUIRE(dspjit_test_flush_resets() == resets + phase + 1);
            }
            dispose(m);
        }
    }
    /* Three kinds share a mode allocation but must never share entry bits. */
    m = machine_new(true);
    c = m->dsp->backend;
    memset(c->pram, 0, sizeof(c->pram));
    c->pram[0] = c->pram[4] = c->pram[8] = 0x200013;
    c->pram[1] = 0x08f484;
    c->pram[2] = 1;
    normal(m, 0, true);
    partial(m, 4, true, true);
    partial(m, 8, false, true);
    bytes = dspjit_test_flush_pointer_bytes();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 3 * entry_bytes);
    c->pram[0] = c->pram[4] = c->pram[8] = 0;
    partial(m, 8, false, false);
    partial(m, 4, true, false);
    normal(m, 0, false);
    bytes = dspjit_test_flush_pointer_bytes();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 3 * entry_bytes);
    dispose(m);

    /* Fully occupied first/last bitmap words exercise the dense bulk path
     * and the last legal cache-entry index, without needing a large arena. */
    m = machine_new(true);
    c = m->dsp->backend;
    memset(c->pram, 0, sizeof(c->pram));
    resets = dspjit_test_flush_resets();
    for (unsigned phase = 0; phase < 2; phase++) {
        for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) c->pram[i] = phase ? 0x200013 : 0;
        uint64_t entries = dspjit_test_entry_count();
        for (unsigned group = 0; group < 2; group++) {
            for (unsigned index = 0; index < 64; index++) {
                c->pc = group ? DSP_PRAM_SIZE - 64 + index : index;
                c->registers[DSP_REG_SR] = 0;
                c->registers[DSP_REG_A0] = c->registers[DSP_REG_A2] = 0;
                c->registers[DSP_REG_A1] = 0x123456;
                c->is_idle = false;
                m->dsp->save_cycles = 0;
                m->dsp->ops->run(m->dsp, 1);
                REQUIRE(c->registers[DSP_REG_A1] == (phase ? 0 : 0x123456));
            }
        }
        REQUIRE(dspjit_test_entry_count() == entries + 128);
        REQUIRE(dspjit_test_flush_resets() == resets + phase);
        bytes = dspjit_test_flush_pointer_bytes();
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 128 * entry_bytes);
        REQUIRE(dspjit_test_flush_resets() == resets + phase + 1);
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 128 * entry_bytes);
    }
    dispose(m);
    REQUIRE(dspjit_test_fallback_count() == 4); /* one deliberate legacy REP call per scale */
    printf("DSP flush: %u empty/normal/REP/interrupt cache checks pass\n",checks);
    return 0;
}
