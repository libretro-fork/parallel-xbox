/* Host volume must not add a DC bias or change unity-gain samples.
 * cc -O2 -I. libretro/tests/test-audio-volume.c -lm -o test-audio-volume
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include "hw/xbox/mcpx/apu/monitor.h"

/* Independent long-double oracle, using the C library's rounding operation. */
static int reference_f32(float sample, int gain)
{
    if (!gain || isnan(sample)) return 0;
    long double scaled = (long double)sample * gain / 2.0L;
    if (scaled >= 32767) return 32767;
    if (scaled <= -32768) return -32768;
    return (int)roundl(scaled);
}

static void check_float_output(void)
{
    unsigned legacy_mismatches = 0;
    for (int sample = -32768; sample <= 32767; sample++) {
        float normalized = sample / 32768.0f;
        assert(mcpx_apu_monitor_convert_f32(normalized, 65536) == sample);
        legacy_mismatches += (int)(normalized * 32767.0f) != sample;
        for (int gain = 0; gain <= 65536; gain += 256) {
            int actual = mcpx_apu_monitor_convert_f32(normalized, gain);
            assert(actual == reference_f32(normalized, gain));
            assert(actual == mcpx_apu_monitor_scale_s16(sample, gain));
        }
    }
    assert(legacy_mismatches == 65535);
    const float edges[] = { -INFINITY, -FLT_MAX, -1.0f, -0.5f,
        -3.0f / 65536, -1.0f / 65536, -FLT_MIN, -0.0f, 0.0f,
        FLT_MIN, 1.0f / 65536, 3.0f / 65536, 0.5f, 1.0f,
        FLT_MAX, INFINITY, NAN };
    for (int gain = 0; gain <= 65536; gain++) {
        for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
            float sample = edges[i];
            assert(mcpx_apu_monitor_convert_f32(sample, gain) ==
                   reference_f32(sample, gain));
        }
    }
    /* Arbitrary IEEE float values, including subnormals and nonfinite data,
     * plus adjacent representable values around quantization half steps. */
    uint32_t random = 0x72849abc;
    for (unsigned i = 0; i < 1000000; i++) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        float sample;
        memcpy(&sample, &random, sizeof(sample));
        int gain = i % 65537;
        assert(mcpx_apu_monitor_convert_f32(sample, gain) == reference_f32(sample, gain));
    }
    for (int n = -32768; n < 32767; n++) {
        float tie = (n + 0.5f) / 32768.0f;
        float around[] = { nextafterf(tie, -INFINITY), tie,
                           nextafterf(tie, INFINITY) };
        for (unsigned i = 0; i < 3; i++) {
            assert(mcpx_apu_monitor_convert_f32(around[i], 65536) ==
                   reference_f32(around[i], 65536));
        }
    }
    printf("float output: all PCM samples round-trip; legacy mismatches=%u, new=0; high-precision oracle passed\n",
           legacy_mismatches);
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
    check_float_output();
    puts("audio volume: nearest rounding, zero DC, mute and unity passed");
    return 0;
}
