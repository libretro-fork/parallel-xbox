/*
 * HRTF Filter
 *
 * Copyright (c) 2025 Matt Borgerson
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

#ifndef HW_XBOX_MCPX_HRTF_H
#define HW_XBOX_MCPX_HRTF_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "hw/xbox/mcpx/apu/apu_regs.h"

#define HRTF_SAMPLES_PER_FRAME  NUM_SAMPLES_PER_FRAME
#define HRTF_NUM_TAPS           31
#define HRTF_MAX_DELAY_SAMPLES  42
#define HRTF_BUFLEN             (HRTF_NUM_TAPS + HRTF_MAX_DELAY_SAMPLES)

/* Taps in 1/2048 (the HRIR minifloats' own steps), the delay (ITD) in
 * 1/512 of a sample (its register's s6.9), samples in sample steps */
typedef struct {
    int buf_pos;
    int32_t buf[HRTF_BUFLEN];
    int32_t hrir_coeff_cur[2][HRTF_NUM_TAPS];
    int32_t itd_cur;
    bool params_set;
} HrtfFilter;

static inline void hrtf_filter_init(HrtfFilter *f)
{
    memset(f, 0, sizeof(*f));
}

static inline void hrtf_filter_clear_history(HrtfFilter *f)
{
    f->buf_pos = 0;
    memset(f->buf, 0, sizeof(f->buf));
}

static inline void hrtf_filter_process(HrtfFilter *f,
                                       const int32_t in[HRTF_SAMPLES_PER_FRAME][2],
                                       int32_t out[HRTF_SAMPLES_PER_FRAME][2],
                                       const int32_t target[2][HRTF_NUM_TAPS],
                                       int32_t itd_target)
{
    const int32_t itd_max = HRTF_MAX_DELAY_SAMPLES * 512;

    itd_target = itd_target > itd_max ? itd_max :
                 itd_target < -itd_max ? -itd_max : itd_target;
    /* The target is borrowed only for this call. Retain the last completed
     * frame's parameters so later entry edits cannot alter the ramp start. */
    /* Taps and delay move from the last frame's to the target across one
     * frame; a voice's first frame starts at the target. The tap curve is
     * measured on hardware; applying it to delay is an assumption. Both
     * ramps are exact: n/32 of the way is (from * (32 - n) + to * n) / 32,
     * so taps are in 1/65536 and the delay in 1/16384 of a sample. */
    const int32_t (*previous)[HRTF_NUM_TAPS] =
        f->params_set ? (const int32_t (*)[HRTF_NUM_TAPS])f->hrir_coeff_cur :
                        target;
    int32_t itd_from = f->params_set ? f->itd_cur : itd_target;
    for (int n = 0; n < HRTF_SAMPLES_PER_FRAME; n++) {
        /* One mono history serves both ears. No stereo duplication. */
        int32_t *buf = f->buf;
        buf[f->buf_pos] = in[n][0];
        int32_t itd = itd_from * (HRTF_SAMPLES_PER_FRAME - n) + itd_target * n;

        for (int ch = 0; ch < 2; ch++) {
            const int32_t *coeff_cur = previous[ch];
            const int32_t *coeff_tar = target[ch];

            // Interaural time difference (channel delay)
            int32_t d = ch == 0 ? itd : -itd;
            if (d < 0) {
                d = 0;
            }
            int di = d >> 14;
            int32_t dfrac = d & 0x3fff;

            /* HRIR Convolution, exact: taps in 1/65536, samples in
             * 1/16384, each product (at most 2^61) split at bit 14 so the
             * sum of 31 cannot overflow */
            int64_t acc = 0, acc_lo = 0;
            for (int k = 0; k < HRTF_NUM_TAPS; k++) {
                int idx1 = (f->buf_pos - di - k + HRTF_BUFLEN) % HRTF_BUFLEN;
                int64_t s = (int64_t)buf[idx1] * 16384;

                // Linear interpolation for fractional part
                if (dfrac > 0) {
                    int idx2 = (idx1 - 1 + HRTF_BUFLEN) % HRTF_BUFLEN;
                    s = (int64_t)buf[idx1] * (16384 - dfrac) +
                        (int64_t)buf[idx2] * dfrac;
                }
                int32_t c = coeff_cur[k] * (HRTF_SAMPLES_PER_FRAME - n) +
                            coeff_tar[k] * n;
                int64_t p = c * s;
                acc += p >> 14;
                acc_lo += p & 0x3fff;
            }

            /* acc: the sum in 1/2^16 of a sample step, exactly floored;
             * rounded half up to a sample step */
            acc += acc_lo >> 14;
            out[n][ch] = (int32_t)((acc + (1 << 15)) >> 16);
        }

        f->buf_pos = (f->buf_pos + 1) % HRTF_BUFLEN;
    }
    memcpy(f->hrir_coeff_cur, target, sizeof(f->hrir_coeff_cur));
    f->itd_cur = itd_target;
    f->params_set = true;
}

#endif
