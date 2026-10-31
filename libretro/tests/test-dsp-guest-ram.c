/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TARGET_PAGE_SIZE 4096
#define NUM_MIXBINS 2
#define NUM_SAMPLES_PER_FRAME 32
typedef uint64_t hwaddr;
typedef void (*dsp_dma_run_func)(void *, uint8_t *, size_t);
typedef struct MCPXAPUState {
    uint8_t *ram_ptr;
    void *ram;
    int ep_frame_div;
    int64_t mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    bool pending, write, register_pending;
    unsigned int max_sge, next_max_sge;
    hwaddr table;
    uint32_t guest_dma_addr;
    uint8_t result[64];
    unsigned int transfers, dirty;
} MCPXAPUState;

static uint8_t ram[1024 * 1024];
static int address_space_memory;
static uint32_t ldl_le_phys(void *space, hwaddr addr)
{
    uint32_t value;
    (void)space;
    assert(addr + sizeof(value) <= sizeof(ram));
    memcpy(&value, ram + addr, sizeof(value));
    return value;
}
static size_t memory_region_size(void *region)
{
    assert(region == ram);
    return sizeof(ram);
}
static MCPXAPUState *active;
static void memory_region_set_dirty(void *region, hwaddr addr, size_t bytes)
{
    assert(region == ram && addr + bytes <= sizeof(ram));
    active->dirty++;
}
/* PRODUCTION WALK */

static void transfer(void *user, uint8_t *host, size_t bytes)
{
    MCPXAPUState *d = user;
    assert(bytes == sizeof(d->result));
    if (d->write) {
        memcpy(host, d->result, bytes);
    } else {
        memcpy(d->result, host, bytes);
    }
    d->transfers++;
}
/* A deterministic scheduler holds the worker until quiesce. Guest RAM
 * remains live, just as it does for the real DSP DMA callbacks. Register
 * writes are applied after the previously posted frame. */
static void mcpx_apu_dsp_quiesce(MCPXAPUState *d)
{
    if (d->pending) {
        scatter_gather_walk(d, d->table, d->max_sge, d->guest_dma_addr,
                            sizeof(d->result), d->write, transfer, d);
        d->pending = false;
    }
    if (d->register_pending) {
        d->max_sge = d->next_max_sge;
        d->register_pending = false;
    }
}
static void mcpx_apu_update_dsp_preference(MCPXAPUState *d) { (void)d; }
static int64_t (*mcpx_apu_dsp_mixbins(MCPXAPUState *d))[NUM_SAMPLES_PER_FRAME]
{
    return d->mixbins;
}
static void mcpx_apu_vp_frame(MCPXAPUState *d,
                            int64_t bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    (void)d;
    for (unsigned int i = 0; i < NUM_MIXBINS; i++) {
        for (unsigned int j = 0; j < NUM_SAMPLES_PER_FRAME; j++) {
            assert(bins[i][j] == 0);
            bins[i][j] = 42;
        }
    }
}
static void mcpx_apu_dsp_post(MCPXAPUState *d,
                            int64_t bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    assert(!d->pending && bins == d->mixbins && bins[0][0] == 42);
    d->pending = true;
}
/* PRODUCTION FRAME */

static void init(MCPXAPUState *d)
{
    memset(d, 0, sizeof(*d));
    memset(ram, 0, sizeof(ram));
    active = d;
    d->ram_ptr = ram;
    d->ram = ram;
    d->table = 0x1000;
    d->max_sge = 0xc;
    d->guest_dma_addr = 0x520;
    for (unsigned int page = 0; page <= 0x6c; page++) {
        uint32_t physical = 0x10000 + page * TARGET_PAGE_SIZE;
        memcpy(ram + d->table + page * 8, &physical, sizeof(physical));
        memset(ram + physical, 0x31 + page, TARGET_PAGE_SIZE);
    }
}
int main(void)
{
    MCPXAPUState d;
    for (int write = 0; write < 2; write++) {
        init(&d);
        d.write = write;
        memset(d.result, 0xa5, sizeof(d.result));
        se_frame(&d);
        /* OutRun 2 grows scratch from max page 0xc to 0x6c, publishing
         * parameters for a 64-byte read at 0x12520 before the MMIO write.
         * With the old deferred tick, this drain asserts on page 0x12. */
        d.guest_dma_addr = 0x12520;
        d.next_max_sge = 0x6c;
        d.register_pending = true;
        mcpx_apu_dsp_quiesce(&d);
        assert(d.transfers == 1 && !d.pending && d.ep_frame_div == 1);
        assert(d.dirty == (unsigned int)write);
        for (size_t i = 0; i < sizeof(d.result); i++) {
            assert(d.result[i] == (write ? 0xa5 : 0x31));
            assert(ram[0x10520 + i] == (write ? 0xa5 : 0x31));
        }
        se_frame(&d);
        assert(d.transfers == 2 && !d.pending && d.ep_frame_div == 2);
        assert(d.dirty == (unsigned int)(2 * write));
        if (!write) {
            for (size_t i = 0; i < sizeof(d.result); i++) {
                assert(d.result[i] == 0x43);
            }
        }
    }
    init(&d);
    se_frame(&d);
    /* Reusing a scratch page after the tick must not change its output. */
    memset(ram + 0x10520, 0xfe, sizeof(d.result));
    mcpx_apu_dsp_quiesce(&d);
    for (size_t i = 0; i < sizeof(d.result); i++) {
        assert(d.result[i] == 0x31);
    }
    puts("DSP guest RAM: scratch growth, reads, writes and page reuse passed");
    return 0;
}
