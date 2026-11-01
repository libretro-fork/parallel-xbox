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
#ifndef HW_XBOX_MCPX_APU_GP_EP_H
#define HW_XBOX_MCPX_APU_GP_EP_H

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/xbox/mcpx/apu/apu_regs.h"

#include "dsp.h"
#include "hw/xbox/mcpx/apu/monitor.h"

typedef struct MCPXAPUState MCPXAPUState;

typedef struct MCPXAPUGPState {
    bool realtime;
    MemoryRegion mmio;
    DSPState *dsp;
    uint32_t regs[0x10000];
} MCPXAPUGPState;

typedef struct MCPXAPUEPState {
    bool realtime;
    MemoryRegion mmio;
    DSPState *dsp;
    uint32_t regs[0x10000];
} MCPXAPUEPState;

extern const MemoryRegionOps gp_ops;
extern const MemoryRegionOps ep_ops;

/* The engine thread owns the DSPs and their live guest-RAM DMA. Each tick
 * completes before guest execution resumes. One mix buffer is sufficient;
 * EP output still commits directly to the frontend's SPSC audio queue. */
enum {
    MCPX_APU_DSP_WRITE_GP,
    MCPX_APU_DSP_WRITE_EP,
    MCPX_APU_DSP_WRITE_APU,
};
typedef struct MCPXAPUDSPState {
    int64_t mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    uint32_t gp_rst, ep_rst;
    int16_t (*ep_dst)[2];
    bool ep_written;
    int16_t ep_spare[256][2];
} MCPXAPUDSPState;

void mcpx_apu_dsp_init(MCPXAPUState *d);
void mcpx_apu_dsp_finalize(MCPXAPUState *d);
void mcpx_apu_update_dsp_preference(MCPXAPUState *d);
/* The VP fills the engine-owned mix buffer, then the GP/EP finish the same
 * tick synchronously. No queue, worker wake or deferred guest-memory access. */
int64_t (*mcpx_apu_dsp_mixbins(MCPXAPUState *d))[NUM_SAMPLES_PER_FRAME];
void mcpx_apu_dsp_post(MCPXAPUState *d, int64_t mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
void mcpx_apu_dsp_post_silence(MCPXAPUState *d);
/* Register writes are ordered on that same owner thread. */
void mcpx_apu_dsp_write(MCPXAPUState *d, uint32_t kind, hwaddr addr, uint32_t val);
/* The DSP reset shadows, synchronized on reset or state load. */
void mcpx_apu_dsp_sync_regs(MCPXAPUState *d);
/* Whether a register the DSPs' frames use is at this APU offset */
bool mcpx_apu_dsp_uses_reg(hwaddr addr);

#endif
