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
static double timer_seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER tick, frequency;
    QueryPerformanceCounter(&tick);
    QueryPerformanceFrequency(&frequency);
    return (double)tick.QuadPart / frequency.QuadPart;
#else
    struct timespec tick;
    clock_gettime(CLOCK_MONOTONIC, &tick);
    return tick.tv_sec + tick.tv_nsec * 1e-9;
#endif
}

static unsigned checks;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"DSP invalidation line %u: %s\n",__LINE__,#x); exit(1); } checks++; } while (0)
static void execute(Machine *m, unsigned start, unsigned target, unsigned mode, bool clear)
{
    dsp_core_t *c = m->dsp->backend;
    c->pc = start;
    c->registers[DSP_REG_SR] = ((mode & 3) << DSP_SR_S0) | ((mode & 4) ? (1 << DSP_SR_LF) : 0);
    c->registers[DSP_REG_LA] = 0xfff;
    c->registers[DSP_REG_LC] = 5;
    c->registers[DSP_REG_A0] = c->registers[DSP_REG_A2] = 0;
    c->registers[DSP_REG_A1] = 0x123456;
    c->is_idle = false;
    m->dsp->save_cycles = 0;
    m->dsp->ops->run(m->dsp, 10000);
    if (!c->is_idle) fprintf(stderr, "did not halt start=%u target=%u mode=%u PC=%u\n", start,target,mode,c->pc);
    REQUIRE(c->is_idle);
    REQUIRE(c->registers[DSP_REG_A1] == (clear ? 0 : 0x123456));
}
static Machine *prime(void)
{
    static const unsigned entries[] = {0, 1, 16, 32, 63, 64, 65, 127};
    Machine *m = machine_new(true);
    dsp_core_t *c = m->dsp->backend;
    REQUIRE(c->jit != NULL);
    memset(c->pram, 0, sizeof(c->pram));
    c->pram[128] = 0x08f484;
    c->pram[129] = 1;
    for (unsigned mode = 0; mode < 8; mode++) {
        for (unsigned i = 0; i < sizeof(entries) / sizeof(*entries); i++)
            execute(m, entries[i], entries[i], mode, false);
    }
    REQUIRE(dspjit_test_mode_count(c->jit) == 8);
    return m;
}
static void dispose(Machine *m)
{
    m->dsp->ops->finalize(m->dsp);
    free(m->dsp);
    free(m);
}
int main(int argc, char **argv)
{
    static const unsigned targets[] = {1, 16, 32, 63, 64, 65, 127};
    static const unsigned entries[] = {0, 1, 16, 32, 63, 64, 65, 127};
    if (argc == 2 && !strcmp(argv[1], "--bench")) {
        Machine *m = prime();
        dsp_core_t *c = m->dsp->backend;
        uint64_t before = dspjit_test_invalidation_scans();
        double start = timer_seconds();
        for (unsigned i = 0; i < 1000000; i++)
            m->dsp->ops->write_memory(m->dsp, 'P', 32, (i & 1) ? 0 : 0x200013);
        double seconds = timer_seconds() - start;
        REQUIRE(c->pram[32] == 0 && c->jit_code_changed);
        printf("invalidation benchmark: %.6f seconds, %" PRIu64 " scans, final P[32]=%06x\n",
               seconds, dspjit_test_invalidation_scans() - before, c->pram[32]);
        dispose(m);
        return 0;
    }
    if (argc != 1) return 2;
    for (unsigned t = 0; t < sizeof(targets) / sizeof(*targets); t++) {
        Machine *m = prime();
        dsp_core_t *c = m->dsp->backend;
        unsigned target = targets[t];
        uint64_t before = dspjit_test_invalidation_scans();
        for (unsigned phase = 0; phase < 2; phase++) {
            for (unsigned i = 0; i < 17; i++) {
                uint32_t value = ((i & 1) == phase) ? 0x200013 : 0;
                m->dsp->ops->write_memory(m->dsp, 'P', target, value);
                REQUIRE(dspjit_test_invalidation_scans() == before + phase + 1);
                REQUIRE(c->jit_code_changed);
            }
            /* All overlapping entries in all scaling/LF identities must
             * execute the replacement, not a stale block at another PC. */
            for (unsigned mode = 0; mode < 8; mode++) {
                for (unsigned i = 0; i < sizeof(entries) / sizeof(*entries); i++) {
                    if (entries[i] <= target) execute(m, entries[i], target, mode, phase == 0);
                }
            }
        }
        /* Recompilation rearmed coverage; another change must scan again. */
        m->dsp->ops->write_memory(m->dsp, 'P', target, 0x200013);
        REQUIRE(dspjit_test_invalidation_scans() == before + 3);
        execute(m, target, target, 0, true);
        dispose(m);
    }
    REQUIRE(dspjit_test_fallback_count() == 0);
    printf("DSP invalidation: %u overlapping-block/rearm checks pass; zero interpreter fallbacks\n", checks);
    return 0;
}
