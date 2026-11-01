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
 * The emulator thread produces VP/GP/EP output while it advances guest time;
 * the frontend consumes exactly one video frame's worth after that turn.
 * DSP DMA and register accesses share the engine owner. The existing queue
 * lead is preserved (monitor.h), including the EP's eight-frame burst.
 */

#include "apu_int.h"
#include "dsp/gp_ep.h"
#include <retro_spsc.h>
#include "monitor.h"

#define LIBRETRO_APU_QUEUE_FRAMES 16384
#define FRAME_BYTES 4 /* stereo S16 */

static retro_spsc_t apu_queue;
static bool apu_queue_ready;
/* Audio > Audio Volume, as standalone's curve: the gain the samples are
 * scaled by, in 1/65536 */
static retro_atomic_int_t apu_gain_q16 = RETRO_ATOMIC_INT_INITIALIZER(65536);
/* Engine frames already in the queue ahead of the engine: the EP's eight
 * go in at once (mcpx_apu_monitor_end_ahead), and the next seven frames'
 * output is skipped, whichever producer it comes from. The producer
 * side is the engine owner;
 * the queue's reset, with both stopped, clears it. */
static unsigned apu_ahead;

static void apu_queue_reset(void)
{
    /* MCPX_APU_MONITOR_PREROLL_FRAMES engine frames (monitor.h) */
    static const int16_t preroll[MCPX_APU_MONITOR_PREROLL_FRAMES *
                                 NUM_SAMPLES_PER_FRAME * 2];

    retro_spsc_clear(&apu_queue);
    apu_ahead = 0;
    retro_spsc_write(&apu_queue, preroll, sizeof(preroll));
}

void libretro_audio_set_volume(float volume)
{
    float vu = pow(fmax(0.0, fmin(volume, 1.0)), M_E);
    retro_atomic_store_relaxed_int(&apu_gain_q16, (int)(vu * 65536.0f + 0.5f));
}

/* State commands run with the engine stopped and the frontend not reading. */
size_t mcpx_apu_monitor_save(int16_t *samples, size_t max, uint32_t *ahead)
{
    size_t frames = 0;

    *ahead = 0;
    if (apu_queue_ready) {
        frames = MIN(retro_spsc_read_avail(&apu_queue) / FRAME_BYTES, max);
        retro_spsc_peek(&apu_queue, samples, frames * FRAME_BYTES);
        *ahead = apu_ahead;
    }
    return frames;
}

void mcpx_apu_monitor_restore(const int16_t *samples, size_t frames,
                              uint32_t ahead)
{
    if (!apu_queue_ready) {
        return;
    }
    retro_spsc_clear(&apu_queue);
    retro_spsc_write(&apu_queue, samples, frames * FRAME_BYTES);
    apu_ahead = ahead;
}

/* Consumer, the frontend between engine turns: a contiguous pointer to
 * the produced samples. The retained preroll covers cadence phase (monitor.h). */
size_t libretro_audio_read_begin(const int16_t **samples)
{
    const void *p;

    if (!apu_queue_ready) {
        return 0;
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

/* The stopped engine owns the DSPs and the queue at reset/state boundaries. */
void libretro_audio_discard(void)
{
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

/* The device going away: the frontend's thread is not reading (the
 * content is closed), so the queue goes too; the next device makes one */
void mcpx_apu_monitor_finalize(MCPXAPUState *d)
{
    if (apu_queue_ready) {
        apu_queue_ready = false;
        retro_spsc_free(&apu_queue);
    }
}

/* The samples' room in the queue, or none: a frame the frontend has no
 * room for is dropped whole */
static int16_t *monitor_room(size_t frames)
{
    void *p;

    if (!apu_queue_ready ||
        retro_spsc_write_avail(&apu_queue) < frames * FRAME_BYTES) {
        return NULL;
    }
    /* mirrored: the room is contiguous */
    if (retro_spsc_write_begin(&apu_queue, &p) < frames * FRAME_BYTES) {
        return NULL;
    }
    return p;
}

/* One engine frame already queued ahead: this frame's output is not */
static bool monitor_frame_ahead(void)
{
    if (apu_ahead) {
        apu_ahead--;
        return true;
    }
    return false;
}

/* An engine frame's room, written in place and committed with
 * mcpx_apu_monitor_end; none when it is queued ahead or has no room */
int16_t *mcpx_apu_monitor_begin(size_t frames)
{
    if (monitor_frame_ahead()) {
        return NULL;
    }
    return monitor_room(frames);
}

/* Room for several engine frames at once, for a producer that writes
 * them in place (the EP's DMA: dsp/gp_ep.c); none when the queue is full */
int16_t *mcpx_apu_monitor_begin_ahead(size_t frames)
{
    return monitor_room(frames);
}

/* Those frames committed, the volume applied where they are: the first is
 * this engine frame's output, the rest the next frames' */
void mcpx_apu_monitor_end_ahead(int16_t *out, size_t frames)
{
    const int32_t gain = retro_atomic_load_relaxed_int(&apu_gain_q16);

    if (gain != 65536) {
        for (size_t i = 0; i < 2 * frames; i++) {
            out[i] = mcpx_apu_monitor_scale_s16(out[i], gain);
        }
    }
    retro_spsc_write_end(&apu_queue, frames * FRAME_BYTES);
    apu_ahead += frames / NUM_SAMPLES_PER_FRAME - 1;
}

int32_t mcpx_apu_monitor_gain_q16(void)
{
    return retro_atomic_load_relaxed_int(&apu_gain_q16);
}

void mcpx_apu_monitor_end(size_t frames)
{
    retro_spsc_write_end(&apu_queue, frames * FRAME_BYTES);
}

/* Producer: stereo frames of the VP monitor's mix (mix steps, sample.h) */
void mcpx_apu_monitor_push_mix(const int64_t (*samples)[2], size_t frames)
{
    const int32_t gain = retro_atomic_load_relaxed_int(&apu_gain_q16);
    int16_t *out = mcpx_apu_monitor_begin(frames);

    if (!out) {
        return;
    }
    for (size_t i = 0; i < 2 * frames; i++) {
        out[i] = mcpx_apu_monitor_convert_mix(((const int64_t *)samples)[i], gain);
    }
    retro_spsc_write_end(&apu_queue, frames * FRAME_BYTES);
}

/* Producer: a frame of silence */
void mcpx_apu_monitor_push_silence(void)
{
    int16_t *out = mcpx_apu_monitor_begin(NUM_SAMPLES_PER_FRAME);

    if (!out) {
        return;
    }
    memset(out, 0, NUM_SAMPLES_PER_FRAME * FRAME_BYTES);
    retro_spsc_write_end(&apu_queue, NUM_SAMPLES_PER_FRAME * FRAME_BYTES);
}

/* An engine frame with the engine stopped: silence, in order with the
 * frames before it, from the engine (the queue's one producer) */
void mcpx_apu_monitor_silence(MCPXAPUState *d)
{
    mcpx_apu_dsp_post_silence(d);
}
