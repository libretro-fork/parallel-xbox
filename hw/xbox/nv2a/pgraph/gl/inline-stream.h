/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef XEMU_GL_INLINE_STREAM_H
#define XEMU_GL_INLINE_STREAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Final mapped GPU storage, not a payload cache. The observed DOA3 boot/menu
 * trace peaks below 166 KiB/frame before alignment; cap each slice at 256 KiB.
 * Large/unretired/exhausted draws keep the correct existing upload path. */
#define GL_INLINE_STREAM_SLICES 3u
#define GL_INLINE_STREAM_SLICE_BYTES (256u * 1024u)
#define GL_INLINE_STREAM_BYTES (GL_INLINE_STREAM_SLICES * GL_INLINE_STREAM_SLICE_BYTES)

typedef struct GlInlineStreamCursor {
    size_t used;
    uint64_t serial[GL_INLINE_STREAM_SLICES];
    unsigned slice;
    bool disabled;
} GlInlineStreamCursor;

/* Owner-context only. CPU writes cannot overlap a submitted GPU consumer.
 * The completion serial is borrowed from the existing flip-fence retirement;
 * this helper neither polls a fence nor waits, allocates or retains a payload. */
static inline bool gl_inline_stream_reserve(GlInlineStreamCursor *cursor,
                                           size_t size, uint64_t completed,
                                           size_t *offset)
{
    if (cursor->disabled || !size || size > GL_INLINE_STREAM_SLICE_BYTES ||
        cursor->slice >= GL_INLINE_STREAM_SLICES ||
        cursor->used > GL_INLINE_STREAM_SLICE_BYTES ||
        cursor->serial[cursor->slice] > completed) {
        return false;
    }
    size_t start = (cursor->used + 15u) & ~(size_t)15u;
    if (size > GL_INLINE_STREAM_SLICE_BYTES - start) {
        return false;
    }
    *offset = cursor->slice * GL_INLINE_STREAM_SLICE_BYTES + start;
    cursor->used = start + size;
    return true;
}

static inline void gl_inline_stream_seal(GlInlineStreamCursor *cursor,
                                        uint64_t serial)
{
    if (cursor->disabled) {
        return;
    }
    if (!serial || serial <= cursor->serial[cursor->slice]) {
        /* A reset/wrapped epoch must never alias an older GPU consumer. */
        cursor->disabled = true;
        return;
    }
    if (cursor->used) {
        cursor->serial[cursor->slice] = serial;
    }
    cursor->used = 0;
    cursor->slice = (cursor->slice + 1) % GL_INLINE_STREAM_SLICES;
}
#endif
