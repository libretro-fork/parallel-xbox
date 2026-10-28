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
 * A lock-free single-producer, single-consumer queue, mirrored so both
 * ends see every byte contiguously: the producer writes the samples into
 * it in one pass (the volume applied as they go), and retro_run on the
 * frontend's thread hands the frontend a pointer into it, exactly one
 * video frame's worth (libretro_audio_read_begin/end), without copying.
 *
 * The producer is whichever thread builds the output (apu.c): the DSPs'
 * thread with APU DSP Emulation on (the EP's 256 samples every 8th engine
 * frame, or the GP's 32 each frame), the emulator thread with it off (the
 * VP's 32 each frame) and for the frames with the engine stopped. The
 * two never produce at once: the switch waits for the thread's frame
 * (mcpx_apu_update_dsp_preference), and retro_run waits for it before
 * reading (libretro_audio_read_begin), so what it reads is what the guest
 * time run made, whatever the thread's timing. The queue starts with two
 * engine frames of silence, so a video frame's 800 or 801 samples are
 * always there out of the engine's 32-sample frames (apu_queue_reset).
 */

#include "apu_int.h"
#include "dsp/gp_ep.h"
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
    /* two engine frames: the 25 or 26 of a video frame against its 800
     * or 801 samples can leave the queue a frame short at the worst
     * phase of the two, and one more frame keeps every pull whole */
    static const int16_t preroll[2 * NUM_SAMPLES_PER_FRAME * 2];

    retro_spsc_clear(&apu_queue);
    retro_spsc_write(&apu_queue, preroll, sizeof(preroll));
}

void libretro_audio_set_volume(float volume)
{
    float vu = pow(fmax(0.0, fmin(volume, 1.0)), M_E);
    qatomic_set(&apu_gain_q16, (int32_t)(vu * 65536.0f + 0.5f));
}

/* Consumer, the frontend's thread, the emulator thread parked: a pointer
 * to the samples there, contiguous, and how many frames; the DSPs' thread
 * done with the engine's last frame first */
size_t libretro_audio_read_begin(const int16_t **samples)
{
    const void *p;

    if (!apu_queue_ready) {
        return 0;
    }
    if (g_state) {
        mcpx_apu_dsp_quiesce(g_state);
    }
    size_t bytes = retro_spsc_read_begin(&apu_queue, &p);
    *samples = p;
    return bytes / FRAME_BYTES;
}

void libretro_audio_read_end(size_t frames)
{
    if (apu_queue_ready) {
        retro_spsc_read_end(&apu_queue, frames * FRAME_BYTES);
    }
}

/* Both sides stopped (a reset, a state loaded, new content): the queue
 * starts over, once the DSPs' thread is done producing */
void libretro_audio_discard(void)
{
    if (g_state) {
        mcpx_apu_dsp_quiesce(g_state);
    }
    if (apu_queue_ready) {
        apu_queue_reset();
    }
}

void mcpx_apu_monitor_init(MCPXAPUState *d, Error **errp)
{
    if (!apu_queue_ready) {
        if (!retro_spsc_init_mirrored(&apu_queue,
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

/* The samples' room in the queue, or none: a frame the frontend has no
 * room for is dropped whole */
static int16_t *monitor_room(size_t frames)
{
    void *p;

    if (!apu_queue_ready ||
        retro_spsc_write_avail(&apu_queue) < frames * FRAME_BYTES) {
        libretro_audio_dropped++;
        return NULL;
    }
    /* mirrored: the room is contiguous */
    if (retro_spsc_write_begin(&apu_queue, &p) < frames * FRAME_BYTES) {
        libretro_audio_dropped++;
        return NULL;
    }
    return p;
}

/* Producer: stereo S16 frames, as the DSPs make them */
void mcpx_apu_monitor_push_s16(const int16_t (*samples)[2], size_t frames)
{
    const int32_t gain = qatomic_read(&apu_gain_q16);
    int16_t *out = monitor_room(frames);

    if (!out) {
        return;
    }
    if (gain == 65536) {
        memcpy(out, samples, frames * FRAME_BYTES);
    } else {
        for (size_t i = 0; i < frames; i++) {
            out[2 * i]     = (int16_t)(((int64_t)samples[i][0] * gain) >> 16);
            out[2 * i + 1] = (int16_t)(((int64_t)samples[i][1] * gain) >> 16);
        }
    }
    retro_spsc_write_end(&apu_queue, frames * FRAME_BYTES);
}

/* Producer: stereo float frames, as the VP makes them */
void mcpx_apu_monitor_push_f32(const float (*samples)[2], size_t frames)
{
    const float gain = (float)qatomic_read(&apu_gain_q16) * (32767.0f / 65536.0f);
    int16_t *out = monitor_room(frames);

    if (!out) {
        return;
    }
    for (size_t i = 0; i < 2 * frames; i++) {
        float v = ((const float *)samples)[i] * gain;
        out[i] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v);
    }
    retro_spsc_write_end(&apu_queue, frames * FRAME_BYTES);
}

/* Producer: a frame of silence */
void mcpx_apu_monitor_push_silence(void)
{
    int16_t *out = monitor_room(NUM_SAMPLES_PER_FRAME);

    if (!out) {
        return;
    }
    memset(out, 0, NUM_SAMPLES_PER_FRAME * FRAME_BYTES);
    retro_spsc_write_end(&apu_queue, NUM_SAMPLES_PER_FRAME * FRAME_BYTES);
}

/* An engine frame with the engine stopped, on the emulator thread, once
 * the DSPs' thread is done producing */
void mcpx_apu_monitor_silence(MCPXAPUState *d)
{
    mcpx_apu_dsp_quiesce(d);
    mcpx_apu_monitor_push_silence();
}
