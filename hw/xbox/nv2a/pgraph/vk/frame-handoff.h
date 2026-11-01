/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef NV2A_VK_FRAME_HANDOFF_H
#define NV2A_VK_FRAME_HANDOFF_H
#include <stdbool.h>
#include <retro_atomic.h>

/* One FIFO owner and one frontend consumer. Slot IDs are stored plus one;
 * zero-initialized state holds no image. Operations never spin or wait. */
typedef struct PGRAPHVkFrameHandoff {
    retro_atomic_int_t front, reading, writing;
} PGRAPHVkFrameHandoff;

static inline void pgraph_vk_frame_handoff_init(PGRAPHVkFrameHandoff *h)
{
    retro_atomic_int_init(&h->front, 0);
    retro_atomic_int_init(&h->reading, 0);
    retro_atomic_int_init(&h->writing, 0);
}

static inline bool pgraph_vk_frame_write_begin(PGRAPHVkFrameHandoff *h,
                                               unsigned slot)
{
    if (retro_atomic_load_relaxed_int(&h->writing)) {
        return false; /* no reentrant display recording */
    }
    int id = (int)slot + 1;
    retro_atomic_store_relaxed_int(&h->writing, id);
    retro_atomic_thread_fence_seq_cst();
    /* Read the temporary claim before the persistent claim. The frontend
     * installs front before clearing reading when it commits a handoff. */
    if (retro_atomic_load_acquire_int(&h->reading) == id ||
        retro_atomic_load_acquire_int(&h->front) == id) {
        retro_atomic_store_release_int(&h->writing, 0);
        return false;
    }
    return true;
}

static inline void pgraph_vk_frame_write_end(PGRAPHVkFrameHandoff *h)
{
    retro_atomic_store_release_int(&h->writing, 0);
}

static inline bool pgraph_vk_frame_read_begin(PGRAPHVkFrameHandoff *h,
                                              unsigned slot)
{
    int id = (int)slot + 1;
    retro_atomic_store_relaxed_int(&h->reading, id);
    retro_atomic_thread_fence_seq_cst();
    if (retro_atomic_load_acquire_int(&h->writing) == id) {
        retro_atomic_store_release_int(&h->reading, 0);
        return false;
    }
    return true;
}

static inline void pgraph_vk_frame_read_cancel(PGRAPHVkFrameHandoff *h)
{
    retro_atomic_store_release_int(&h->reading, 0);
}

/* Commit only after video_refresh returns, after the frontend has queued its
 * sampling commands. Keep the old front protected until that point. */
static inline void pgraph_vk_frame_read_commit(PGRAPHVkFrameHandoff *h)
{
    int id = retro_atomic_load_relaxed_int(&h->reading);
    if (id) {
        retro_atomic_store_release_int(&h->front, id);
        retro_atomic_store_release_int(&h->reading, 0);
    }
}
#endif
