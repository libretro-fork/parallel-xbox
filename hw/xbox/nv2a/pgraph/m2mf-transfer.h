/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef HW_XBOX_NV2A_PGRAPH_M2MF_TRANSFER_H
#define HW_XBOX_NV2A_PGRAPH_M2MF_TRANSFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* CPU execution only. The owner must establish RAM authority and preserve FIFO
 * ordering before calling this helper, and publish completion after DONE.
 * No host pointers are retained: bases can be rebound after a restore. */
typedef struct PGRAPHM2MFTransfer {
    uint32_t offset_in, offset_out;
    uint32_t pitch_in, pitch_out;
    uint32_t length, rows;
    uint32_t stride_in, stride_out;
    uint32_t row, column;
} PGRAPHM2MFTransfer;

typedef enum PGRAPHM2MFResult {
    PGRAPH_M2MF_INVALID,
    PGRAPH_M2MF_YIELD,
    PGRAPH_M2MF_DONE,
} PGRAPHM2MFResult;

/* Validate the whole footprint before modifying RAM. Division-based bounds
 * avoid overflowing a sum of two potentially near-64-bit products. Zero pitch
 * is valid here (repeated rows); zero stride is rejected, never coerced. */
static inline bool pgraph_m2mf_range(size_t bytes, uint32_t offset,
                                     uint32_t pitch, uint32_t length,
                                     uint32_t rows, uint32_t stride)
{
    if (!length || !rows) {
        return true;
    }
    if (!stride || offset >= bytes) {
        return false;
    }
    size_t available = bytes - offset;
    if ((uint64_t)(length - 1) > (available - 1) / stride) {
        return false;
    }
    size_t line = (size_t)(length - 1) * stride + 1;
    return !pitch || (uint64_t)(rows - 1) <= (available - line) / pitch;
}

static inline PGRAPHM2MFResult pgraph_m2mf_begin(PGRAPHM2MFTransfer *t,
                                                size_t in_bytes,
                                                size_t out_bytes)
{
    t->row = t->column = 0;
    if (!pgraph_m2mf_range(in_bytes, t->offset_in, t->pitch_in,
                           t->length, t->rows, t->stride_in) ||
        !pgraph_m2mf_range(out_bytes, t->offset_out, t->pitch_out,
                           t->length, t->rows, t->stride_out)) {
        return PGRAPH_M2MF_INVALID;
    }
    return !t->rows || !t->length ? PGRAPH_M2MF_DONE : PGRAPH_M2MF_YIELD;
}

/* At most budget payload bytes are processed per call, even with billions of
 * repeated rows. Unit-stride rows retain memmove semantics across yields;
 * strided copies retain the reference transfer's forward element order.
 * Call only after a successful begin, with unchanged layout and valid bases.
 * This does not define guest fault/clipping policy or GPU synchronization. */
static inline PGRAPHM2MFResult pgraph_m2mf_step(PGRAPHM2MFTransfer *t,
                                               const uint8_t *in,
                                               uint8_t *out, size_t budget)
{
    if (!t->length || t->row == t->rows) {
        return PGRAPH_M2MF_DONE;
    }
    while (budget) {
        size_t src_row = t->offset_in + (size_t)t->row * t->pitch_in;
        size_t dst_row = t->offset_out + (size_t)t->row * t->pitch_out;
        size_t n = t->length - t->column;
        if (n > budget) {
            n = budget;
        }
        if (t->stride_in == 1 && t->stride_out == 1) {
            const uint8_t *src = in + src_row;
            uint8_t *dst = out + dst_row;
            uintptr_t s = (uintptr_t)src, d = (uintptr_t)dst;
            bool backward = d > s && d - s < t->length;
            size_t x = backward ? t->length - t->column - n : t->column;
            memmove(dst + x, src + x, n);
        } else {
            for (size_t i = 0; i < n; i++) {
                size_t x = t->column + i;
                out[dst_row + x * t->stride_out] =
                    in[src_row + x * t->stride_in];
            }
        }
        budget -= n;
        t->column += n;
        if (t->column == t->length) {
            t->column = 0;
            if (++t->row == t->rows) {
                return PGRAPH_M2MF_DONE;
            }
        }
    }
    return PGRAPH_M2MF_YIELD;
}
#endif
