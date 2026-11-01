/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
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
    struct {int64_t mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];uint32_t ep_rst;} dsp_state;
    struct {int point;} monitor;
    bool dsp_vp_pending;const uint64_t *dsp_vp_pages,*dsp_vp_readonly;
    bool write;
    unsigned int max_sge;
    hwaddr table;
    uint32_t guest_dma_addr;
    uint8_t result[64];
    unsigned int transfers, dirty;
} MCPXAPUState;

static uint8_t ram[1024 * 1024];
static int address_space_memory;
#define NV_PAPU_GPRST_GPRST 1
#define NV_PAPU_GPRST_GPDSPRST 2
#define MCPX_APU_DEBUG_MON_GP_OR_EP 1
#define LR_DSP_FENCE_TICK 5
static bool ownership_enabled,ownership_is_worker,ownership_gp_dma_guard;
static int ownership_ready,ownership_posted,ownership_done,ownership_work,ownership_div,bus_lease;
static unsigned ownership_cpu_depth;
static uint64_t ownership_pages[512],ownership_tables[512],own_live[512];
static int retro_atomic_load_acquire_int(int*p){return *p;}
static int retro_atomic_load_relaxed_int(int*p){return *p;}
static void retro_atomic_store_release_int(int*p,int v){*p=v;}
static void retro_eventcount_notify(int*p){(void)p;}
static void lr_dsp_ownership_quiesce(void){}
static void lr_dsp_ownership_fence(unsigned reason){assert(reason==5);}
static void lr_dsp_ownership_ram(uint64_t addr,uint64_t len){(void)addr;(void)len;}
static void lr_dsp_ownership_read(uint64_t addr,uint64_t len){(void)addr;(void)len;}
static uint64_t memory_region_get_ram_addr(void*p){(void)p;return 0;}
static uint32_t ldl_le_p(const void*p){uint32_t v;memcpy(&v,p,4);return v;}
static bool bus_lease_try_dsp(int*p){(void)p;return false;}
static void bus_lease_dsp_done(int*p){(void)p;}
static bool ownership_reserve_tables(MCPXAPUState*d){(void)d;return false;}
static bool ownership_candidate(MCPXAPUState*d){(void)d;return false;}
static bool gpu_try_dsp_pages(void){return false;}
static void gpu_release_dsp_pages(void){}
static void own_protect(MCPXAPUState*d){(void)d;}
static void dsp_run_ep(MCPXAPUState*d,int div){assert(div==d->ep_frame_div);}

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
static void mcpx_apu_update_dsp_preference(MCPXAPUState *d) { (void)d; }
/* PRODUCTION MIXBINS */
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
static void dsp_run_gp(MCPXAPUState *d,
                            int64_t bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    assert(bins == d->dsp_state.mixbins && bins[0][0] == 42);
    scatter_gather_walk(d, d->table, d->max_sge, d->guest_dma_addr,
                        sizeof(d->result), d->write, transfer, d);
}
/* PRODUCTION POST */
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
        d.max_sge = 0x6c;
        assert(d.transfers == 1 && d.ep_frame_div == 1);
        assert(d.dirty == (unsigned int)write);
        for (size_t i = 0; i < sizeof(d.result); i++) {
            assert(d.result[i] == (write ? 0xa5 : 0x31));
            assert(ram[0x10520 + i] == (write ? 0xa5 : 0x31));
        }
        se_frame(&d);
        assert(d.transfers == 2 && d.ep_frame_div == 2);
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
    for (size_t i = 0; i < sizeof(d.result); i++) {
        assert(d.result[i] == 0x31);
    }
    for(unsigned i=0;i<20000;i++) {
        ram[0x10520]=(uint8_t)i;
        unsigned before=d.transfers;
        se_frame(&d);
        assert(d.transfers==before+1 && d.result[0]==(uint8_t)i);
        ram[0x10520]^=0xff;
        assert(d.result[0]==(uint8_t)i);
    }
    puts("DSP guest RAM: scratch growth, reads, writes, page reuse and 20000 synchronous zero-copy mix-buffer ticks passed");
    return 0;
}
