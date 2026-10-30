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

void libretro_audio_set_volume(float volume);
size_t libretro_audio_read_begin(const int16_t **samples);
void libretro_audio_read_end(size_t frames);
void libretro_audio_discard(void);

#endif
