/*
 * The APU's output queue to the frontend (monitor.c), as ui/libretro.c and
 * the tests see it. No QEMU headers: libretro/tests/test-audio-margin.c
 * includes it on its own.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_XBOX_MCPX_APU_MONITOR_H
#define HW_XBOX_MCPX_APU_MONITOR_H

#include <stddef.h>
#include <stdint.h>

/*
 * Engine frames of silence the queue starts with. The frontend's thread
 * takes each video frame's 800 or 801 samples without waiting for the
 * DSPs' thread, so the engine frame that thread may still be working on
 * (at most one: posting a frame waits for the one before) is not there
 * yet. One frame covers the 25 or 26 engine frames of a video frame
 * against its samples at the worst phase of the two, one the frame in
 * flight, and one is a frame of slack. The test works the margin out.
 */
#define MCPX_APU_MONITOR_PREROLL_FRAMES 3

/* Host volume only, after the guest's S16 output conversion. Gain is Q16
 * in [0, 65536]. Round symmetrically: shifting a signed product downward
 * biases attenuated negative samples, even for a zero-mean input. */
static inline int16_t mcpx_apu_monitor_scale_s16(int16_t sample, int32_t gain)
{
    int64_t product = (int64_t)sample * gain;
    return (int16_t)((product + (product < 0 ? -32768 : 32768)) / 65536);
}

/* The VP's normalized host floats use the same 32768 scale as PCM input.
 * Quantize once, after host gain, with symmetric nearest rounding. Double
 * holds the float-by-Q16 product exactly and avoids premature float rounding
 * around half steps. Nonfinite filter output cannot be cast to an integer. */
static inline int16_t mcpx_apu_monitor_convert_f32(float sample, int32_t gain)
{
    if (!gain) {
        return 0;
    }
    double value = (double)sample * gain * 0.5;
    if (value != value) {
        return 0;
    }
    if (value >= 32767.0) {
        return 32767;
    }
    if (value <= -32768.0) {
        return -32768;
    }
    return (int16_t)(value + (value < 0 ? -0.5 : 0.5));
}

void libretro_audio_set_volume(float volume);
size_t libretro_audio_read_begin(const int16_t **samples);
void libretro_audio_read_end(size_t frames);
void libretro_audio_discard(void);

#endif
