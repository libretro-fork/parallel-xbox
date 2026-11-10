/* SPDX-License-Identifier: GPL-2.0-or-later */
int dspdiff_main(int argc, char **argv);
#define main dspdiff_main
#include "dspdiff.c"
#undef main
#include "hw/xbox/mcpx/apu/dsp/dsp_dma_regs.h"

static dsp_dma_mem_read_func original_read;
static dsp_dma_mem_write_func original_write;
static unsigned payload_reads, payload_writes;
static dsp_dma_mem_descriptor_func original_descriptor;
static bool track_header, bad_header_order, track_chain;
static dsp_scratch_walk_func original_scratch;
static unsigned chain_walks;
static int header_space;
static uint32_t header_addr;
static unsigned header_reads, header_batches;
static void track_descriptor(void *opaque, int space, uint32_t addr, DSPDMADescriptor *out)
{
    if (track_header && (space == header_space || track_chain) && addr == header_addr) header_batches++;
    memset(out, 0xA5, sizeof(*out)); /* every returned metadata word must be defined */
    original_descriptor(opaque, space, addr, out);
}
static void track_walk(void *opaque, uint32_t addr, size_t len, bool dir,
                       dsp_dma_run_func fn, void *user)
{
    if (track_chain && ++chain_walks > 3) { fprintf(stderr,"descriptor chain failed to terminate\n"); exit(1); }
    original_scratch(opaque, addr, len, dir, fn, user);
}
static uint32_t count_read(void *opaque, int space, uint32_t addr)
{
    if (track_header && (space == header_space || track_chain) && addr >= header_addr && addr < header_addr + 7) {
        if (addr != header_addr + header_reads % 7) bad_header_order = true;
        header_reads++;
    }
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
static unsigned peripheral_sequence;
static uint32_t ordered_peripheral(dsp_core_t *core, uint32_t addr)
{
    (void)core;
    return (++peripheral_sequence << 16) | (addr & 0xffff);
}
static void seed_header(dsp_core_t *c, const uint32_t *words)
{
    for (unsigned i = 0; i < 7; i++) original_write(c, header_space, header_addr + i, words[i]);
}
static uint32_t node_address(int space, uint32_t addr)
{
    return addr + (space == DSP_SPACE_Y ? 0x1800 : space == DSP_SPACE_P ? 0x2800 : 0);
}
static void reset_header_counts(void)
{
    header_reads = header_batches = 0;
    bad_header_order = false;
    track_header = true;
}
static int descriptor_checks(unsigned *total)
{
    unsigned checks = 0;
    static const struct { int space; uint32_t addr; } locations[] = {
        {DSP_SPACE_X,0}, {DSP_SPACE_X,1}, {DSP_SPACE_X,0x3fa}, {DSP_SPACE_X,0x7fa}, {DSP_SPACE_X,0xbf9},
        {DSP_SPACE_X,0xbfa}, /* straddles X RAM and the lower mixbuffer alias */
        {DSP_SPACE_X,0xc00}, {DSP_SPACE_X,0xff9},
        {DSP_SPACE_X,0x1400}, {DSP_SPACE_X,0x17f9},
        {DSP_SPACE_Y,0}, {DSP_SPACE_Y,1}, {DSP_SPACE_Y,0x7f9},
        {DSP_SPACE_P,0}, {DSP_SPACE_P,1}, {DSP_SPACE_P,0x100}, {DSP_SPACE_P,0xff9}
    };
    static const unsigned formats[] = {1,2,6};
    for (unsigned jit = 0; jit < 2; jit++) {
        Machine *m = machine_new(jit);
        dsp_core_t *c = m->dsp->backend;
        DSPDMAState *dma = &m->dsp->dma;
        original_read = dma->mem_read;
        original_write = dma->mem_write;
        original_descriptor = dma->mem_descriptor;
        REQUIRE(original_descriptor != NULL);
        dma->mem_read = count_read;
        dma->mem_descriptor = track_descriptor;
        original_scratch = dma->scratch_walk;
        dma->scratch_walk = track_walk;
        for (unsigned loc = 0; loc < ARRAY_SIZE(locations); loc++) {
            header_space = locations[loc].space;
            header_addr = locations[loc].addr;
            for (unsigned reject = 0; reject < 2; reject++) {
                dma->mem_descriptor = reject ? NULL : track_descriptor;
                for (unsigned path = 0; path < 3; path++) { /* input, output, interleaved output */
                    for (unsigned f = 0; f < ARRAY_SIZE(formats); f++) {
                        track_header = false;
                        memset(c->xram, 0, sizeof(c->xram));
                        memset(c->yram, 0, sizeof(c->yram));
                        memset(c->pram, 0, sizeof(c->pram));
                        memset(c->mixbuffer, 0, sizeof(c->mixbuffer));
                        memset(m->scratch, 0x5a, sizeof(m->scratch));
                        uint32_t words[] = {0x123456,0xabcdef,0x654321,0x765432};
                        memcpy(c->xram + 0x40, words, sizeof(words));
                        unsigned n = path == 2 ? 4 : 3;
                        unsigned size = formats[f] == 1 ? 2 : 4;
                        uint8_t expected[16];
                        for (unsigned i = 0; i < n; i++) {
                            unsigned index = path == 2 ? (i % 2) * 2 + i / 2 : i;
                            uint32_t v = words[index];
                            if (size == 2) { uint16_t short_v = v >> 8; memcpy(expected + i * size, &short_v, size); }
                            else memcpy(expected + i * size, &v, size);
                        }
                        if (!path) memcpy(m->scratch + 37, expected, n * size);
                        uint32_t header[] = {NODE_POINTER_EOL,
                            (formats[f] << 10) | (15 << 5) | 16 | (path ? NODE_CONTROL_DIRECTION : 0) | (path == 2),
                            path == 2 ? 33 : 3, 0x40, 5, 32, 127};
                        seed_header(c, header);
                        reset_header_counts();
                        dsp_dma_write(dma, DMA_NEXT_BLOCK, node_address(header_space, header_addr));
                        dsp_dma_write(dma, DMA_CONTROL, DMA_CONTROL_ACTION_START);
                        REQUIRE(header_batches == (reject ? 0 : 1));
                        REQUIRE(header_reads == (reject ? 7 : 0));
                        REQUIRE(!bad_header_order);
                        REQUIRE(dma->next_block == NODE_POINTER_EOL && dma->eol);
                        track_header = false;
                        REQUIRE(original_read(c, header_space, header_addr + 4) == 5);
                        if (path) REQUIRE(!memcmp(m->scratch + 37, expected, n * size));
                        else for (unsigned i = 0; i < n; i++) {
                            uint32_t v;
                            if (size == 2) { uint16_t short_v; memcpy(&short_v, expected + i * size, size); v = (uint32_t)short_v << 8; }
                            else { memcpy(&v, expected + i * size, size); v &= 0xffffff; }
                            REQUIRE(c->xram[0x40 + i] == v);
                        }
                    }
                }
            }
        }
        /* Ring input overwrites its own header across two walker callbacks.
         * Count, control, next node and scratch offset must remain snapshots. */
        for (unsigned bank = 0; bank < 4; bank++) {
            header_space = bank == 2 ? DSP_SPACE_Y : bank == 3 ? DSP_SPACE_P : DSP_SPACE_X;
            header_addr = bank == 1 ? 0xbfa : 0x100;
            for (unsigned reject = 0; reject < 2; reject++) for (unsigned wb = 0; wb < 2; wb++) {
                track_header = false;
                dma->mem_descriptor = reject ? NULL : track_descriptor;
                uint32_t data[7]; for (unsigned i = 0; i < 7; i++) data[i] = 0x100 + i * 17;
                memcpy(m->scratch + 32 + 120, data, 8);
                memcpy(m->scratch + 32, (uint8_t *)data + 8, 20);
                uint32_t header[] = {NODE_POINTER_EOL, (2 << 10) | (14 << 5) | (wb ? 16 : 0),
                    7, node_address(header_space, header_addr), 120, 32, 127};
                seed_header(c, header);
                reset_header_counts();
                dsp_dma_write(dma, DMA_NEXT_BLOCK, node_address(header_space, header_addr));
                dsp_dma_write(dma, DMA_CONTROL, DMA_CONTROL_ACTION_START);
                REQUIRE(header_batches == (reject ? 0 : 1));
                REQUIRE(header_reads == (reject ? 7 : 0));
                REQUIRE(!bad_header_order);
                REQUIRE(dma->next_block == NODE_POINTER_EOL);
                track_header = false;
                for (unsigned i = 0; i < 7; i++) REQUIRE(original_read(c, header_space, header_addr + i) == (wb && i == 4 ? 20 : data[i]));
            }
        }
        /* Reborrow each node across a linked X -> Y -> P chain. */
        for (unsigned reject = 0; reject < 2; reject++) {
            track_header = false; dma->mem_descriptor = reject ? NULL : track_descriptor;
            header_addr = 0x100;
            for (unsigned bank = 0; bank < 3; bank++) {
                header_space = bank == 0 ? DSP_SPACE_X : bank == 1 ? DSP_SPACE_Y : DSP_SPACE_P;
                uint32_t header[] = {bank == 2 ? NODE_POINTER_EOL : node_address(bank == 0 ? DSP_SPACE_Y : DSP_SPACE_P, 0x100),
                    (2 << 10) | (15 << 5),3,0x40 + bank * 16,0,32 + bank * 16,127};
                seed_header(c, header);
                for (unsigned i = 0; i < 3; i++) { uint32_t v = 0x200 + bank * 32 + i; memcpy(m->scratch + 32 + bank * 16 + i * 4, &v, 4); }
            }
            header_space = DSP_SPACE_X;
            track_chain = true; chain_walks = 0;
            reset_header_counts();
            dsp_dma_write(dma, DMA_NEXT_BLOCK, 0x100);
            dsp_dma_write(dma, DMA_CONTROL, DMA_CONTROL_ACTION_START);
            REQUIRE(header_batches == (reject ? 0 : 3) && chain_walks == 3);
            REQUIRE(header_reads == (reject ? 21 : 0));
            REQUIRE(!bad_header_order && dma->next_block == NODE_POINTER_EOL);
            track_header = track_chain = false;
            for (unsigned bank = 0; bank < 3; bank++) for (unsigned i = 0; i < 3; i++) REQUIRE(c->xram[0x40 + bank * 16 + i] == 0x200 + bank * 32 + i);
        }
        /* A callback-only backend can still process zero/discard transfers. */
        header_space = DSP_SPACE_X; header_addr = 0x100;
        uint32_t header[] = {NODE_POINTER_EOL,(2 << 10) | (15 << 5) | NODE_CONTROL_DIRECTION,3,0x4000,5,32,127};
        seed_header(c, header); memset(m->scratch + 37, 0x5a, 12);
        dma->mem_ptr = NULL;
        dma->mem_descriptor = NULL;
        reset_header_counts();
        dsp_dma_write(dma, DMA_NEXT_BLOCK, 0x100);
        dsp_dma_write(dma, DMA_CONTROL, DMA_CONTROL_ACTION_START);
        REQUIRE(header_batches == 0 && header_reads == 7 && !bad_header_order);
        track_header = false;
        for (unsigned i = 0; i < 12; i++) REQUIRE(m->scratch[37 + i] == 0);
        uint32_t (*read_x)(dsp_core_t *, uint32_t) = c->read_peripheral;
        uint32_t (*read_y)(dsp_core_t *, uint32_t) = c->read_y_peripheral;
        c->read_peripheral = c->read_y_peripheral = ordered_peripheral;
        uint32_t observed[7];
        for (unsigned bus = 0; bus < 2; bus++) {
            peripheral_sequence = 0;
            dsp56k_read_memory_words(c, bus ? DSP_SPACE_Y : DSP_SPACE_X, DSP_PERIPH_BASE, observed, 7);
            REQUIRE(peripheral_sequence == 7);
            for (unsigned i = 0; i < 7; i++) REQUIRE(observed[i] == ((i + 1) << 16 | ((DSP_PERIPH_BASE + i) & 0xffff)));
        }
        c->read_y_peripheral = NULL;
        peripheral_sequence = 0;
        dsp56k_read_memory_words(c, DSP_SPACE_Y, DSP_PERIPH_BASE, observed, 7);
        REQUIRE(peripheral_sequence == 0);
        for (unsigned i = 0; i < 7; i++) REQUIRE(observed[i] == 0xffffff);
        dsp56k_read_memory_words(c, DSP_SPACE_X, UINT32_MAX, NULL, 0);
        REQUIRE(peripheral_sequence == 0);
        c->read_peripheral = read_x; c->read_y_peripheral = read_y;
        m->dsp->ops->finalize(m->dsp); free(m->dsp); free(m);
    }
    *total += checks;
    return 0;
}
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
    REQUIRE(descriptor_checks(&checks) == 0);
    printf("DMA program spans: %u range/direction/invalidation checks pass\n", checks);
    return 0;
}
