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
static void require_empty_opcache(dsp_core_t *c)
{
    bool empty = true;
    for (unsigned pc = 0; pc < DSP_PRAM_SIZE; pc++) empty &= c->pram_opcache[pc] == NULL;
    REQUIRE(empty);
    REQUIRE(!c->pram_opcache_words);
}
static void prime_opcache(dsp_core_t *c, unsigned pc)
{
    c->pc = pc;
    c->pram[pc] = 0; /* nonparallel NOP populates the interpreter decoder cache */
    c->is_idle = false;
    c->loop_rep = 0;
    c->interrupt_state = DSP_INTERRUPT_NONE;
    dsp56k_execute_instruction(c);
    REQUIRE(c->pram_opcache[pc] != NULL);
    REQUIRE(c->pram_opcache_words & (UINT64_C(1) << (pc >> 6)));
}
static void backend_opcache_checks(void)
{
    for (unsigned jit = 0; jit < 2; jit++) {
        Machine *m = machine_new(jit);
        dsp_core_t *c = m->dsp->backend;
        const uint64_t group_bytes = 64 * sizeof(*c->pram_opcache);
        uint64_t bytes = dsp56k_test_opcache_clear_bytes();
        require_empty_opcache(c);
        for (unsigned i = 0; i < 1000; i++) m->dsp->ops->invalidate_opcache(m->dsp);
        REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes);
        if (jit) {
            c->pram[0] = 0x200013;
            c->pram[1] = 0x08f484;
            c->pram[2] = 1;
            normal(m, 0, true);
            m->dsp->ops->invalidate_opcache(m->dsp);
            require_empty_opcache(c);
            REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes);
        }
        for (unsigned phase = 0; phase < 2; phase++) {
            prime_opcache(c, 0);
            prime_opcache(c, 63);
            prime_opcache(c, DSP_PRAM_SIZE - 1);
            m->dsp->ops->invalidate_opcache(m->dsp);
            REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes + (phase + 1) * 2 * group_bytes);
            require_empty_opcache(c);
            m->dsp->ops->invalidate_opcache(m->dsp);
            REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes + (phase + 1) * 2 * group_bytes);
        }
        /* Per-word eviction must not incorrectly mark other entries empty. */
        prime_opcache(c, 0);
        prime_opcache(c, 63);
        m->dsp->ops->write_memory(m->dsp, 'P', 0, 1);
        REQUIRE(c->pram_opcache[0] == NULL);
        REQUIRE(c->pram_opcache[63] != NULL);
        m->dsp->ops->invalidate_opcache(m->dsp);
        require_empty_opcache(c);
        /* Even eviction of the last pointer can retain a conservative marker. */
        prime_opcache(c, 0);
        m->dsp->ops->write_memory(m->dsp, 'P', 0, 1);
        bytes = dsp56k_test_opcache_clear_bytes();
        m->dsp->ops->invalidate_opcache(m->dsp);
        REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes + group_bytes);
        require_empty_opcache(c);
        prime_opcache(c, 63);
        bytes = dsp56k_test_opcache_clear_bytes();
        m->dsp->ops->bootstrap(m->dsp);
        REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes + group_bytes);
        require_empty_opcache(c);
        m->dsp->ops->bootstrap(m->dsp);
        REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes + group_bytes);
        prime_opcache(c, 63);
        bytes = dsp56k_test_opcache_clear_bytes();
        m->dsp->ops->sync_to_vm(m->dsp);
        m->dsp->core.pram[63] = 0x08f484;
        m->dsp->core.pram[64] = 1;
        m->dsp->core.pc = 63;
        m->dsp->ops->sync_from_vm(m->dsp);
        REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes + group_bytes);
        require_empty_opcache(c);
        c->is_idle = false;
        dsp56k_execute_instruction(c);
        REQUIRE(c->is_idle); /* restored HALT must not reuse the old cached NOP */
        m->dsp->ops->reset(m->dsp);
        REQUIRE(c->pram_opcache_words); /* CPU reset preserves program memory */
        m->dsp->ops->invalidate_opcache(m->dsp);
        require_empty_opcache(c);
        /* Every group, both boundary entries: duplicate publications share
         * one clear and a disjoint reuse must not retain the old summary. */
        for (unsigned group = 0; group < 64; group++) {
            prime_opcache(c, group * 64);
            prime_opcache(c, group * 64 + 63);
            REQUIRE(c->pram_opcache_words == (UINT64_C(1) << group));
            bytes = dsp56k_test_opcache_clear_bytes();
            m->dsp->ops->invalidate_opcache(m->dsp);
            REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes + group_bytes);
            require_empty_opcache(c);
            m->dsp->ops->invalidate_opcache(m->dsp);
            REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes + group_bytes);
        }
        /* Dense summaries retain the bulk path, whether each group has
         * one published slot or all of its slots. Re-arm after each clear. */
        for (unsigned stride = 64; stride; stride = stride == 64 ? 1 : 0) {
            for (unsigned pc = 0; pc < DSP_PRAM_SIZE; pc += stride) prime_opcache(c, pc);
            REQUIRE(c->pram_opcache_words == UINT64_MAX);
            bytes = dsp56k_test_opcache_clear_bytes();
            m->dsp->ops->invalidate_opcache(m->dsp);
            REQUIRE(dsp56k_test_opcache_clear_bytes() == bytes + sizeof(c->pram_opcache));
            require_empty_opcache(c);
        }
        dispose(m);
    }
}
static void fresh_interrupt_fallback_checks(void)
{
    for (unsigned mode = 0; mode < 4; mode++) {
        Machine *m = machine_new(true);
        dsp_core_t *c = m->dsp->backend;
        REQUIRE(c->jit);
        c->pc = 0;
        c->pram[0] = 0x0ace78; /* deliberate legacy out-of-width BSET #24,A */
        c->registers[DSP_REG_SR] = mode << DSP_SR_S0;
        c->interrupt_state = DSP_INTERRUPT_DISABLED;
        c->interrupt_pipeline_count = 0;
        c->interrupt_instr_fetch = 0x20;
        c->interrupt_save_pc = 0x60;
        c->interrupt_ipl_to_raise = 2;
        c->is_idle = false;
        m->dsp->save_cycles = 0;
        uint64_t calls = dspjit_test_flush_calls();
        uint64_t fallback = dspjit_test_fallback_count();
        uint64_t entries = dspjit_test_entry_count();
        m->dsp->ops->run(m->dsp, 1);
        REQUIRE(dspjit_test_flush_calls() == calls);
        REQUIRE(dspjit_test_fallback_count() == fallback + 1);
        REQUIRE(dspjit_test_entry_count() == entries);
        dispose(m);
    }
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
    uint64_t coverage_words = dspjit_test_flush_coverage_words();
    uint64_t bitmap_words = dspjit_test_flush_bitmap_words();
    uint64_t resets = dspjit_test_flush_resets();
    if (!bench) {
        for (unsigned i = 0; i < 32; i++) dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_resets() == resets);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes);
        REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words);
        REQUIRE(dspjit_test_flush_coverage_words() == coverage_words);
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
        bitmap_words = dspjit_test_flush_bitmap_words();
        coverage_words = dspjit_test_flush_coverage_words();
        unsigned iterations = bench_single ? 10000 : 100000;
        double start = seconds();
        for (unsigned i = 0; i < iterations; i++) {
            if (bench_single) normal(m, 0, true);
            dspjit_flush(c->jit);
        }
        printf("%s flush: %.9f seconds, %" PRIu64 " table resets, %" PRIu64 " pointer bytes, %" PRIu64 " bitmap words, %" PRIu64 " coverage words\n",
               bench_single ? "single-mode" : "empty", seconds()-start,
               dspjit_test_flush_resets()-resets, dspjit_test_flush_pointer_bytes()-bytes,
               dspjit_test_flush_bitmap_words()-bitmap_words,
               dspjit_test_flush_coverage_words()-coverage_words);
        dispose(m);
        return 0;
    }
    if (argc != 1) return 2;
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 1);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 8 * entry_bytes);
    REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 8);
    REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 1);
    for (unsigned i = 0; i < 32; i++) dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 1);
    /* Deliberately replace PRAM directly: only flush can retire the old CLR. */
    c->pram[0] = 0;
    for (unsigned mode = 0; mode < 8; mode++) normal(m, mode, false);
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 2);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 16 * entry_bytes);
    REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 16);
    REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 2);
    for (unsigned i = 0; i < 32; i++) dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 2);
    /* All modes remain allocated. Publish one entry in one identity, then
     * retire only that entry, including LF-set identities. */
    for (unsigned mode = 0; mode < 8; mode++) {
        c->pram[0] = (mode & 1) ? 0x200013 : 0;
        normal(m, mode, !!(mode & 1));
        bytes = dspjit_test_flush_pointer_bytes();
        bitmap_words = dspjit_test_flush_bitmap_words();
        coverage_words = dspjit_test_flush_coverage_words();
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
        REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 1);
        REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 1);
        REQUIRE(dspjit_test_flush_resets() == resets + 3 + mode);
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
        REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 1);
        REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 1);
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
    bitmap_words = dspjit_test_flush_bitmap_words();
    coverage_words = dspjit_test_flush_coverage_words();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + sizeof(pcs)/sizeof(*pcs) * entry_bytes);
    REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 4);
    REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 4);
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + sizeof(pcs)/sizeof(*pcs) * entry_bytes);
    REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 4);
    REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 4);
    for (unsigned i = 0; i < sizeof(pcs)/sizeof(*pcs); i++) {
        c->pram[pcs[i]] = 0;
        normal_at(m, 0, pcs[i], false);
    }
    bytes = dspjit_test_flush_pointer_bytes();
    bitmap_words = dspjit_test_flush_bitmap_words();
    coverage_words = dspjit_test_flush_coverage_words();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + sizeof(pcs)/sizeof(*pcs) * entry_bytes);
    REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 4);
    REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 4);
    /* A later disjoint publication must not inherit old bitmap entries. */
    c->pram[300] = 0;
    c->pram[301] = 0x08f484;
    c->pram[302] = 1;
    normal_at(m, 0, 300, false);
    bytes = dspjit_test_flush_pointer_bytes();
    bitmap_words = dspjit_test_flush_bitmap_words();
    coverage_words = dspjit_test_flush_coverage_words();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
    REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 1);
    REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 1);
    /* Reuse a previously populated bitmap word with a disjoint PC set. */
    c->pram[8] = 0;
    c->pram[9] = 0x08f484;
    c->pram[10] = 1;
    normal_at(m, 0, 8, false);
    bytes = dspjit_test_flush_pointer_bytes();
    bitmap_words = dspjit_test_flush_bitmap_words();
    coverage_words = dspjit_test_flush_coverage_words();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
    REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 1);
    REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 1);
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
        uint64_t flush_calls = dspjit_test_flush_calls();
        m->dsp->ops->run(m->dsp, 1);
        REQUIRE(dspjit_test_flush_calls() == flush_calls);
        REQUIRE(dspjit_test_fallback_count() == fallback + 1);
        REQUIRE(c->loop_rep);
        resets = dspjit_test_flush_resets();
        bytes = dspjit_test_flush_pointer_bytes();
        bitmap_words = dspjit_test_flush_bitmap_words();
        coverage_words = dspjit_test_flush_coverage_words();
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_resets() == resets + 1);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
        REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 1);
        REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 1);
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
        REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 2);
        REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 2);
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
                bitmap_words = dspjit_test_flush_bitmap_words();
                coverage_words = dspjit_test_flush_coverage_words();
                dspjit_flush(c->jit);
                REQUIRE(dspjit_test_flush_resets() == resets + phase + 1);
                REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + entry_bytes);
                REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 1);
                REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 1);
                dspjit_flush(c->jit);
                REQUIRE(dspjit_test_flush_resets() == resets + phase + 1);
                REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 1);
                REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 1);
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
    bitmap_words = dspjit_test_flush_bitmap_words();
    coverage_words = dspjit_test_flush_coverage_words();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 3 * entry_bytes);
    REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 3);
    REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 2);
    c->pram[0] = c->pram[4] = c->pram[8] = 0;
    partial(m, 8, false, false);
    partial(m, 4, true, false);
    normal(m, 0, false);
    bytes = dspjit_test_flush_pointer_bytes();
    bitmap_words = dspjit_test_flush_bitmap_words();
    coverage_words = dspjit_test_flush_coverage_words();
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 3 * entry_bytes);
    REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 3);
    REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 2);
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
        bitmap_words = dspjit_test_flush_bitmap_words();
        coverage_words = dspjit_test_flush_coverage_words();
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 128 * entry_bytes);
        REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 2);
        REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 3);
        REQUIRE(dspjit_test_flush_resets() == resets + phase + 1);
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_pointer_bytes() == bytes + 128 * entry_bytes);
        REQUIRE(dspjit_test_flush_bitmap_words() == bitmap_words + 2);
        REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 3);
    }
    dispose(m);
    /* A block ending at PRAM wrap must retain high-address coverage. */
    m = machine_new(true);
    c = m->dsp->backend;
    c->pram[4093] = 0x200013;
    c->pram[4094] = 0x08f484;
    c->pram[4095] = 1;
    normal_at(m, 0, 4093, true);
    uint64_t scans = dspjit_test_invalidation_scans();
    uint64_t entries = dspjit_test_entry_count();
    m->dsp->ops->write_memory(m->dsp, 'P', 4093, 0);
    REQUIRE(dspjit_test_invalidation_scans() == scans + 1);
    normal_at(m, 0, 4093, false);
    REQUIRE(dspjit_test_entry_count() == entries + 1);
    dispose(m);

    /* Fill every coverage word, exercising the full-bitmap bulk clear. */
    m = machine_new(true);
    c = m->dsp->backend;
    resets = dspjit_test_flush_resets();
    for (unsigned phase = 0; phase < 2; phase++) {
        for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) c->pram[i] = phase ? 0x200013 : 0;
        for (unsigned pc = 0; pc < DSP_PRAM_SIZE; pc += 64) {
            c->pc = pc;
            c->registers[DSP_REG_SR] = 0;
            c->registers[DSP_REG_A0] = c->registers[DSP_REG_A2] = 0;
            c->registers[DSP_REG_A1] = 0x123456;
            c->is_idle = false;
            m->dsp->save_cycles = 0;
            m->dsp->ops->run(m->dsp, 1);
            REQUIRE(c->registers[DSP_REG_A1] == (phase ? 0 : 0x123456));
        }
        REQUIRE(dspjit_test_flush_resets() == resets + phase);
        coverage_words = dspjit_test_flush_coverage_words();
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 64);
        dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_coverage_words() == coverage_words + 64);
    }
    dispose(m);
    REQUIRE(dspjit_test_fallback_count() == 4); /* one deliberate legacy REP call per scale */
    backend_opcache_checks();
    fresh_interrupt_fallback_checks();
    REQUIRE(dspjit_test_fallback_count() == 8); /* deliberate legacy REP/IRQ cases only */
    printf("DSP flush: %u empty/normal/REP/interrupt cache checks pass\n",checks);
    return 0;
}
