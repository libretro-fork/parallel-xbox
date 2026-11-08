/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_SUN_SHADOW_FIFO_H
#define NV2A_SUN_SHADOW_FIFO_H

#include "sun-shadow-scene.h"
#include "sun-shadow-runtime.h"

/* CPU producer and FIFO consumer hold the existing FIFO lock. The CPU must
 * publish BEFORE its BEGIN header becomes visible to the GPU. The translator
 * maps the PARAMETER word separately: a header can end a virtual page whose
 * following page is not physically adjacent. No guest mapping is retained. */
typedef bool (*PgraphSunPhysical)(void *, uint32_t virtual_address,
                                  uint32_t *physical_address);
typedef struct PgraphSunMarker {
    uint64_t sequence, scene_epoch;
    uint32_t parameter_address;
    bool valid;
    PgraphSunScene scene;
} PgraphSunMarker;

typedef struct PgraphSunFifo {
    PgraphSunMarker *storage;
    size_t capacity, head, count;
    uint64_t lifetime, sequence, scene_epoch;
    PgraphSunScene last_scene;
    PgraphSunMarker current;
    bool failed, have_scene, prepared;
} PgraphSunFifo;

/* Existing FIFO lock held; called only by enhancement dispatch. The normal
 * thread continues to call its original, separately compiled pusher. */
typedef struct NV2AState NV2AState;
void pfifo_run_pusher_sun(NV2AState *d, PgraphSunFifo *sun);

#define PGRAPH_SUN_FIFO_MAX_MARKERS 4096
#define PGRAPH_SUN_FIFO_BEGIN_END 0x17fc

static inline bool pgraph_sun_fifo_start(bool enabled, PgraphSunFifo *fifo,
    PgraphSunMarker *storage, size_t capacity, enum PgraphSunBuild build,
    uint64_t lifetime)
{
    if (!enabled || !fifo || fifo->storage || !storage || !capacity ||
        capacity > PGRAPH_SUN_FIFO_MAX_MARKERS || !lifetime || lifetime == UINT64_MAX ||
        build != PGRAPH_SUN_DOAXVB_US_11106) return false;
    *fifo = (PgraphSunFifo){.storage = storage, .capacity = capacity, .lifetime = lifetime};
    return true;
}

static inline bool pgraph_sun_fifo_fail(PgraphSunFifo *fifo)
{
    fifo->failed = true;
    fifo->current = (PgraphSunMarker){0};
    fifo->prepared = false;
    return false;
}

/* CPU stopped at a verified pre-publication draw seam, never a periodic
 * camera poll. Invalid cameras enqueue an invalid marker too: they cannot
 * inherit the preceding draw's camera. Translation failure/overflow disables
 * this lifetime; dropping a marker and guessing a later association is unsafe. */
static inline bool pgraph_sun_fifo_publish(PgraphSunFifo *fifo,
    uint32_t cursor, PgraphSunRead read, PgraphSunPhysical physical, void *opaque)
{
    if (!fifo || !fifo->storage || fifo->failed) return false;
    uint32_t address;
    if (!read || !physical || (cursor & 3) || cursor > UINT32_MAX - 7 ||
        !physical(opaque, cursor + 4, &address) || (address & 3) ||
        address > 0x07fffffc || fifo->count == fifo->capacity ||
        fifo->sequence == UINT64_MAX || fifo->scene_epoch == UINT64_MAX) {
        return pgraph_sun_fifo_fail(fifo);
    }
    for (size_t i = 0; i < fifo->count; i++) {
        if (fifo->storage[(fifo->head + i) % fifo->capacity].parameter_address == address) {
            return pgraph_sun_fifo_fail(fifo); /* unconsumed ring location reused */
        }
    }
    PgraphSunMarker marker = {.sequence = ++fifo->sequence, .parameter_address = address};
    marker.valid = pgraph_sun_read_scene(&pgraph_sun_doaxvb_us, read, opaque, &marker.scene);
    if (!marker.valid || !fifo->have_scene ||
        memcmp(&marker.scene, &fifo->last_scene, sizeof(marker.scene))) {
        fifo->scene_epoch++;
    }
    marker.scene_epoch = fifo->scene_epoch;
    fifo->have_scene = marker.valid;
    fifo->last_scene = marker.scene;
    fifo->storage[(fifo->head + fifo->count++) % fifo->capacity] = marker;
    return true;
}

/* Enhanced pusher only, before dispatching one method run. Never compare
 * virtual push cursors with DMA-relative GET or assume GET is the header.
 * Lookahead must be disabled so another BEGIN cannot be skipped. Unmarked
 * draws are deliberately inadmissible, including draws from other code paths.
 * A retry must see the same marker: ownership advances only after dispatch. */
static inline void pgraph_sun_fifo_prepare(PgraphSunFifo *fifo,
    uint32_t parameter_address, unsigned subchannel, unsigned method, uint32_t parameter)
{
    fifo->prepared = false;
    if (method != PGRAPH_SUN_FIFO_BEGIN_END || !parameter) return;
    fifo->current = (PgraphSunMarker){0};
    if (fifo->failed || !fifo->storage || subchannel || parameter > 10) return;
    for (size_t i = 0; i < fifo->count; i++) {
        PgraphSunMarker *marker = &fifo->storage[(fifo->head + i) % fifo->capacity];
        if (marker->parameter_address != parameter_address) continue;
        if (i) { pgraph_sun_fifo_fail(fifo); return; } /* reordered/skipped marker */
        fifo->current = *marker;
        fifo->prepared = true;
        return;
    }
}

static inline void pgraph_sun_fifo_complete(PgraphSunFifo *fifo,
    unsigned method, uint32_t parameter, bool consumed)
{
    if (!consumed) {
        if (fifo->prepared) fifo->current = (PgraphSunMarker){0};
    } else {
        if (fifo->prepared) {
            fifo->head = (fifo->head + 1) % fifo->capacity;
            fifo->count--;
        }
        if (!method || (method == PGRAPH_SUN_FIFO_BEGIN_END && !parameter)) {
            fifo->current = (PgraphSunMarker){0};
        }
    }
    fifo->prepared = false;
}

static inline void pgraph_sun_fifo_stop(PgraphSunFifo *fifo)
{
    /* End storage lend. Reset/load/unload must also cancel resource capture. */
    *fifo = (PgraphSunFifo){0};
}
#endif
