/*
 * Helper FP conversions
 *
 * Copyright (c) 2020-2025 Matt Borgerson
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


#ifndef FLOATCONV_H
#define FLOATCONV_H

#include <stdint.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

static inline float int8_to_float(int8_t x)
{
    return x / 128.0f;
}

static inline float uint8_to_float(uint8_t value)
{
    return ((int)value - 0x80) / (1.0 * 0x80);
}

static inline float int16_to_float(int16_t value)
{
    return value / (1.0 * 0x8000);
}

static inline float s6p9_to_float(int16_t value)
{
    return value / 512.0f;
}

static inline float int32_to_float(int32_t value)
{
    return value / (1.0 * 0x80000000);
}

static inline float int24_to_float(int32_t value)
{
    return int32_to_float((uint32_t)value << 8);
}

/*
 * value * 2^23 rounded half up, saturated to 24 bits. In single precision:
 * scaling by 2^23 is exact, x - trunc(x) is exact, and floor(x + 0.5) is
 * trunc(x) + (frac >= 0.5) - (frac < -0.5), with no rounding of x + 0.5.
 * The same result as the double-precision floor(x + 0.5) for every one of
 * the 2^32 inputs, NaN (0) and infinities included.
 */
static inline uint32_t float_to_24b(float value)
{
    float x = value * 8388608.0f;
    int32_t i;
    float f;

    if (x >= 8388607.0f) {
        return 0x7fffff;
    }
    if (x <= -8388608.0f) {
        return 0x800000;
    }
    if (x != x) {
        return 0;
    }
    i = (int32_t)x;
    f = x - (float)i;
    i += (f >= 0.5f) - (f < -0.5f);
    return (uint32_t)i & 0xffffff;
}

/* float_to_24b() over n values (n a multiple of 4), four at a time with
 * SSE2: clamped first (min(hi, max(lo, x)) leaves NaN as NaN, which
 * converts to 0x80000000, 0 in 24 bits), the same results */
static inline void float_to_24b_n(uint32_t *dst, const float *src, int n)
{
#if defined(__SSE2__)
    const __m128 scale = _mm_set1_ps(8388608.0f);
    const __m128 lo = _mm_set1_ps(-8388608.0f);
    const __m128 hi = _mm_set1_ps(8388607.0f);
    const __m128 half = _mm_set1_ps(0.5f);
    const __m128 mhalf = _mm_set1_ps(-0.5f);
    const __m128i mask = _mm_set1_epi32(0xffffff);

    for (int k = 0; k < n; k += 4) {
        __m128 x = _mm_mul_ps(_mm_loadu_ps(src + k), scale);
        __m128i i;
        __m128 f;

        x = _mm_min_ps(hi, _mm_max_ps(lo, x));
        i = _mm_cvttps_epi32(x);
        f = _mm_sub_ps(x, _mm_cvtepi32_ps(i));
        i = _mm_sub_epi32(i, _mm_castps_si128(_mm_cmpge_ps(f, half)));
        i = _mm_add_epi32(i, _mm_castps_si128(_mm_cmplt_ps(f, mhalf)));
        _mm_storeu_si128((__m128i *)(dst + k), _mm_and_si128(i, mask));
    }
#else
    for (int k = 0; k < n; k++) {
        dst[k] = float_to_24b(src[k]);
    }
#endif
}

#endif
