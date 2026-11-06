/* SPDX-License-Identifier: GPL-2.0-or-later */
int dspdiff_main(int argc, char **argv);
#define main dspdiff_main
#include "dspdiff.c"
#undef main
#include "hw/xbox/mcpx/apu/dsp/dsp_dma_regs.h"

static dsp_dma_mem_read_func original_read;
static dsp_dma_mem_write_func original_write;
static unsigned payload_reads, payload_writes;
static uint32_t count_read(void *opaque, int space, uint32_t addr)
{
    if (space == DSP_SPACE_P) payload_reads++;
    return original_read(opaque, space, addr);
}
static void count_write(void *opaque, int space, uint32_t addr, uint32_t value)
{
    if (space == DSP_SPACE_P) payload_writes++;
    original_write(opaque, space, addr, value);
}
static void issue(Machine *m, bool output)
{
    dsp_core_t *c = m->dsp->backend;
    uint32_t descriptor[] = { NODE_POINTER_EOL,
        (2 << 10) | (15 << 5) | (output ? NODE_CONTROL_DIRECTION : 0),
        3, 0x2800, 0, output ? 32 : 0, 63 };
    memcpy(c->xram + 0x100, descriptor, sizeof(descriptor));
    payload_reads = payload_writes = 0;
    dsp_dma_write(&m->dsp->dma, DMA_NEXT_BLOCK, 0x100);
    dsp_dma_write(&m->dsp->dma, DMA_CONTROL, DMA_CONTROL_ACTION_START);
}
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"DMA span line %u: %s\n",__LINE__,#x); return 1; } checks++; } while (0)
int main(void)
{
    unsigned checks = 0;
    static const uint32_t counts[] = { 0, 1, 2, 32, 2048, 4096, UINT32_MAX };
    for (unsigned jit = 0; jit < 2; jit++) {
        Machine *m = machine_new(jit);
        dsp_core_t *c = m->dsp->backend;
        DSPDMAState *dma = &m->dsp->dma;
        original_read = dma->mem_read;
        original_write = dma->mem_write;
        dma->mem_read = count_read;
        dma->mem_write = count_write;
        for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) c->pram[i] = (i * 0x12345) & 0xffffff;
        for (unsigned addr = 0; addr <= DSP_PRAM_SIZE; addr++) {
            for (unsigned n = 0; n < sizeof(counts) / sizeof(*counts); n++) {
                uint32_t count = counts[n];
                bool valid = count <= DSP_PRAM_SIZE - addr;
                uint32_t *ptr = dma->mem_ptr(c, DSP_SPACE_P, addr, count, false);
                REQUIRE(ptr == (valid ? c->pram + addr : NULL));
                REQUIRE(dma->mem_ptr(c, DSP_SPACE_P, addr, count, true) == NULL);
            }
        }
        REQUIRE(!dma->mem_ptr(c, DSP_SPACE_P, UINT32_MAX - 1, 3, false));
        /* Every forbidden bit at every position, including reduction tails
         * and valid prefixes immediately before the poisoned word. */
        for (unsigned bit = 24; bit < 32; bit++) {
            for (unsigned i = 0; i < DSP_PRAM_SIZE; i++) {
                uint32_t old = c->pram[i];
                c->pram[i] |= UINT32_C(1) << bit;
                REQUIRE(!dma->mem_ptr(c, DSP_SPACE_P, 0, DSP_PRAM_SIZE, false));
                REQUIRE(dma->mem_ptr(c, DSP_SPACE_P, i, 0, false) == c->pram + i);
                REQUIRE(!dma->mem_ptr(c, DSP_SPACE_P, i, 1, false));
                REQUIRE(!dma->mem_ptr(c, DSP_SPACE_P, 0, i + 1, false));
                REQUIRE(dma->mem_ptr(c, DSP_SPACE_P, 0, i, false) == c->pram);
                c->pram[i] = old;
            }
        }
        /* Prime CLR A in both execution engines, then replace it through
         * incoming DMA. Bypassing P write callbacks would leave stale code. */
        c->pram[0] = 0x200013;
        c->pram[1] = 0x08f484;
        c->pram[2] = 1;
        m->dsp->ops->invalidate_opcache(m->dsp);
        c->pc = 0;
        c->registers[DSP_REG_A1] = 0x123456;
        REQUIRE(!run_frame(m, 10000, false));
        REQUIRE(c->registers[DSP_REG_A1] == 0);
        issue(m, true);
        REQUIRE(payload_reads == 0 && payload_writes == 0);
        REQUIRE(!memcmp(m->scratch + 32, c->pram, 12));
        uint32_t replacement[] = { 0, 0x08f484, 1 };
        memcpy(m->scratch, replacement, sizeof(replacement));
        issue(m, false);
        REQUIRE(payload_reads == 0 && payload_writes == 3);
        c->pc = 0;
        c->registers[DSP_REG_A1] = 0x123456;
        REQUIRE(!run_frame(m, 10000, false));
        REQUIRE(c->registers[DSP_REG_A1] == 0x123456);
        m->dsp->ops->finalize(m->dsp);
        free(m->dsp);
        free(m);
    }
    printf("DMA program spans: %u range/direction/invalidation checks pass\n", checks);
    return 0;
}
