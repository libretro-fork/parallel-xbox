/*
 * The APU's integer sample formats
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

#ifndef HW_XBOX_MCPX_APU_SAMPLE_H
#define HW_XBOX_MCPX_APU_SAMPLE_H

#include <stdint.h>

/*
 * The voice processor works in integers, as the hardware does, from the
 * decoded source to the 24-bit words of the GP's MIXBUF:
 *
 * - a sample is an int32_t in steps of a 24-bit sample: full scale is
 *   VP_SAMPLE_ONE (1 << 23), with 8 bits of headroom above it, which a
 *   multipass bin's 2^7 format headroom and filter overshoot need;
 * - a mix bin (and the VP monitor's mix) is an int64_t in steps of
 *   1 / 2^VP_MIX_SHIFT of a sample: a sample times a route gain in
 *   1/16384, normalised to a bin headroom of 7, exactly, with no rounding
 *   until the 24-bit word is taken from it.
 */
#define VP_SAMPLE_ONE (1 << 23)
#define VP_MIX_SHIFT 21

/* Source PCM to samples: 8-bit unsigned, 16, 24 (in a 32-bit container)
 * and 32-bit signed. 32-bit keeps its top 24 bits. */
static inline int32_t vp_sample_u8(uint8_t value)
{
    return ((int32_t)value - 0x80) * 65536;
}

static inline int32_t vp_sample_s16(int16_t value)
{
    return (int32_t)value * 256;
}

static inline int32_t vp_sample_s24(uint32_t value)
{
    return (int32_t)(value << 8) >> 8;
}

static inline int32_t vp_sample_s32(uint32_t value)
{
    return (int32_t)value >> 8;
}

/* A mix bin's value as the 24-bit word the hardware's bin holds: rounded
 * half up (measured), saturated */
static inline int32_t vp_mix_to_24(int64_t mix)
{
    int64_t x = (mix + ((int64_t)1 << (VP_MIX_SHIFT - 1))) >> VP_MIX_SHIFT;

    if (x > VP_SAMPLE_ONE - 1) {
        return VP_SAMPLE_ONE - 1;
    }
    if (x < -VP_SAMPLE_ONE) {
        return -VP_SAMPLE_ONE;
    }
    return (int32_t)x;
}

/*
 * A 32-bit IEEE float's bit pattern as a fixed-point integer with frac_bits
 * fractional bits, rounded half away from zero, saturated to int32_t; NaN
 * gives 0. For savestates made while the VP worked in floats.
 */
static inline int32_t vp_float_bits_to_fixed(uint32_t bits, int frac_bits)
{
    int exp = (bits >> 23) & 0xff;
    uint64_t mant = bits & 0x7fffff;
    int shift;
    uint64_t mag;

    if (exp == 0xff) {
        return mant ? 0 : (bits >> 31) ? INT32_MIN : INT32_MAX;
    }
    if (exp) {
        mant |= 0x800000;
    } else {
        exp = 1;
    }
    /* value = mant * 2^(exp - 150); fixed = value * 2^frac_bits */
    shift = exp - 150 + frac_bits;
    if (shift >= 0) {
        mag = shift > 39 ? UINT64_MAX : mant << shift;
    } else if (shift < -40) {
        mag = 0;
    } else {
        mag = (mant + ((uint64_t)1 << (-shift - 1))) >> -shift;
    }
    if (mag > INT32_MAX) {
        return (bits >> 31) ? INT32_MIN : INT32_MAX;
    }
    return (bits >> 31) ? -(int32_t)mag : (int32_t)mag;
}

#endif
