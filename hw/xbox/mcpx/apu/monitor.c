/*
 * QEMU MCPX Audio Processing Unit implementation
 *
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

/*
 * The APU's output, to the libretro frontend.
 *
 * Every engine frame (apu.c, 32 samples of guest time) puts its 32 stereo
 * samples into a lock-free single-producer, single-consumer queue: the
 * emulator thread produces, retro_run on the frontend's thread consumes,
 * exactly one video frame's worth (libretro_audio_pull). The queue starts
 * with one engine frame of silence, so a video frame's samples are always
 * there when retro_run takes them: an engine frame's 32 samples are not
 * split across two video frames' worth.
 */

#include "apu_int.h"
#include <retro_spsc.h>

#define LIBRETRO_APU_QUEUE_FRAMES 16384
#define FRAME_BYTES 4 /* stereo S16 */

static retro_spsc_t apu_queue;
static bool apu_queue_ready;
/* Audio > Audio Volume, as standalone's curve: the gain the samples are
 * scaled by, in 1/65536 */
static int32_t apu_gain_q16 = 65536;
/* Engine frames dropped because the frontend stopped taking samples */
uint32_t libretro_audio_dropped;

static void apu_queue_reset(void)
{
    static const int16_t preroll[NUM_SAMPLES_PER_FRAME * 2];

    retro_spsc_clear(&apu_queue);
    retro_spsc_write(&apu_queue, preroll, sizeof(preroll));
}

void libretro_audio_set_volume(float volume)
{
    float vu = pow(fmax(0.0, fmin(volume, 1.0)), M_E);
    qatomic_set(&apu_gain_q16, (int32_t)(vu * 65536.0f + 0.5f));
}

/* Consumer, the frontend's thread, the emulator thread parked: up to
 * max_frames stereo frames, the count actually there */
int libretro_audio_pull(int16_t *out_buf, int max_frames)
{
    if (!apu_queue_ready) {
        return 0;
    }
    return (int)(retro_spsc_read(&apu_queue, out_buf,
                                 (size_t)max_frames * FRAME_BYTES) /
                 FRAME_BYTES);
}

/* Both sides stopped (a reset, a state loaded, new content): the queue
 * starts over */
void libretro_audio_discard(void)
{
    if (apu_queue_ready) {
        apu_queue_reset();
    }
}

void mcpx_apu_monitor_init(MCPXAPUState *d, Error **errp)
{
    if (!apu_queue_ready) {
        if (!retro_spsc_init(&apu_queue,
                             LIBRETRO_APU_QUEUE_FRAMES * FRAME_BYTES)) {
            error_setg(errp, "no memory for the audio queue");
            return;
        }
        apu_queue_ready = true;
    }
    apu_queue_reset();
}

void mcpx_apu_monitor_finalize(MCPXAPUState *d)
{
}

static void push_frame(const int16_t (*samples)[2])
{
    const int32_t gain = qatomic_read(&apu_gain_q16);
    int16_t out[NUM_SAMPLES_PER_FRAME][2];
    const void *src = samples;

    if (!apu_queue_ready) {
        return;
    }
    if (gain != 65536) {
        for (int i = 0; i < NUM_SAMPLES_PER_FRAME; i++) {
            out[i][0] = (int16_t)(((int64_t)samples[i][0] * gain) >> 16);
            out[i][1] = (int16_t)(((int64_t)samples[i][1] * gain) >> 16);
        }
        src = out;
    }
    if (retro_spsc_write_avail(&apu_queue) < sizeof(out)) {
        libretro_audio_dropped++;
        return;
    }
    retro_spsc_write(&apu_queue, src, sizeof(out));
}

/* After each engine frame: its 32 samples, out of the 256 the EP builds at
 * once (or the VP or GP built for this frame), then their slot is cleared */
void mcpx_apu_monitor_frame(MCPXAPUState *d)
{
    int16_t (*slot)[2] =
        &d->monitor.frame_buf[(d->ep_frame_div % 8) * NUM_SAMPLES_PER_FRAME];

    push_frame((const int16_t (*)[2])slot);
    memset(slot, 0, NUM_SAMPLES_PER_FRAME * sizeof(*slot));
}

/* An engine frame with the engine stopped */
void mcpx_apu_monitor_silence(MCPXAPUState *d)
{
    static const int16_t silence[NUM_SAMPLES_PER_FRAME][2];

    push_frame(silence);
}
