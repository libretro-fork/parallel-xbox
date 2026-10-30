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
 * thread of their own, behind the engine, so the emulator thread is not
 * held for the DSP programs (a quarter of a frame's time in Dead or Alive
 * 3) and does not wait on them either. The engine mixes a frame's voices
 * into a slot of a ring of mixbins buffers (mcpx_apu_dsp_mixbins) and
 * posts it; the thread runs the frames in order and puts their output
 * straight into the frontend's queue (monitor.c). The engine waits only
 * when the thread is a whole ring behind (MCPX_APU_MONITOR_FRAMES_IN_FLIGHT,
 * which the queue's preroll covers, monitor.h).
 *
 * The guest's writes to the DSPs (their memories, GPRST/EPRST) and to the
 * APU registers their frames use (mcpx_apu_dsp_uses_reg) go into a write
 * queue, tagged with the frames posted so far, and the thread applies them
 * in order before the next frame, where they would have landed had the
 * DSPs run in step. With the thread idle they are applied at once. Only a
 * guest read of the DSPs' memories or of those registers, a reset, a
 * state and the option's change wait for the thread (quiesce), and a write
 * queue full. Lock-free: a generation each way, an eventcount each way;
 * the thread is woken for half a ring of frames at a time, at the video
 * frame's end, or before the engine waits for it, not for every frame.
 */
#define MCPX_APU_DSP_RING MCPX_APU_MONITOR_FRAMES_IN_FLIGHT
#define MCPX_APU_DSP_WRITES 4096 /* a power of two */

enum {
    MCPX_APU_DSP_WRITE_GP,          /* gp_ops offset */
    MCPX_APU_DSP_WRITE_EP,          /* ep_ops offset */
    MCPX_APU_DSP_WRITE_APU,         /* APU register (mcpx_apu_dsp_uses_reg) */
};

typedef struct MCPXAPUDSPWrite {
    int tag;                        /* frames posted when it was written */
    uint32_t kind;
    uint32_t addr;
    uint32_t val;
} MCPXAPUDSPWrite;

typedef struct MCPXAPUDSPThread {
    struct sthread *thread;
    retro_atomic_int_t posted;      /* frames posted, moved on by the engine */
    retro_atomic_int_t done;        /* frames done, moved on by the thread */
    retro_atomic_int_t should_exit;
    retro_eventcount_t wake;        /* the thread, for a post */
    retro_eventcount_t finished;    /* the engine, for a done */
    int seen;                       /* the thread's own count */
    int notified;                   /* posted when the thread was last woken */
    /* The frames' mixbins, the VP's output: frame n in slot n % RING */
    int64_t mixbins[MCPX_APU_DSP_RING][NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    int frame_div[MCPX_APU_DSP_RING]; /* the frames' engine count (EP 1 in 8) */
    bool silent[MCPX_APU_DSP_RING]; /* the engine stopped: silence */
    /* The write queue: the engine adds at head, the side that owns the
     * DSPs (the thread, or the engine with the thread idle) takes from
     * tail */
    retro_atomic_int_t write_head;
    retro_atomic_int_t write_tail;
    MCPXAPUDSPWrite writes[MCPX_APU_DSP_WRITES];
    /* GPRST and EPRST as the DSPs have them, in the writes' order (the
     * guest's view, gp.regs/ep.regs, is the engine's) */
    uint32_t gp_rst, ep_rst;
    /* Where the EP's 256 samples (8 engine frames) go when it runs: the
     * frontend's queue, straight (dsp_thread_frame), or ep_spare when
     * the queue has no room; and whether its program wrote them */
    int16_t (*ep_dst)[2];
    bool ep_written;
    int16_t ep_spare[256][2];
} MCPXAPUDSPThread;

void mcpx_apu_dsp_init(MCPXAPUState *d);
void mcpx_apu_dsp_finalize(MCPXAPUState *d);
void mcpx_apu_update_dsp_preference(MCPXAPUState *d);
/* The engine's frame: the mixbins buffer the VP fills for it (its slot,
 * free once the thread is done with the frame a ring before), then that
 * handed to the thread, or a frame of silence with the engine stopped */
int64_t (*mcpx_apu_dsp_mixbins(MCPXAPUState *d))[NUM_SAMPLES_PER_FRAME];
void mcpx_apu_dsp_post(MCPXAPUState *d, int64_t mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
void mcpx_apu_dsp_post_silence(MCPXAPUState *d);
/* The thread woken for the frames posted and not yet handed to it: at the
 * end of each video frame (libretro_audio_frame_end) */
void mcpx_apu_dsp_kick(MCPXAPUState *d);
/* The posted frames done and the queued writes applied, before anything
 * else reads or changes the DSPs */
void mcpx_apu_dsp_quiesce(MCPXAPUState *d);
/* A write to the DSPs or to a register their frames use (kind
 * MCPX_APU_DSP_WRITE_*), in order with the frames posted */
void mcpx_apu_dsp_write(MCPXAPUState *d, uint32_t kind, hwaddr addr, uint32_t val);
/* The DSPs' copies of GPRST/EPRST from the registers, both sides stopped
 * (a reset, a state loaded) */
void mcpx_apu_dsp_sync_regs(MCPXAPUState *d);
/* Whether a register the DSPs' frames use is at this APU offset */
bool mcpx_apu_dsp_uses_reg(hwaddr addr);

#endif
