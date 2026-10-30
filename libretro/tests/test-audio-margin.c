/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The frontend's audio pull never waits for the DSPs' thread
 * (hw/xbox/mcpx/apu/monitor.c), so the queue's preroll has to cover the
 * engine frame that thread may still be on. This works out the worst
 * margin over every phase of the APU's engine frames (32 samples, 1500 a
 * second of the guest's time) against the video frames (1001/60 ms, 800
 * or 801 samples, 4004 every 5: ui/libretro.c), with the worst
 * assumptions: the last engine frame of the video frame not yet made, and
 * one due exactly at the frame's end not yet run. Every pull has to find
 * its samples there, with a whole engine frame to spare. */
#include <stdio.h>
#include "../../hw/xbox/mcpx/apu/monitor.h"

#define SAMPLES 32
/* time in 1/60 us: an engine frame 2/3 ms, a video frame 1001/60 ms */
#define T_ENGINE 40000LL
#define T_VIDEO 1001000LL

/* the least room a pull finds over the given phase, in samples, before it
 * takes its own; frames: 200 = five times the cadences' common period */
static long long worst_margin(int preroll)
{
    long long worst = 1LL << 40;

    for (long long t0 = 0; t0 < T_ENGINE; t0++) {
        long long taken = 0, rem = 0;
        for (long long n = 1; n <= 200; n++) {
            long long end = n * T_VIDEO, made = 0, want, room;
            rem += 4004;
            want = rem / 5;
            rem %= 5;
            if (end > t0) {
                made = (end - t0 - 1) / T_ENGINE + 1;   /* due before end */
            }
            if (made > 0) {
                made--;                                 /* one in flight */
            }
            room = (long long)preroll * SAMPLES + made * SAMPLES - taken;
            if (room - want < worst) {
                worst = room - want;
            }
            taken += want;
        }
    }
    return worst;
}

int main(void)
{
    long long m = worst_margin(MCPX_APU_MONITOR_PREROLL_FRAMES);
    long long less = worst_margin(MCPX_APU_MONITOR_PREROLL_FRAMES - 1);

    printf("audio pull margin: %lld samples at the worst phase "
           "(%lld with a frame less of preroll)\n", m, less);
    if (m < SAMPLES) {
        printf("FAIL: the preroll (%d engine frames) does not cover the "
               "frame in flight with a frame to spare\n",
               MCPX_APU_MONITOR_PREROLL_FRAMES);
        return 1;
    }
    return 0;
}
