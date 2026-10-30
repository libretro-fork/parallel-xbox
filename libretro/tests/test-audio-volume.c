/* Host volume must not add a DC bias or change unity-gain samples.
 * cc -O2 -I. libretro/tests/test-audio-volume.c -lm -o test-audio-volume
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "hw/xbox/mcpx/apu/monitor.h"

/* Independent 128-bit oracle for the VP monitor's mix (2^44 is full
 * scale): round(mix * gain / 2^45) half away from zero, saturated. */
static int reference_mix(int64_t mix, int gain)
{
    __int128 n = (__int128)mix * gain;
    __int128 a = n < 0 ? -n : n;
    __int128 q = (a + ((__int128)1 << 44)) >> 45;
    if (n >= 0) return q >= 32767 ? 32767 : (int)q;
    return q >= 32768 ? -32768 : -(int)q;
}

static void check_mix_output(void)
{
    for (int sample = -32768; sample <= 32767; sample++) {
        int64_t mix = (int64_t)sample * ((int64_t)1 << 29);
        assert(mcpx_apu_monitor_convert_mix(mix, 65536) == sample);
        for (int gain = 0; gain <= 65536; gain += 256) {
            int actual = mcpx_apu_monitor_convert_mix(mix, gain);
            assert(actual == reference_mix(mix, gain));
            assert(actual == mcpx_apu_monitor_scale_s16(sample, gain));
        }
    }
    const int64_t edges[] = { INT64_MIN, INT64_MIN + 1, -((int64_t)1 << 62),
        -((int64_t)1 << 44), -((int64_t)1 << 28), -1, 0, 1,
        (int64_t)1 << 28, (int64_t)1 << 44, (int64_t)1 << 62, INT64_MAX };
    for (int gain = 0; gain <= 65536; gain++) {
        for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
            assert(mcpx_apu_monitor_convert_mix(edges[i], gain) ==
                   reference_mix(edges[i], gain));
        }
    }
    /* Arbitrary mixes at every magnitude, and both sides of each half
     * step at unity and an odd gain */
    uint64_t random = 0x72849abc1234567ull;
    for (unsigned i = 0; i < 4000000; i++) {
        random ^= random << 13; random ^= random >> 7; random ^= random << 17;
        int64_t mix = (int64_t)random >> (random % 40);
        int gain = i % 65537;
        assert(mcpx_apu_monitor_convert_mix(mix, gain) == reference_mix(mix, gain));
    }
    for (int n = -32768; n < 32767; n++) {
        int64_t tie = (int64_t)n * ((int64_t)1 << 29) + ((int64_t)1 << 28);
        for (int64_t k = -1; k <= 1; k++) {
            assert(mcpx_apu_monitor_convert_mix(tie + k, 65536) ==
                   reference_mix(tie + k, 65536));
            assert(mcpx_apu_monitor_convert_mix(tie + k, 40961) ==
                   reference_mix(tie + k, 40961));
        }
    }
    printf("VP mix output: all PCM samples round-trip; 128-bit oracle passed\n");
}

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
    check_mix_output();
    puts("audio volume: nearest rounding, zero DC, mute and unity passed");
    return 0;
}
