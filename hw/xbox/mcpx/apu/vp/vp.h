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
#ifndef HW_XBOX_MCPX_VP_H
#define HW_XBOX_MCPX_VP_H


#include "qemu/osdep.h"
#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/xbox/mcpx/apu/apu_regs.h"
#include "svf.h"
#include "hrtf.h"

typedef struct MCPXAPUState MCPXAPUState;

typedef struct MCPXAPUVPSSLData {
    uint32_t base[MCPX_HW_SSLS_PER_VOICE];
    uint8_t count[MCPX_HW_SSLS_PER_VOICE];
    int ssl_index;
    int ssl_seg;
} MCPXAPUVPSSLData;

typedef struct MCPXAPUVoiceFilter {
    uint16_t voice;
    /* Decoded source frames are retained across calls. The eight-point
     * interpolation history is a ring; head is its oldest/next-write slot.
     * Phase and source advance use 16.16 fixed point. Samples, here and
     * in the filters, are in steps of a 24-bit sample (sample.h). */
    int32_t resample_buf[NUM_SAMPLES_PER_FRAME][2];
    int resample_len;
    int resample_pos;
    /* Decoded payload length excludes zero padding. Natural EOF leaves the
     * voice active until eight padded samples have replaced its history. */
    int resample_valid;
    unsigned int resample_zeros;
    bool source_ended;
    int32_t resample_hist[8][2];
    unsigned int resample_head;
    uint32_t resample_phase;
    sv_filter svf[2];
    sv_filter peq[2];
    int32_t i3dl2[2];
    uint16_t route_gain[8];
    bool gains;
    uint32_t pitch_step;
    int64_t pitch;          /* the last frame's, 1/512 of 1/4096 octave */
    bool pitched;
    bool dls2_set;
    bool peq_set;
    uint16_t lfo_phase[2];
    uint16_t lfo_wait[2];
    bool lfo_armed;
    HrtfFilter hrtf;
} MCPXAPUVoiceFilter;

typedef struct VoiceWorkItem {
    int voice;
    int list;
} VoiceWorkItem;

/* A voice processor worker. A frame's voices are a list of units (a voice,
 * or a multipass group: the voices that mix into its bin and the voice
 * that reads it); the emulator thread and every worker take units from
 * one claim word until none is left (voice_work_dispatch). The emulator
 * thread mixes into the DSP's buffer, a worker into its own, which the
 * emulator thread adds in once every unit is done; it never sleeps on a
 * worker, it only spins for the units a worker has already started.
 * Lock-free: a worker is woken by its generation counter moving on. */
typedef struct VoiceWorker {
    struct sthread *thread;
    struct MCPXAPUState *apu;
    int id;
    retro_atomic_int_t generation; /* moved on by the dispatcher */
    int seen_generation;           /* the worker's own */
    retro_eventcount_t wake;
    /* the frame (claim generation) its buffers hold units of: set before
     * its first unit of the frame is mixed */
    retro_atomic_int_t used_gen;
    int64_t mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    int64_t sample_buf[NUM_SAMPLES_PER_FRAME][2];
} VoiceWorker;

typedef struct VoiceWorkUnit {
    int16_t start;
    int16_t count;
} VoiceWorkUnit;

typedef struct VoiceWorkDispatch {
    /* workers[0] is the emulator thread itself; 1.. have threads */
    int num_workers;
    VoiceWorker *workers;
    retro_atomic_int_t should_exit;
    /* the frame's claim generation (15 bits) and next unit (16): a
     * worker still on an earlier frame finds another generation and
     * stops, never taking a unit of this one */
    retro_atomic_int_t claim;
    /* num_units for the claims: a worker can look at it while the next
     * frame's schedule is made, its claim then failing (atomic: no torn
     * or undefined read) */
    retro_atomic_int_t claim_limit;
    retro_atomic_int_t units_done; /* units mixed this frame */
    int gen;
    VoiceWorkUnit units[MCPX_HW_MAX_VOICES];
    int num_units;
    VoiceWorkItem queue[MCPX_HW_MAX_VOICES];
    int queue_len;
} VoiceWorkDispatch;

typedef struct {
    MemoryRegion mmio;
    VoiceWorkDispatch voice_work_dispatch;
    MCPXAPUVoiceFilter filters[MCPX_HW_MAX_VOICES];

    // FIXME: Where are these stored?
    int ssl_base_page;
    MCPXAPUVPSSLData ssl[MCPX_HW_MAX_VOICES];
    uint8_t hrtf_headroom;
    uint8_t hrtf_submix[4];
    uint8_t submix_headroom[NUM_MIXBINS];
    /* the VP monitor's mix (all voices, for the frontend with the DSPs
     * off), in mix steps as a bin */
    int64_t sample_buf[NUM_SAMPLES_PER_FRAME][2];
    uint64_t voice_locked[4];

    struct {
        int current_entry;
        // FIXME: Stored in RAM
        struct {
            int32_t hrir[2][HRTF_NUM_TAPS]; /* 1/2048 */
            int32_t itd;                    /* 1/512 sample */
        } entries[HRTF_ENTRY_COUNT];
    } hrtf;

    uint32_t inbuf_sge_handle; //FIXME: Where is this stored?
    uint32_t outbuf_sge_handle; //FIXME: Where is this stored?
} MCPXAPUVPState;

extern const MemoryRegionOps vp_ops;

void mcpx_apu_vp_init(MCPXAPUState *d);
void mcpx_apu_vp_finalize(MCPXAPUState *d);
void mcpx_apu_vp_frame(MCPXAPUState *d, int64_t mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
void mcpx_apu_vp_reset(MCPXAPUState *d);

#endif
