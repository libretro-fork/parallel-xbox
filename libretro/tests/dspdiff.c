/*
 * The DSP JIT against the interpreter: a DSP program run frame by frame on
 * both, from the same state, with the same memory around them, and the
 * whole state compared after every frame (registers, stack, X, Y, P, the
 * mixbuffer, the loop and interrupt state; the scratch RAM and FIFOs the
 * DMA moves data through). Any difference is a bug in one of them.
 *
 *   dspdiff [options] program
 *     program         a56's dd lines (libretro/tests/rom/gp.inc), or a
 *                     dump: "DSPD" then P (4096 words), X (4096), Y (2048)
 *                     as 32-bit little-endian words
 *     --frames N      frames to run (default 100)
 *     --seed S        the random data in X, Y, the scratch RAM and the
 *                     FIFOs (default 1); 0: zeros
 *     --x ADDR=VALUE  an X word set after the random fill (the test ROM's
 *                     pass count at X:0, say); --y likewise
 *     --max-cycles C  a frame that has not halted after C cycles fails
 *                     (default 50000000)
 *
 * Exit 0 when every frame matched, 1 on a difference or a frame that did
 * not halt, 2 when the host has no JIT (nothing to compare).
 *
 * Built by the core's Makefile (make dspdiff) with the core's own DSP
 * sources; libretro/tests/run.sh runs it on the test ROM's GP program.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/dsp.h"
#include "hw/xbox/mcpx/apu/dsp/dsp_internal.h"
#include "hw/xbox/mcpx/apu/dsp/interp/dsp_cpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* what the core's DSP sources need of QEMU's trace points (off) */
int trace_events_enabled_count;
uint16_t _TRACE_DSP56K_EXECUTE_INSTRUCTION_DISASM_DSTATE;

/* what the core's DSP sources need of GLib */
void *g_malloc0(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) {
        abort();
    }
    return p;
}

void g_free(void *p)
{
    free(p);
}

void g_assertion_message_expr(const char *domain, const char *file, int line,
                              const char *func, const char *expr)
{
    fprintf(stderr, "%s:%d: %s: assertion failed: %s\n", file, line, func, expr ? expr : "");
    abort();
}

/* The memory around a DSP: what dsp.c and gp_ep.c give it, flat */
#define SCRATCH_BYTES (1u << 20)
#define FIFO_COUNT 4
#define FIFO_BYTES (1u << 16)

typedef struct Machine {
    DSPState *dsp;
    uint8_t scratch[SCRATCH_BYTES];
    uint8_t fifo[FIFO_COUNT][FIFO_BYTES];
    uint32_t fifo_cur[FIFO_COUNT];
} Machine;

#define INTERRUPT_START_FRAME (1 << 1)
#define INTERRUPT_DMA_EOL (1 << 7)

/* dsp.c's peripherals */
uint32_t read_peripheral(DSPState *dsp, uint32_t address)
{
    uint32_t v = 0xababa;
    switch (address) {
    case 0xFFFFB3:
        v = 0;
        break;
    case 0xFFFFC5:
        v = dsp->interrupts;
        if (dsp->dma.eol) {
            v |= INTERRUPT_DMA_EOL;
        }
        break;
    case 0xFFFFD4:
        v = dsp_dma_read(&dsp->dma, DMA_NEXT_BLOCK);
        break;
    case 0xFFFFD5:
        v = dsp_dma_read(&dsp->dma, DMA_START_BLOCK);
        break;
    case 0xFFFFD6:
        v = dsp_dma_read(&dsp->dma, DMA_CONTROL);
        break;
    case 0xFFFFD7:
        v = dsp_dma_read(&dsp->dma, DMA_CONFIGURATION);
        break;
    }
    return v;
}

void write_peripheral(DSPState *dsp, uint32_t address, uint32_t value)
{
    switch (address) {
    case 0xFFFFC4:
        if (value & 1) {
            dsp->ops->set_halt_requested(dsp, true);
        }
        break;
    case 0xFFFFC5:
        dsp->interrupts &= ~value;
        if (value & INTERRUPT_DMA_EOL) {
            dsp->dma.eol = false;
        }
        break;
    case 0xFFFFD4:
        dsp_dma_write(&dsp->dma, DMA_NEXT_BLOCK, value);
        break;
    case 0xFFFFD5:
        dsp_dma_write(&dsp->dma, DMA_START_BLOCK, value);
        break;
    case 0xFFFFD6:
        dsp_dma_write(&dsp->dma, DMA_CONTROL, value);
        break;
    case 0xFFFFD7:
        dsp_dma_write(&dsp->dma, DMA_CONFIGURATION, value);
        break;
    }
}

void dsp_start_frame_impl(DSPState *dsp)
{
    dsp->interrupts |= INTERRUPT_START_FRAME;
}

static void scratch_walk(void *opaque, uint32_t addr, size_t len, bool dir,
                         dsp_dma_run_func fn, void *user)
{
    Machine *m = opaque;

    addr %= SCRATCH_BYTES;
    while (len > 0) {
        size_t run = SCRATCH_BYTES - addr;
        if (run > len) {
            run = len;
        }
        fn(user, &m->scratch[addr], run);
        addr = (addr + run) % SCRATCH_BYTES;
        len -= run;
    }
}

static void fifo_walk(void *opaque, unsigned int index, size_t len, bool dir,
                      dsp_dma_run_func fn, void *user)
{
    Machine *m = opaque;
    uint32_t cur;

    index %= FIFO_COUNT;
    cur = m->fifo_cur[index];
    while (len > 0) {
        size_t run = FIFO_BYTES - cur;
        if (run > len) {
            run = len;
        }
        fn(user, &m->fifo[index][cur], run);
        cur = (cur + run) % FIFO_BYTES;
        len -= run;
    }
    m->fifo_cur[index] = cur;
}

static uint32_t rng_state;
static uint32_t rng(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state >> 8;
}

static void fill_random(uint8_t *p, size_t n)
{
    if (rng_state == 0) {
        memset(p, 0, n);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        p[i] = rng();
    }
}

static Machine *machine_new(bool jit)
{
    Machine *m = calloc(1, sizeof(*m));
    DSPState *dsp;

    if (!m) {
        abort();
    }
    dsp = calloc(1, sizeof(*dsp));
    if (!dsp) {
        abort();
    }
    dsp->is_gp = true;
    dsp->core.is_gp = true;
    dsp->dma.rw_opaque = m;
    dsp->dma.scratch_walk = scratch_walk;
    dsp->dma.fifo_walk = fifo_walk;
    if (jit) {
        dsp_jit_init(dsp);
    } else {
        dsp_c_init(dsp);
    }
    dsp->ops->reset(dsp);
    m->dsp = dsp;
    return m;
}

static DspCoreState dump_core;
static bool has_core;

static int load_program(const char *path, uint32_t *p, uint32_t *x, uint32_t *y, bool *has_xy)
{
    FILE *f = fopen(path, "rb");
    char magic[4];

    if (!f) {
        perror(path);
        return -1;
    }
    *has_xy = false;
    if (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "DSPD", 4)) {
        uint8_t buf[4];
        for (int i = 0; i < DSP_PRAM_SIZE + DSP_XRAM_SIZE + DSP_YRAM_SIZE; i++) {
            if (fread(buf, 1, 4, f) != 4) {
                fprintf(stderr, "%s: short dump\n", path);
                fclose(f);
                return -1;
            }
            uint32_t v = buf[0] | buf[1] << 8 | buf[2] << 16 | (uint32_t)buf[3] << 24;
            if (i < DSP_PRAM_SIZE) {
                p[i] = v;
            } else if (i < DSP_PRAM_SIZE + DSP_XRAM_SIZE) {
                x[i - DSP_PRAM_SIZE] = v;
            } else {
                y[i - DSP_PRAM_SIZE - DSP_XRAM_SIZE] = v;
            }
        }
        *has_xy = true;
        /* a dump may carry the core's state too (a program caught between
         * frames, where it halts) */
        if (fread(magic, 1, 4, f) == 4 && !memcmp(magic, "CORE", 4)) {
            if (fread(&dump_core, sizeof(dump_core), 1, f) == 1) {
                has_core = true;
            }
        }
        fclose(f);
        return 0;
    }
    rewind(f);
    {
        char line[256];
        int n = 0;
        while (fgets(line, sizeof(line), f)) {
            char *s = line;
            unsigned long v;
            while (*s == ' ' || *s == '\t') {
                s++;
            }
            if (strncmp(s, "dd", 2)) {
                continue;
            }
            v = strtoul(s + 2, NULL, 0);
            if (n < DSP_PRAM_SIZE) {
                p[n++] = v & 0xFFFFFF;
            }
        }
        fclose(f);
        if (n == 0) {
            fprintf(stderr, "%s: no dd lines\n", path);
            return -1;
        }
    }
    return 0;
}

static int compare(const char *what, const void *a, const void *b, size_t n, size_t width)
{
    const uint8_t *pa = a, *pb = b;

    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) {
            size_t at = i / width;
            fprintf(stderr, "  %s differs at %zu (interpreter %08x, jit %08x)\n",
                    what, at,
                    width == 4 ? ((const uint32_t *)a)[at] : width == 2 ? ((const uint16_t *)a)[at] : pa[at],
                    width == 4 ? ((const uint32_t *)b)[at] : width == 2 ? ((const uint16_t *)b)[at] : pb[at]);
            return 1;
        }
    }
    return 0;
}

static int compare_machines(Machine *a, Machine *b)
{
    /* the state sync_to_vm fills: the DSPState's own */
    DspCoreState *sa = &a->dsp->core, *sb = &b->dsp->core;
    int bad = 0;

    a->dsp->ops->sync_to_vm(a->dsp);
    b->dsp->ops->sync_to_vm(b->dsp);
    bad |= compare("pc", &sa->pc, &sb->pc, sizeof(sa->pc), 4);
    bad |= compare("register", sa->registers, sb->registers, sizeof(sa->registers), 4);
    bad |= compare("stack", sa->stack, sb->stack, sizeof(sa->stack), 4);
    bad |= compare("X", sa->xram, sb->xram, sizeof(sa->xram), 4);
    bad |= compare("Y", sa->yram, sb->yram, sizeof(sa->yram), 4);
    bad |= compare("P", sa->pram, sb->pram, sizeof(sa->pram), 4);
    bad |= compare("mixbuffer", sa->mixbuffer, sb->mixbuffer, sizeof(sa->mixbuffer), 4);
    bad |= compare("cycle_count", &sa->cycle_count, &sb->cycle_count, 4, 4);
    bad |= compare("halt", &sa->halt_requested, &sb->halt_requested, 1, 1);
    bad |= compare("loop_rep", &sa->loop_rep, &sb->loop_rep, 4, 4);
    bad |= compare("pc_on_rep", &sa->pc_on_rep, &sb->pc_on_rep, 4, 4);
    bad |= compare("interrupt_state", &sa->interrupt_state, &sb->interrupt_state, 2, 2);
    bad |= compare("interrupt_counter", &sa->interrupt_counter, &sb->interrupt_counter, 2, 2);
    bad |= compare("interrupt_pipeline_count", &sa->interrupt_pipeline_count, &sb->interrupt_pipeline_count, 2, 2);
    bad |= compare("interrupt_is_pending", sa->interrupt_is_pending, sb->interrupt_is_pending, sizeof(sa->interrupt_is_pending), 2);
    bad |= compare("interrupts", &a->dsp->interrupts, &b->dsp->interrupts, 4, 4);
    bad |= compare("scratch", a->scratch, b->scratch, sizeof(a->scratch), 1);
    bad |= compare("fifo", a->fifo, b->fifo, sizeof(a->fifo), 1);
    bad |= compare("fifo_cur", a->fifo_cur, b->fifo_cur, sizeof(a->fifo_cur), 4);
    return bad;
}

/* dsp_thread_frame's frame: until the program halts */
static int run_frame(Machine *m, uint32_t max_cycles)
{
    DSPState *dsp = m->dsp;
    uint32_t cycles = 0;

    dsp->ops->start_frame(dsp);
    dsp->ops->set_halt_requested(dsp, false);
    dsp->ops->set_cycle_count(dsp, 0);
    do {
        dsp->ops->run(dsp, 1000);
        cycles += 1000;
    } while (!dsp->ops->get_halt_requested(dsp) && cycles < max_cycles);
    return dsp->ops->get_halt_requested(dsp) ? 0 : -1;
}

int main(int argc, char **argv)
{
    static uint32_t p[DSP_PRAM_SIZE], x[DSP_XRAM_SIZE], y[DSP_YRAM_SIZE];
    const char *path = NULL;
    int frames = 100;
    uint32_t seed = 1;
    uint32_t max_cycles = 50000000;
    struct { int space; uint32_t addr, value; } sets[32];
    int nsets = 0;
    bool has_xy;
    Machine *a, *b;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
            seed = strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(argv[i], "--max-cycles") && i + 1 < argc) {
            max_cycles = strtoul(argv[++i], NULL, 0);
        } else if ((!strcmp(argv[i], "--x") || !strcmp(argv[i], "--y")) && i + 1 < argc && nsets < 32) {
            char *eq;
            sets[nsets].space = argv[i][2] == 'x' ? 'X' : 'Y';
            sets[nsets].addr = strtoul(argv[i + 1], &eq, 0);
            sets[nsets].value = eq && *eq == '=' ? strtoul(eq + 1, NULL, 0) : 0;
            nsets++;
            i++;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "dspdiff: unknown option %s\n", argv[i]);
            return 1;
        } else {
            path = argv[i];
        }
    }
    if (!path) {
        fprintf(stderr, "usage: dspdiff [--frames N] [--seed S] [--x ADDR=VALUE] [--y ADDR=VALUE] [--max-cycles C] program\n");
        return 1;
    }
    if (load_program(path, p, x, y, &has_xy)) {
        return 1;
    }

    a = machine_new(false);
    b = machine_new(true);
    if (b->dsp->ops == a->dsp->ops) {
        fprintf(stderr, "dspdiff: no JIT on this host: nothing to compare\n");
        return 2;
    }

    /* the same world for both */
    rng_state = seed;
    fill_random(a->scratch, sizeof(a->scratch));
    fill_random((uint8_t *)a->fifo, sizeof(a->fifo));
    memcpy(b->scratch, a->scratch, sizeof(a->scratch));
    memcpy(b->fifo, a->fifo, sizeof(a->fifo));
    if (!has_xy) {
        for (int i = 0; i < DSP_XRAM_SIZE; i++) {
            x[i] = seed ? rng() & 0xFFFFFF : 0;
        }
        for (int i = 0; i < DSP_YRAM_SIZE; i++) {
            y[i] = seed ? rng() & 0xFFFFFF : 0;
        }
    }
    for (int i = 0; i < nsets; i++) {
        if (sets[i].space == 'X' && sets[i].addr < DSP_XRAM_SIZE) {
            x[sets[i].addr] = sets[i].value & 0xFFFFFF;
        } else if (sets[i].space == 'Y' && sets[i].addr < DSP_YRAM_SIZE) {
            y[sets[i].addr] = sets[i].value & 0xFFFFFF;
        }
    }
    for (int k = 0; k < 2; k++) {
        DSPState *dsp = (k ? b : a)->dsp;
        for (int i = 0; i < DSP_PRAM_SIZE; i++) {
            dsp->ops->write_memory(dsp, 'P', i, p[i]);
        }
        for (int i = 0; i < DSP_XRAM_SIZE; i++) {
            if (i < 0xC00) {
                dsp->ops->write_memory(dsp, 'X', i, x[i]);
            }
        }
        for (int i = 0; i < DSP_MIXBUFFER_SIZE; i++) {
            dsp->ops->write_memory(dsp, 'X', DSP_MIXBUFFER_BASE + i, x[0xC00 + i] & 0xFFFFFF);
        }
        for (int i = 0; i < DSP_YRAM_SIZE; i++) {
            dsp->ops->write_memory(dsp, 'Y', i, y[i]);
        }
    }

    if (has_core) {
        for (int k = 0; k < 2; k++) {
            DSPState *dsp = (k ? b : a)->dsp;
            dsp->core = dump_core;
            dsp->ops->sync_from_vm(dsp);
        }
    }

    for (int frame = 0; frame < frames; frame++) {
        int ra = run_frame(a, max_cycles);
        int rb = run_frame(b, max_cycles);
        if (ra || rb) {
            fprintf(stderr, "dspdiff: frame %d: the program did not halt within %u cycles (interpreter %s, jit %s)\n",
                    frame, max_cycles, ra ? "running" : "halted", rb ? "running" : "halted");
            return 1;
        }
        if (compare_machines(a, b)) {
            fprintf(stderr, "dspdiff: frame %d: the JIT's state differs from the interpreter's (above)\n", frame);
            return 1;
        }
    }
    printf("dspdiff: %d frames, %s: the JIT and the interpreter agree\n", frames, path);
    return 0;
}
