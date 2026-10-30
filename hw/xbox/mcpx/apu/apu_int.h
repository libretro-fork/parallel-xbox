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
#ifndef HW_XBOX_MCPX_APU_INT_H
#define HW_XBOX_MCPX_APU_INT_H

#include "qemu/osdep.h"
#include <math.h>
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "cpu.h"
#include "exec/target_page.h"
#include "migration/vmstate.h"
#include "qemu/main-loop.h"
#include "qemu/thread.h"
#include <retro_atomic.h>
#include "system/runstate.h"
#include "ui/xemu-settings.h"

#include "trace.h"
#include "apu.h"
#include "apu_regs.h"
#include "apu_debug.h"
#include "fpconv.h"
#include "vp/vp.h"
#include "dsp/gp_ep.h"

#define GET_MASK(v, mask) (((v) & (mask)) >> ctz32(mask))

#define SET_MASK(v, mask, val)                                       \
    do {                                                             \
        (v) &= ~(mask);                                              \
        (v) |= ((val) << ctz32(mask)) & (mask);                      \
    } while (0)

#define CASE_4(v, step)                                              \
    case (v):                                                        \
    case (v)+(step):                                                 \
    case (v)+(step)*2:                                               \
    case (v)+(step)*3

// #define DEBUG_MCPX

#ifdef DEBUG_MCPX
#define DPRINTF(fmt, ...) \
    do { fprintf(stderr, fmt, ## __VA_ARGS__); } while (0)
#else
#define DPRINTF(fmt, ...) \
    do { } while (0)
#endif

#define MCPX_APU_DEVICE(obj) \
    OBJECT_CHECK(MCPXAPUState, (obj), "mcpx-apu")

/* The most queued frames a state carries: the queue holds about its preroll,
 * part of a video frame and the EP's eight engine frames between frames */
#define MCPX_APU_MONITOR_STATE_FRAMES 2048

typedef struct MCPXAPUState {
    /*< private >*/
    PCIDevice parent_obj;
    /*< public >*/

    bool set_irq;

    /* The engine's frames (apu.c): the next one's QEMU_CLOCK_VIRTUAL time,
     * on an exact grid of 2000000/3 ns */
    QEMUTimer *tick_timer;
    int64_t next_tick_ns;
    int tick_rem;

    MemoryRegion *ram;
    uint8_t *ram_ptr;
    MemoryRegion mmio;

    MCPXAPUVPState vp;
    MCPXAPUGPState gp;
    MCPXAPUEPState ep;
    MCPXAPUDSPThread dsp_thread;

    /* the guest's from the vCPU, the DSP thread's and the voice workers':
     * word-atomic, apu_reg_r/w */
    retro_atomic_int_t regs[0x20000];

    int ep_frame_div;
    struct {
        McpxApuDebugMonitorPoint point;
    } monitor;

    /* What a state carries of the output to the frontend (apu.c, the
     * "mcpx-apu/output" subsection): the samples queued and not yet taken,
     * the engine frames of them queued ahead, the engine's next frame and
     * EP cycle, and the voices' resampler and filter state and HRTF table.
     * With them a load carries on exactly as the save would have, so
     * Run-Ahead's load every frame leaves the sound whole. */
    struct {
        bool loaded;                /* the subsection was in the state */
        uint32_t frames;
        uint32_t ahead;
        int16_t samples[MCPX_APU_MONITOR_STATE_FRAMES * 2];
    } saved_output;
} MCPXAPUState;

/* An APU register: relaxed, a plain load or store */
static inline uint32_t apu_reg_r(MCPXAPUState *d, unsigned int r)
{
    return (uint32_t)retro_atomic_load_relaxed_int(&d->regs[r]);
}

static inline void apu_reg_w(MCPXAPUState *d, unsigned int r, uint32_t v)
{
    retro_atomic_store_relaxed_int(&d->regs[r], (int)v);
}

#define APU_REG_SET_MASK(d, r, mask, v)    \
    do {                                    \
        uint32_t apu_v_ = apu_reg_r((d), (r)); \
        SET_MASK(apu_v_, (mask), (v));      \
        apu_reg_w((d), (r), apu_v_);        \
    } while (0)

extern MCPXAPUState *g_state; // Used via debug handlers
extern struct McpxApuDebug g_dbg, g_dbg_cache;
extern int g_dbg_voice_monitor;
extern uint64_t g_dbg_muted_voices[4];

void mcpx_debug_begin_frame(void);
void mcpx_debug_end_frame(void);

void mcpx_apu_monitor_init(MCPXAPUState *d, Error **errp);
/* The queue for a state, both sides stopped: the oldest frames up to max
 * into samples, how many, and the frames queued ahead; and back */
size_t mcpx_apu_monitor_save(int16_t *samples, size_t max, uint32_t *ahead);
void mcpx_apu_monitor_restore(const int16_t *samples, size_t frames,
                              uint32_t ahead);
void mcpx_apu_monitor_finalize(MCPXAPUState *d);
/* Producer: a frame's room in the queue to write straight into (NULL:
 * no room, the frame is dropped; or the frame was queued ahead), the gain
 * to apply (16.16), and the frames written */
int16_t *mcpx_apu_monitor_begin(size_t frames);
/* Several engine frames' room at once (NULL: no room), committed with the
 * gain applied in place; the frames after this one then skip theirs */
int16_t *mcpx_apu_monitor_begin_ahead(size_t frames);
void mcpx_apu_monitor_end_ahead(int16_t *out, size_t frames);
int32_t mcpx_apu_monitor_gain_q16(void);
void mcpx_apu_monitor_end(size_t frames);
void mcpx_apu_monitor_push_f32(const float (*samples)[2], size_t frames);
void mcpx_apu_monitor_push_silence(void);
void mcpx_apu_monitor_silence(MCPXAPUState *d);

#endif
