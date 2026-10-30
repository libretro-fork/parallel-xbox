/*
 * Carry-less multiply operations.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#include "qemu/osdep.h"
#include "crypto/clmul.h"

Int128 clmul_64_gen(uint64_t n, uint64_t m)
{
    uint64_t rl = 0, rh = 0;

    /* Bit 0 can only influence the low 64-bit result.  */
    if (n & 1) {
        rl = m;
    }

    for (int i = 1; i < 64; ++i) {
        uint64_t mask = -((n >> i) & 1);
        rl ^= (m << i) & mask;
        rh ^= (m >> (64 - i)) & mask;
    }
    return int128_make128(rl, rh);
}
