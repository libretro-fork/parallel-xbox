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
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>

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

/*
 * The DSPs' thread (gp_ep.c): the GP and EP run each engine frame on a
 * thread of their own, alongside the engine's next frame, so the emulator
 * thread is not held for the DSP programs (a quarter of a frame's time in
 * Dead or Alive 3). The engine mixes a frame's voices into one of two
 * mixbins buffers (mcpx_apu_dsp_mixbins) and posts it (post), once the
 * frame before is done, and the thread puts the frame's output straight
 * into the frontend's queue (monitor.c); anything else that touches the
 * DSPs or the registers their frame uses (MMIO, reset, a state, the
 * frontend's read of the queue) waits for the posted frame to be done
 * first (quiesce). Lock-free: a generation each way, an eventcount each
 * way.
 */
typedef struct MCPXAPUDSPThread {
    struct sthread *thread;
    retro_atomic_int_t posted;      /* frames posted, moved on by the engine */
    retro_atomic_int_t done;        /* frames done, moved on by the thread */
    retro_atomic_int_t should_exit;
    retro_eventcount_t wake;        /* the thread, for a post */
    retro_eventcount_t finished;    /* the engine, for a done */
    int seen;                       /* the thread's own count */
    /* The frames' mixbins, the VP's output: the one the thread may be
     * on and the one the engine builds (mcpx_apu_dsp_mixbins) */
    float mixbins[2][NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    int frame_div[2];               /* the frames' engine count (EP 1 in 8) */
    /* The EP's last 256 samples: 32 of them go to the frontend each frame,
     * so the output keeps the engine's cadence whatever the DSPs do */
    int16_t ep_out[256][2];
} MCPXAPUDSPThread;

void mcpx_apu_dsp_init(MCPXAPUState *d);
void mcpx_apu_update_dsp_preference(MCPXAPUState *d);
/* The engine's frame: the mixbins buffer the VP fills for it (free: the
 * thread is on the other one at most), then that handed to the thread.
 * The thread puts the DSPs' output straight into the frontend's queue
 * (monitor.c). */
float (*mcpx_apu_dsp_mixbins(MCPXAPUState *d))[NUM_SAMPLES_PER_FRAME];
void mcpx_apu_dsp_post(MCPXAPUState *d, float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
/* The posted frame done, before anything else touches the DSPs */
void mcpx_apu_dsp_quiesce(MCPXAPUState *d);
/* Whether a GP/EP register the DSPs' frame uses is at this APU offset */
bool mcpx_apu_dsp_uses_reg(hwaddr addr);

#endif
