/*
 * QEMU MCPX Audio Processing Unit implementation
 *
 * Copyright (c) 2012 espes
 * Copyright (c) 2018-2019 Jannik Vogel
 * Copyright (c) 2019-2025 Matt Borgerson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/xbox/mcpx/apu/apu_int.h"
#include "hw/xbox/mcpx/apu/monitor.h"
#include <rthreads/rthreads.h>

/* The option values the DSPs were last set up for: none on a new device
 * (mcpx_apu_dsp_init), so new content gets its monitor point and engine
 * whatever the last content ran with */
static int last_known_dsp_pref = -1;
static int last_known_jit_pref = -1;

void mcpx_apu_update_dsp_preference(MCPXAPUState *d)
{
    if (last_known_dsp_pref == (int)g_config.audio.use_dsp &&
        last_known_jit_pref == (int)g_config.audio.use_dsp_jit) {
        return;
    }
    /* a change: the thread done with its frame first, since the monitor
     * point and the engine are its */
    mcpx_apu_dsp_quiesce(d);

    if (last_known_dsp_pref != (int)g_config.audio.use_dsp) {
        if (g_config.audio.use_dsp) {
            d->monitor.point = MCPX_APU_DEBUG_MON_GP_OR_EP;
            d->gp.realtime = true;
            d->ep.realtime = true;
        } else {
            d->monitor.point = MCPX_APU_DEBUG_MON_VP;
            d->gp.realtime = false;
            d->ep.realtime = false;
        }
        last_known_dsp_pref = g_config.audio.use_dsp;
    }

    if (last_known_jit_pref != (int)g_config.audio.use_dsp_jit) {
        dsp_set_engine(d->gp.dsp, g_config.audio.use_dsp_jit);
        dsp_set_engine(d->ep.dsp, g_config.audio.use_dsp_jit);
        last_known_jit_pref = g_config.audio.use_dsp_jit;
    }
}

static void scatter_gather_walk(MCPXAPUState *d, hwaddr sge_base,
                                unsigned int max_sge, uint32_t addr,
                                size_t len, bool dir,
                                dsp_dma_run_func fn, void *user)
{
    unsigned int page_entry = addr / TARGET_PAGE_SIZE;
    unsigned int offset_in_page = addr % TARGET_PAGE_SIZE;
    unsigned int bytes_to_copy = TARGET_PAGE_SIZE - offset_in_page;

    while (len > 0) {
        assert(page_entry <= max_sge);

        uint32_t prd_address = ldl_le_phys(&address_space_memory,
                                           sge_base + page_entry * 8 + 0);
        // uint32_t prd_control = ldl_le_phys(&address_space_memory,
        //                                     sge_base + page_entry * 8 + 4);

        hwaddr paddr = prd_address + offset_in_page;

        if (bytes_to_copy > len) {
            bytes_to_copy = len;
        }

        assert(paddr + bytes_to_copy < memory_region_size(d->ram));

        /* the page's run of the guest's RAM, in place */
        fn(user, &d->ram_ptr[paddr], bytes_to_copy);
        if (dir) {
            memory_region_set_dirty(d->ram, paddr, bytes_to_copy);
        }

        len -= bytes_to_copy;

        /* After the first iteration, we are page aligned */
        page_entry += 1;
        bytes_to_copy = TARGET_PAGE_SIZE;
        offset_in_page = 0;
    }
}

static void gp_scratch_walk(void *opaque, uint32_t addr, size_t len, bool dir,
                            dsp_dma_run_func fn, void *user)
{
    MCPXAPUState *d = opaque;
    // fprintf(stderr, "GP %s scratch 0x%x bytes (0x%x words) at %x (0x%x words)\n", dir ? "writing to" : "reading from", len, len/4, addr, addr/4);
    scatter_gather_walk(d, apu_reg_r(d, NV_PAPU_GPSADDR), apu_reg_r(d, NV_PAPU_GPSMAXSGE),
                        addr, len, dir, fn, user);
}

static void ep_scratch_walk(void *opaque, uint32_t addr, size_t len, bool dir,
                            dsp_dma_run_func fn, void *user)
{
    MCPXAPUState *d = opaque;
    // fprintf(stderr, "EP %s scratch 0x%x bytes (0x%x words) at %x (0x%x words)\n", dir ? "writing to" : "reading from", len, len/4, addr, addr/4);
    scatter_gather_walk(d, apu_reg_r(d, NV_PAPU_EPSADDR), apu_reg_r(d, NV_PAPU_EPSMAXSGE),
                        addr, len, dir, fn, user);
}

static uint32_t circular_scatter_gather_walk(MCPXAPUState *d, hwaddr sge_base,
                                             unsigned int max_sge,
                                             uint32_t base, uint32_t end,
                                             uint32_t cur, size_t len, bool dir,
                                             dsp_dma_run_func fn, void *user)
{
    while (len > 0) {
        unsigned int bytes_to_copy = end - cur;

        if (bytes_to_copy > len) {
            bytes_to_copy = len;
        }

        DPRINTF("circular scatter gather %s in range 0x%x - 0x%x at 0x%x of "
                "length 0x%x / 0x%lx bytes\n",
                dir ? "write" : "read", base, end, cur, bytes_to_copy, len);

        assert((cur >= base) && ((cur + bytes_to_copy) <= end));
        scatter_gather_walk(d, sge_base, max_sge, cur, bytes_to_copy, dir, fn, user);

        len -= bytes_to_copy;

        /* After the first iteration we might have to wrap */
        cur += bytes_to_copy;
        if (cur >= end) {
            assert(cur == end);
            cur = base;
        }
    }

    return cur;
}

static void gp_fifo_walk(void *opaque, unsigned int index, size_t len, bool dir,
                         dsp_dma_run_func fn, void *user)
{
    MCPXAPUState *d = opaque;
    uint32_t base;
    uint32_t end;
    hwaddr cur_reg;
    if (dir) {
        assert(index < GP_OUTPUT_FIFO_COUNT);
        base = GET_MASK(apu_reg_r(d, NV_PAPU_GPOFBASE0 + 0x10 * index),
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(apu_reg_r(d, NV_PAPU_GPOFEND0 + 0x10 * index),
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_GPOFCUR0 + 0x10 * index;
    } else {
        assert(index < GP_INPUT_FIFO_COUNT);
        base = GET_MASK(apu_reg_r(d, NV_PAPU_GPIFBASE0 + 0x10 * index),
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(apu_reg_r(d, NV_PAPU_GPIFEND0 + 0x10 * index),
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_GPIFCUR0 + 0x10 * index;
    }

    uint32_t cur = GET_MASK(apu_reg_r(d, cur_reg), NV_PAPU_GPOFCUR0_VALUE);

    // fprintf(stderr, "GP %s fifo #%d, base = %x, end = %x, cur = %x, len = %x\n",
    //     dir ? "writing to" : "reading from", index,
    //     base, end, cur, len);

    /* DSP hangs if current >= end; but forces current >= base */
    assert(cur < end);
    if (cur < base) {
        cur = base;
    }

    cur = circular_scatter_gather_walk(d,
        apu_reg_r(d, NV_PAPU_GPFADDR), apu_reg_r(d, NV_PAPU_GPFMAXSGE),
        base, end, cur, len, dir, fn, user);

    APU_REG_SET_MASK(d, cur_reg, NV_PAPU_GPOFCUR0_VALUE, cur);
}

/* the EP's output FIFO 0 sinks to the frontend: a run of the guest's
 * FIFO gets silence instead (ep_fifo_walk) */
static void ep_silence_run(void *user, uint8_t *host, size_t bytes)
{
    memset(host, 0, bytes);
}

static void ep_fifo_walk(void *opaque, unsigned int index, size_t len, bool dir,
                         dsp_dma_run_func fn, void *user)
{
    MCPXAPUState *d = opaque;
    uint32_t base;
    uint32_t end;
    hwaddr cur_reg;
    if (dir) {
        assert(index < EP_OUTPUT_FIFO_COUNT);
        base = GET_MASK(apu_reg_r(d, NV_PAPU_EPOFBASE0 + 0x10 * index),
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(apu_reg_r(d, NV_PAPU_EPOFEND0 + 0x10 * index),
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_EPOFCUR0 + 0x10 * index;
    } else {
        assert(index < EP_INPUT_FIFO_COUNT);
        base = GET_MASK(apu_reg_r(d, NV_PAPU_EPIFBASE0 + 0x10 * index),
                        NV_PAPU_GPOFBASE0_VALUE);
        end = GET_MASK(apu_reg_r(d, NV_PAPU_EPIFEND0 + 0x10 * index),
                       NV_PAPU_GPOFEND0_VALUE);
        cur_reg = NV_PAPU_EPIFCUR0 + 0x10 * index;
    }

    uint32_t cur = GET_MASK(apu_reg_r(d, cur_reg), NV_PAPU_GPOFCUR0_VALUE);

    // fprintf(stderr, "EP %s fifo #%d, base = %x, end = %x, cur = %x, len = %x\n",
    //     dir ? "writing to" : "reading from", index,
    //     base, end, cur, len);

    if (dir && index == 0 && d->monitor.point != MCPX_APU_DEBUG_MON_AC97) {
        /* sunk: the EP's 256 samples, 8 engine frames' worth, written
         * straight into the frontend's queue (dsp_thread_frame), and the
         * guest's FIFO gets silence */
        if ((d->monitor.point == MCPX_APU_DEBUG_MON_EP) ||
            (d->monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP)) {
            assert(len == sizeof(d->dsp_thread.ep_spare));
            fn(user, (uint8_t *)d->dsp_thread.ep_dst, len);
            d->dsp_thread.ep_written = true;
        }
        fn = ep_silence_run;
    }

    /* DSP hangs if current >= end; but forces current >= base */
    if (cur >= end) {
        cur = cur % (end - base);
    }
    if (cur < base) {
        cur = base;
    }

    cur = circular_scatter_gather_walk(d,
        apu_reg_r(d, NV_PAPU_EPFADDR), apu_reg_r(d, NV_PAPU_EPFMAXSGE),
        base, end, cur, len, dir, fn, user);

    APU_REG_SET_MASK(d, cur_reg, NV_PAPU_GPOFCUR0_VALUE, cur);
}

static void proc_rst_write(DSPState *dsp, uint32_t oldval, uint32_t val)
{
    if (!(val & NV_PAPU_GPRST_GPRST) || !(val & NV_PAPU_GPRST_GPDSPRST)) {
        dsp_reset(dsp);
    } else if (
        (!(oldval & NV_PAPU_GPRST_GPRST) || !(oldval & NV_PAPU_GPRST_GPDSPRST))
        && ((val & NV_PAPU_GPRST_GPRST) && (val & NV_PAPU_GPRST_GPDSPRST))) {
        dsp_bootstrap(dsp);
    }
}

/* Global Processor - programmable DSP */
static uint64_t gp_read(void *opaque, hwaddr addr, unsigned int size)
{
    MCPXAPUState *d = opaque;

    mcpx_apu_dsp_quiesce(d);

    assert(size == 4);
    assert(addr % 4 == 0);

    uint64_t r = 0;
    switch (addr) {
    case NV_PAPU_GPXMEM ... NV_PAPU_GPXMEM + 0x1000 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPXMEM) / 4;
        r = dsp_read_memory(d->gp.dsp, 'X', xaddr);
        // fprintf(stderr, "read GP NV_PAPU_GPXMEM [%x] -> %x\n", xaddr, r);
        break;
    }
    case NV_PAPU_GPMIXBUF ... NV_PAPU_GPMIXBUF + 0x400 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPMIXBUF) / 4;
        r = dsp_read_memory(d->gp.dsp, 'X', GP_DSP_MIXBUF_BASE + xaddr);
        // fprintf(stderr, "read GP NV_PAPU_GPMIXBUF [%x] -> %x\n", xaddr, r);
        break;
    }
    case NV_PAPU_GPYMEM ... NV_PAPU_GPYMEM + 0x800 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_GPYMEM) / 4;
        r = dsp_read_memory(d->gp.dsp, 'Y', yaddr);
        // fprintf(stderr, "read GP NV_PAPU_GPYMEM [%x] -> %x\n", yaddr, r);
        break;
    }
    case NV_PAPU_GPPMEM ... NV_PAPU_GPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_GPPMEM) / 4;
        r = dsp_read_memory(d->gp.dsp, 'P', paddr);
        // fprintf(stderr, "read GP NV_PAPU_GPPMEM [%x] -> %x\n", paddr, r);
        break;
    }
    default:
        r = d->gp.regs[addr];
        break;
    }
    DPRINTF("mcpx apu GP: read [0x%" HWADDR_PRIx "] -> 0x%lx\n", addr, r);

    return r;
}

static void gp_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    MCPXAPUState *d = opaque;

    mcpx_apu_dsp_quiesce(d);

    assert(size == 4);
    assert(addr % 4 == 0);

    DPRINTF("mcpx apu GP: [0x%" HWADDR_PRIx "] = 0x%lx\n", addr, val);

    switch (addr) {
    case NV_PAPU_GPXMEM ... NV_PAPU_GPXMEM + 0x1000 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPXMEM) / 4;
        // fprintf(stderr, "gp write xmem %x = %x\n", xaddr, val);
        dsp_write_memory(d->gp.dsp, 'X', xaddr, val);
        break;
    }
    case NV_PAPU_GPMIXBUF ... NV_PAPU_GPMIXBUF + 0x400 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_GPMIXBUF) / 4;
        // fprintf(stderr, "gp write xmixbuf %x = %x\n", xaddr, val);
        dsp_write_memory(d->gp.dsp, 'X', GP_DSP_MIXBUF_BASE + xaddr, val);
        break;
    }
    case NV_PAPU_GPYMEM ... NV_PAPU_GPYMEM + 0x800 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_GPYMEM) / 4;
        // fprintf(stderr, "gp write ymem %x = %x\n", yaddr, val);
        dsp_write_memory(d->gp.dsp, 'Y', yaddr, val);
        break;
    }
    case NV_PAPU_GPPMEM ... NV_PAPU_GPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_GPPMEM) / 4;
        // fprintf(stderr, "gp write pmem %x = %x\n", paddr, val);
        dsp_write_memory(d->gp.dsp, 'P', paddr, val);
        break;
    }
    case NV_PAPU_GPRST:
        proc_rst_write(d->gp.dsp, d->gp.regs[NV_PAPU_GPRST], val);
        d->gp.regs[NV_PAPU_GPRST] = val;
        break;
    default:
        d->gp.regs[addr] = val;
        break;
    }
}

const MemoryRegionOps gp_ops = {
    .read = gp_read,
    .write = gp_write,
};

/* Encode Processor - encoding DSP */
static uint64_t ep_read(void *opaque, hwaddr addr, unsigned int size)
{
    MCPXAPUState *d = opaque;

    mcpx_apu_dsp_quiesce(d);

    assert(size == 4);
    assert(addr % 4 == 0);

    uint64_t r = 0;
    switch (addr) {
    case NV_PAPU_EPXMEM ... NV_PAPU_EPXMEM + 0xC00 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_EPXMEM) / 4;
        r = dsp_read_memory(d->ep.dsp, 'X', xaddr);
        // fprintf(stderr, "read EP  NV_PAPU_EPXMEM [%x] -> %x\n", xaddr, r);
        break;
    }
    case NV_PAPU_EPYMEM ... NV_PAPU_EPYMEM + 0x100 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_EPYMEM) / 4;
        r = dsp_read_memory(d->ep.dsp, 'Y', yaddr);
        // fprintf(stderr, "read EP  NV_PAPU_EPYMEM [%x] -> %x\n", yaddr, r);
        break;
    }
    case NV_PAPU_EPPMEM ... NV_PAPU_EPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_EPPMEM) / 4;
        r = dsp_read_memory(d->ep.dsp, 'P', paddr);
        // fprintf(stderr, "read EP  NV_PAPU_EPPMEM [%x] -> %x\n", paddr, r);
        break;
    }
    default:
        r = d->ep.regs[addr];
        break;
    }
    DPRINTF("mcpx apu EP: read [0x%" HWADDR_PRIx "] -> 0x%lx\n", addr, r);

    return r;
}

static void ep_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size)
{
    MCPXAPUState *d = opaque;

    mcpx_apu_dsp_quiesce(d);

    assert(size == 4);
    assert(addr % 4 == 0);

    DPRINTF("mcpx apu EP: [0x%" HWADDR_PRIx "] = 0x%lx\n", addr, val);

    switch (addr) {
    case NV_PAPU_EPXMEM ... NV_PAPU_EPXMEM + 0xC00 * 4 - 1: {
        uint32_t xaddr = (addr - NV_PAPU_EPXMEM) / 4;
        dsp_write_memory(d->ep.dsp, 'X', xaddr, val);
        // fprintf(stderr, "ep write xmem %x = %x\n", xaddr, val);
        break;
    }
    case NV_PAPU_EPYMEM ... NV_PAPU_EPYMEM + 0x100 * 4 - 1: {
        uint32_t yaddr = (addr - NV_PAPU_EPYMEM) / 4;
        dsp_write_memory(d->ep.dsp, 'Y', yaddr, val);
        // fprintf(stderr, "ep write ymem %x = %x\n", yaddr, val);
        break;
    }
    case NV_PAPU_EPPMEM ... NV_PAPU_EPPMEM + 0x1000 * 4 - 1: {
        uint32_t paddr = (addr - NV_PAPU_EPPMEM) / 4;
        // fprintf(stderr, "ep write pmem %x = %x\n", paddr, val);
        dsp_write_memory(d->ep.dsp, 'P', paddr, val);
        break;
    }
    case NV_PAPU_EPRST:
        proc_rst_write(d->ep.dsp, d->ep.regs[NV_PAPU_EPRST], val);
        d->ep.regs[NV_PAPU_EPRST] = val;
        d->ep_frame_div = 0; /* FIXME: Still unsure about frame sync */
        break;
    default:
        d->ep.regs[addr] = val;
        break;
    }
}

const MemoryRegionOps ep_ops = {
    .read = ep_read,
    .write = ep_write,
};

/* One engine frame of the DSPs, on their thread: the mixbins into the
 * GP's MIXBUF, the GP, every 8th frame the EP, and the frame's 32 samples
 * of output to the frontend's queue (the GP's with the EP off; the EP's
 * 256 go in at its run, for its eight frames): one frame's worth each
 * frame, so a video frame's samples are all there whatever the DSPs'
 * programs did */
static void dsp_thread_frame(MCPXAPUState *d, float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME],
                             int frame_div)
{
    /* Write VP results to the GP DSP MIXBUF, in place */
    uint32_t *mixbuf = dsp_memory_ptr(d->gp.dsp, 'X', GP_DSP_MIXBUF_BASE,
                                      NUM_MIXBINS * NUM_SAMPLES_PER_FRAME);
    QEMU_BUILD_BUG_ON((NUM_MIXBINS * NUM_SAMPLES_PER_FRAME) % 4);
    float_to_24b_n(mixbuf, &mixbins[0][0], NUM_MIXBINS * NUM_SAMPLES_PER_FRAME);

    bool ep_enabled = (d->ep.regs[NV_PAPU_EPRST] & NV_PAPU_GPRST_GPRST) &&
                      (d->ep.regs[NV_PAPU_EPRST] & NV_PAPU_GPRST_GPDSPRST);

    bool gp_enabled = (d->gp.regs[NV_PAPU_GPRST] & NV_PAPU_GPRST_GPRST) &&
                      (d->gp.regs[NV_PAPU_GPRST] & NV_PAPU_GPRST_GPDSPRST);
    bool gp_out = (d->monitor.point == MCPX_APU_DEBUG_MON_GP) ||
                  (d->monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP && !ep_enabled);

    /* Run GP */
    if (gp_enabled) {
        dsp_start_frame(d->gp.dsp);
        dsp_set_halt_requested(d->gp.dsp, false);
        dsp_set_cycle_count(d->gp.dsp, 0);
        do {
            dsp_run(d->gp.dsp, 1000);
        } while (!dsp_get_halt_requested(d->gp.dsp) && d->gp.realtime);
        g_dbg.gp.cycles = dsp_get_cycle_count(d->gp.dsp);

        if (gp_out) {
            /* mixbins 0 and 1, straight into the frontend's queue */
            int16_t *out = mcpx_apu_monitor_begin(NUM_SAMPLES_PER_FRAME);
            if (out) {
                const int32_t gain = mcpx_apu_monitor_gain_q16();
                const uint32_t *l = &mixbuf[0];
                const uint32_t *r = &mixbuf[NUM_SAMPLES_PER_FRAME];
                for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
                    out[2 * i] = mcpx_apu_monitor_scale_s16((int16_t)(l[i] >> 8), gain);
                    out[2 * i + 1] = mcpx_apu_monitor_scale_s16((int16_t)(r[i] >> 8), gain);
                }
                mcpx_apu_monitor_end(NUM_SAMPLES_PER_FRAME);
            }
        }
    } else if (gp_out) {
        mcpx_apu_monitor_push_silence();
    }

    bool ep_out = (d->monitor.point == MCPX_APU_DEBUG_MON_EP) ||
                  (d->monitor.point == MCPX_APU_DEBUG_MON_GP_OR_EP && ep_enabled);

    /* Run EP: its 256 samples are this frame's and the next seven's. Its
     * output DMA writes them straight into the frontend's queue, where
     * they are committed at once; the next seven frames then skip theirs
     * (monitor.c). Silence where its program made none. */
    if (ep_enabled && frame_div % 8 == 0) {
        MCPXAPUDSPThread *t = &d->dsp_thread;
        int16_t *q = ep_out ? mcpx_apu_monitor_begin_ahead(256) : NULL;

        t->ep_dst = q ? (int16_t (*)[2])q : t->ep_spare;
        t->ep_written = false;
        dsp_start_frame(d->ep.dsp);
        dsp_set_halt_requested(d->ep.dsp, false);
        dsp_set_cycle_count(d->ep.dsp, 0);
        do {
            dsp_run(d->ep.dsp, 1000);
        } while (!dsp_get_halt_requested(d->ep.dsp) && d->ep.realtime);
        g_dbg.ep.cycles = dsp_get_cycle_count(d->ep.dsp);
        if (q) {
            if (!t->ep_written) {
                memset(q, 0, 256 * 2 * sizeof(int16_t));
            }
            mcpx_apu_monitor_end_ahead(q, 256);
            return;
        }
    }

    if (ep_out) {
        /* none of the EP's output queued for this frame (the EP off or
         * started mid-cycle, a state loaded, the queue full): silence,
         * unless an earlier frame queued it ahead */
        mcpx_apu_monitor_push_silence();
    }
}

static void dsp_thread_main(void *arg)
{
    MCPXAPUState *d = arg;
    MCPXAPUDSPThread *t = &d->dsp_thread;

    rcu_register_thread();
    for (;;) {
        int n = retro_atomic_load_acquire_int(&t->posted);
        if (n == t->seen) {
            int key = retro_eventcount_prepare_wait(&t->wake);
            n = retro_atomic_load_acquire_int(&t->posted);
            if (n == t->seen) {
                retro_eventcount_commit_wait(&t->wake, key);
                continue;
            }
            retro_eventcount_cancel_wait(&t->wake);
        }
        if (retro_atomic_load_acquire_int(&t->should_exit)) {
            break;
        }
        t->seen = n;
        dsp_thread_frame(d, t->mixbins[n & 1], t->frame_div[n & 1]);
        retro_atomic_store_release_int(&t->done, n);
        retro_eventcount_notify(&t->finished);
    }
    rcu_unregister_thread();
}

void mcpx_apu_dsp_quiesce(MCPXAPUState *d)
{
    MCPXAPUDSPThread *t = &d->dsp_thread;
    int want;

    if (!t->thread) {
        return;
    }
    want = retro_atomic_load_acquire_int(&t->posted);
    while (retro_atomic_load_acquire_int(&t->done) != want) {
        int key = retro_eventcount_prepare_wait(&t->finished);
        if (retro_atomic_load_acquire_int(&t->done) == want) {
            retro_eventcount_cancel_wait(&t->finished);
            break;
        }
        retro_eventcount_commit_wait(&t->finished, key);
    }
}

float (*mcpx_apu_dsp_mixbins(MCPXAPUState *d))[NUM_SAMPLES_PER_FRAME]
{
    MCPXAPUDSPThread *t = &d->dsp_thread;

    /* frame n is posted as t->posted == n and runs on mixbins[n & 1]:
     * the next one's buffer is the other, free once frame n - 1 is done,
     * which the post of frame n waited for */
    return t->mixbins[(retro_atomic_load_acquire_int(&t->posted) + 1) & 1];
}

void mcpx_apu_dsp_post(MCPXAPUState *d, float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME])
{
    MCPXAPUDSPThread *t = &d->dsp_thread;
    int n;

    if (!g_config.audio.use_dsp) {
        /* no DSP work: the frame on this thread, as it was; the VP's
         * output is the frontend's (vp.c) */
        mcpx_apu_dsp_quiesce(d);
        dsp_thread_frame(d, mixbins, d->ep_frame_div);
        return;
    }
    if (!t->thread) {
        retro_eventcount_init(&t->wake);
        retro_eventcount_init(&t->finished);
        retro_atomic_int_init(&t->posted, 0);
        retro_atomic_int_init(&t->done, 0);
        retro_atomic_int_init(&t->should_exit, 0);
        t->thread = sthread_create(dsp_thread_main, d);
    }
    n = retro_atomic_load_acquire_int(&t->posted) + 1;
    assert(mixbins == t->mixbins[n & 1]);
    /* the frame before this one's buffer is done before the buffer is
     * handed out again (mcpx_apu_dsp_mixbins of the next frame) */
    while (retro_atomic_load_acquire_int(&t->done) != n - 1) {
        int key = retro_eventcount_prepare_wait(&t->finished);
        if (retro_atomic_load_acquire_int(&t->done) == n - 1) {
            retro_eventcount_cancel_wait(&t->finished);
            break;
        }
        retro_eventcount_commit_wait(&t->finished, key);
    }
    t->frame_div[n & 1] = d->ep_frame_div;
    retro_atomic_store_release_int(&t->posted, n);
    retro_eventcount_notify(&t->wake);
}

bool mcpx_apu_dsp_uses_reg(hwaddr addr)
{
    /* the scatter-gather tables and FIFOs (gp_scratch_walk, ep_scratch_walk,
     * gp_fifo_walk, ep_fifo_walk), and the EP's output FIFO cursors it moves */
    return (addr >= NV_PAPU_GPSADDR && addr <= NV_PAPU_EPFADDR + 3) ||
           (addr >= NV_PAPU_GPSMAXSGE && addr <= NV_PAPU_EPFMAXSGE + 3) ||
           (addr >= NV_PAPU_GPOFBASE0 && addr <= NV_PAPU_GPIFCUR1 + 3) ||
           (addr >= NV_PAPU_EPOFBASE0 && addr <= NV_PAPU_EPIFCUR0 + 3);
}

void mcpx_apu_dsp_init(MCPXAPUState *d)
{
    last_known_dsp_pref = -1;
    last_known_jit_pref = -1;
    d->gp.dsp = dsp_init(d, gp_scratch_walk, gp_fifo_walk, true);
    dsp_set_halt_requested(d->gp.dsp, false);
    dsp_set_cycle_count(d->gp.dsp, 0);

    d->ep.dsp = dsp_init(d, ep_scratch_walk, ep_fifo_walk, false);
    dsp_set_halt_requested(d->ep.dsp, false);
    dsp_set_cycle_count(d->ep.dsp, 0);

    /* Until DSP is more performant, a switch to decide whether or not we should
     * use the full audio pipeline or not.
     */
    mcpx_apu_update_dsp_preference(d);
}

/* The device going away (content closed): the thread stopped and joined,
 * the way the VP's workers are, then the DSPs freed */
void mcpx_apu_dsp_finalize(MCPXAPUState *d)
{
    MCPXAPUDSPThread *t = &d->dsp_thread;

    if (t->thread) {
        mcpx_apu_dsp_quiesce(d);
        retro_atomic_store_release_int(&t->should_exit, 1);
        /* a post it does not run: should_exit is checked first */
        retro_atomic_store_release_int(&t->posted,
                                       retro_atomic_load_acquire_int(&t->posted) + 1);
        retro_eventcount_notify(&t->wake);
        sthread_join(t->thread);
        retro_eventcount_free(&t->wake);
        retro_eventcount_free(&t->finished);
        t->thread = NULL;
        t->seen = 0;
    }
    dsp_destroy(d->gp.dsp);
    dsp_destroy(d->ep.dsp);
    d->gp.dsp = NULL;
    d->ep.dsp = NULL;
}
