/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
static void failed(const char *what, unsigned line)
{
    fprintf(stderr, "quiet progress: line %u: %s\n", line, what);
    exit(1);
}
#define assert(c) ((c) ? (void)0 : failed(#c, __LINE__))
#define LR_QUIET_INSNS 8192
#define LR_QUIET_EXITS 16
#define TB_EXIT_REQUESTED 2
#define CF_COUNT_MASK 0xFFFF
#define MIN(a,b) ((a) < (b) ? (a) : (b))
typedef uint64_t vaddr;
typedef struct TranslationBlock { int icount, cflags; } TranslationBlock;
typedef struct CPUState {
    struct { int lr_quiet; struct { struct { uint16_t low; } u16; } icount_decr; } neg;
    int lr_quiet_at_exit, lr_quiet_exits, icount_budget, icount_extra, cflags_next_tb;
    bool lr_poll_wait, idle, requested, interrupt;
} CPUState;
static unsigned waits, proofs, refills, paces, useful_work;
static int returned_exit;
static bool return_null, use_icount = true;
static void trace_exec_tb(TranslationBlock *tb, vaddr pc) { (void)tb; (void)pc; }
static TranslationBlock *cpu_tb_exec(CPUState *cpu, TranslationBlock *tb, int *exit)
{
    (void)cpu;
    useful_work++;
    *exit = returned_exit;
    return return_null ? NULL : tb;
}
static bool cpu_kernel_idle_wait(CPUState *cpu, TranslationBlock **last)
{
    proofs++;
    if (cpu->interrupt) {
        cpu->interrupt = false;
        cpu->idle = false;
        *last = NULL;
    }
    return cpu->idle;
}
static void lr_quiet_exit(CPUState *cpu) { waits++; cpu->requested = true; }
static bool cpu_loop_exit_requested(CPUState *cpu) { return cpu->requested; }
static bool icount_enabled(void) { return use_icount; }
static void icount_update(CPUState *cpu) { (void)cpu; refills++; }
static void xemu_cpu_pace_refill(CPUState *cpu) { (void)cpu; paces++; }
static void retro_atomic_store_relaxed_u16(uint16_t *p, uint16_t v) { *p = v; }
/* PRODUCTION */

int main(void)
{
    TranslationBlock block = {.icount = 100, .cflags = 0x10000};
    for (unsigned poll = 0; poll < 2; poll++) {
        for (unsigned idle = 0; idle < 2; idle++) {
            for (unsigned irq = 0; irq < 2; irq++) {
                for (unsigned null = 0; null < 2; null++) {
                    CPUState cpu = {.neg.lr_quiet = -1, .lr_poll_wait = poll,
                                    .idle = idle, .interrupt = irq, .icount_budget = 20};
                    TranslationBlock *last = &block;
                    waits = proofs = refills = 0;
                    returned_exit = TB_EXIT_REQUESTED;
                    return_null = null;
                    int exit;
                    cpu_loop_exec_tb(&cpu, &block, 0, &last, &exit);
                    bool waiting = poll || (idle && !irq);
                    assert(waits == waiting);
                    assert(refills == (unsigned)(!waiting && !null));
                    assert(cpu.neg.lr_quiet == LR_QUIET_INSNS && !cpu.lr_poll_wait);
                    assert(proofs == (unsigned)!poll);
                    if (refills) {
                        assert(cpu.neg.icount_decr.u16.low == 20);
                        assert(cpu.cflags_next_tb == 0x10014);
                    }
                }
            }
        }
    }
    /* Repeated store-free computation remains runnable at every budget
     * boundary; it must never acquire a synthetic timer wait. */
    CPUState cpu = {.icount_budget = 400};
    TranslationBlock *last = &block;
    return_null = false;
    waits = refills = useful_work = 0;
    for (unsigned i = 0; i < 10000; i++) {
        cpu.neg.lr_quiet = -1;
        int exit;
        cpu_loop_exec_tb(&cpu, &block, i, &last, &exit);
    }
    assert(useful_work == 10000 && refills == 10000 && waits == 0);
    puts("quiet progress: active computation, verified waits and interrupt release passed");
    return 0;
}
