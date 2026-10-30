/*
 * Carry-less multiply operations.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (C) 2023 Linaro, Ltd.
 */

#ifndef CRYPTO_CLMUL_H
#define CRYPTO_CLMUL_H

#include "qemu/int128.h"
#include "host/crypto/clmul.h"



/**
 * clmul_64:
 *
 * Perform a 64x64->128 carry-less multiply.
 */
Int128 clmul_64_gen(uint64_t, uint64_t);

static inline Int128 clmul_64(uint64_t a, uint64_t b)
{
    if (HAVE_CLMUL_ACCEL) {
        return clmul_64_accel(a, b);
    } else {
        return clmul_64_gen(a, b);
    }
}

#endif /* CRYPTO_CLMUL_H */
