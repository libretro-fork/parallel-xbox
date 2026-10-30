/* Host volume must not add a DC bias or change unity-gain samples.
 * cc -O2 -I. libretro/tests/test-audio-volume.c -lm -o test-audio-volume
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "hw/xbox/mcpx/apu/monitor.h"

int main(void)
{
    /* Every S16 sample, over the gain range including mute and unity. */
    for (int gain = 0; gain <= 65536; gain += 256) {
        for (int sample = -32768; sample <= 32767; sample++) {
            int actual = mcpx_apu_monitor_scale_s16(sample, gain);
            double reference = (double)sample * gain / 65536.0;
            assert(actual == (int)round(reference));
            assert(fabs(actual - reference) <= 0.5);
            if (sample != -32768) {
                assert(actual + mcpx_apu_monitor_scale_s16(-sample, gain) == 0);
            }
            if (gain == 65536) {
                assert(actual == sample);
            }
        }
    }
    /* All fractional gains, especially values on either side of a tie. */
    const int16_t edges[] = { -32768, -32767, -257, -3, -1, 0, 1, 3, 257, 32767 };
    for (int gain = 0; gain <= 65536; gain++) {
        for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
            assert(mcpx_apu_monitor_scale_s16(edges[i], gain) ==
                   (int)round((double)edges[i] * gain / 65536.0));
        }
    }
    puts("audio volume: nearest rounding, zero DC, mute and unity passed");
    return 0;
}
