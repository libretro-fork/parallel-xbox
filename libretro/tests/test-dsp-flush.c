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
static void normal(Machine *m, unsigned mode, bool clear)
{
    dsp_core_t *c = m->dsp->backend;
    c->pc = 0;
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
    bool bench = argc == 2 && !strcmp(argv[1], "--bench");
    if (argc != 1 && !bench) return 2;
    Machine *m = machine_new(true);
    dsp_core_t *c = m->dsp->backend;
    REQUIRE(c->jit);
    uint64_t resets = dspjit_test_flush_resets();
    if (!bench) {
        for (unsigned i = 0; i < 32; i++) dspjit_flush(c->jit);
        REQUIRE(dspjit_test_flush_resets() == resets);
    }
    c->pram[0] = 0x200013;
    c->pram[1] = 0x08f484;
    c->pram[2] = 1;
    for (unsigned mode = 0; mode < 8; mode++) normal(m, mode, true);
    REQUIRE(dspjit_test_mode_count(c->jit) == 8);
    if (bench) {
        dspjit_flush(c->jit);
        resets = dspjit_test_flush_resets();
        double start = seconds();
        for (unsigned i = 0; i < 100000; i++) dspjit_flush(c->jit);
        printf("empty flush: %.9f seconds, %" PRIu64 " table resets\n",seconds()-start,
               dspjit_test_flush_resets()-resets);
        dispose(m);
        return 0;
    }
    if (argc != 1) return 2;
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 1);
    for (unsigned i = 0; i < 32; i++) dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 1);
    /* Deliberately replace PRAM directly: only flush can retire the old CLR. */
    c->pram[0] = 0;
    for (unsigned mode = 0; mode < 8; mode++) normal(m, mode, false);
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 2);
    for (unsigned i = 0; i < 32; i++) dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 2);
    dispose(m);

    /* A cached REP fallback need not allocate native arena bytes. It must
     * still dirty the tables and be reconsidered after flush. */
    m = machine_new(true);
    c = m->dsp->backend;
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
    dspjit_flush(c->jit);
    REQUIRE(dspjit_test_flush_resets() == resets + 1);
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
    dispose(m);
    /* Start with interrupt-only cache entries, without a normal-block
     * publication that could conceal a missing dirty marker. */
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned stage = 0; stage < 6; stage++) {
            m = machine_new(true);
            c = m->dsp->backend;
            memset(c->pram, 0, sizeof(c->pram));
            resets = dspjit_test_flush_resets();
            fallback = dspjit_test_fallback_count();
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
                dspjit_flush(c->jit);
                REQUIRE(dspjit_test_flush_resets() == resets + phase + 1);
                dspjit_flush(c->jit);
                REQUIRE(dspjit_test_flush_resets() == resets + phase + 1);
            }
            dispose(m);
        }
    }
    printf("DSP flush: %u empty/normal/REP/interrupt cache checks pass\n",checks);
    return 0;
}
