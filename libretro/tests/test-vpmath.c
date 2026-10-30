/* The voice processor's integer control math (hw/xbox/mcpx/apu/vp/vpmath.h)
 * against the double-precision code it replaced: the resampler's kernel
 * table, the cutoff curve, the pitch step and the envelopes' exponential
 * decay. run.sh runs it sampled; "full" checks every input (minutes).
 * cc -O2 -I. libretro/tests/test-vpmath.c -lm -o test-vpmath
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/mcpx/apu/vp/vpmath.h"

static uint64_t rs = 0x2545f4914f6cdd1dull;
static uint64_t rnd(void)
{
    rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17;
    return rs;
}

/* the replaced code, as it was */
static double old_lagrange(int row, int k)
{
    double weight = 1.0;
    for (int j = -3; j <= 4; j++) {
        if (j != k) {
            weight *= (row / 256.0 - j) / (k - j);
        }
    }
    return round(weight * 65536.0);
}

static int32_t old_cutoff(int32_t k)
{
    int octave = floor(k / 1024.0);
    double c = ldexp(exp2((k - octave * 1024) / 1024.0) * 65536.0, octave);
    return (int32_t)fmin(round(c), 65535.0);
}

static uint32_t old_step(double pitch)
{
    return exp2(pitch / 4096.0) * 65536.0;
}

static uint32_t old_decay(uint32_t a, uint32_t n, uint32_t d)
{
    return (uint32_t)floor(a * exp(-(double)n / d));
}

int main(int argc, char **argv)
{
    int full = argc > 1 && strcmp(argv[1], "full") == 0;
    unsigned long checked = 0, mismatches = 0;

    for (int row = 0; row <= 256; row++) {
        for (int k = 0; k < 8; k++) {
            assert(lagrange_rows[row][k] == old_lagrange(row, k - 3));
        }
    }
    /* every cutoff the registers reach (k = (FC * 64 + bends) >> 8) */
    for (int32_t k = -24576; k <= 16384; k++) {
        assert(vp_cutoff_coefficient(k) == old_cutoff(k));
    }
    /* every steady pitch (1/512 of the register's unit, the envelope and
     * LFO resolution) over the registers' range, or a sample of them;
     * ramped pitches (1/2^14) at random */
    for (int64_t p9 = -46 * 1024 * 512; p9 <= 46 * 1024 * 512; p9 += full ? 1 : 97) {
        uint32_t a = vp_pitch_step(p9 * 32), b = old_step(p9 / 512.0);
        checked++;
        if (a != b) {
            mismatches++;
            /* only where the double result sits within its own error of
             * an integer */
            double exact = exp2(p9 / 512.0 / 4096.0) * 65536.0;
            assert(fabs(exact - nearbyint(exact)) < 1e-6 * exact / 65536);
        }
    }
    for (unsigned long i = 0; i < (full ? 400000000ul : 2000000ul); i++) {
        int64_t p14 = (int64_t)(rnd() % (2ull * 46 * 1024 * 512 * 32)) - 46ll * 1024 * 512 * 32;
        uint32_t a = vp_pitch_step(p14), b = old_step(p14 / 16384.0);
        checked++;
        if (a != b) {
            mismatches++;
            double exact = exp2(p14 / 16384.0 / 4096.0) * 65536.0;
            assert(fabs(exact - nearbyint(exact)) < 1e-6 * exact / 65536);
        }
    }
    /* decay: every count and rate (2048 e^(-count / 2R)), or a sample;
     * release: start (a level byte in 8ths) e^(-t / 2R) at random */
    for (uint32_t r = 1; r <= 4095; r += full ? 1 : 37) {
        for (uint32_t n = 0; n <= 65535; n += full ? 1 : 13) {
            uint32_t a = vp_exp_decay(2048, n, 2 * r), b = old_decay(2048, n, 2 * r);
            checked++;
            if (a != b) {
                mismatches++;
                double exact = 2048 * exp(-(double)n / (2 * r));
                assert(fabs(exact - nearbyint(exact)) < 1e-9);
            }
        }
    }
    for (unsigned long i = 0; i < (full ? 400000000ul : 2000000ul); i++) {
        uint32_t r = 1 + rnd() % 4095, t = rnd() % (11 * r);
        uint32_t start = (rnd() % 256) * 8;
        uint32_t a = vp_exp_decay(start, t, 2 * r), b = old_decay(start, t, 2 * r);
        checked++;
        if (a != b) {
            mismatches++;
            double exact = start * exp(-(double)t / (2 * r));
            assert(fabs(exact - nearbyint(exact)) < 1e-9);
        }
    }
    printf("VP math: kernel and cutoff identical; %lu pitch steps and decays, "
           "%lu different from double\n", checked, mismatches);
    return 0;
}
