/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_VK_SUN_SHADOW_CAPTURE_H
#define NV2A_VK_SUN_SHADOW_CAPTURE_H

#include "sun-shadow-pass.h"
#include "../sun-shadow-scene.h"
#include "../sun-shadow-runtime.h"

enum PgraphSunResourceSlot {
    PGRAPH_SUN_VERTEX,
    PGRAPH_SUN_INDEX,
    PGRAPH_SUN_WEIGHTS,
    PGRAPH_SUN_UV,
    PGRAPH_SUN_MATERIAL, /* immutable descriptor, palette, sampler/view AND content */
    PGRAPH_SUN_PIPELINE, /* pipeline and its layout */
};

typedef struct PgraphSunResource {
    uint64_t id, generation; /* owner-issued identities, never LRU-node pointers */
} PgraphSunResource;

typedef struct PgraphSunDrawResources {
    PgraphSunResource vertex, index, weights, uv, material, pipeline;
} PgraphSunDrawResources;

/* Values from the owner's EXISTING submission domains. This layer neither
 * invents submission serials nor waits on the queue/device. A recording token
 * identifies an open recording and must not be reused after cancel/reset. */
typedef struct PgraphSunUse {
    uint64_t lifetime, scene_epoch, recording;
    uint32_t main_submit;
    uint64_t auxiliary_submit, display_submit;
} PgraphSunUse;

typedef struct PgraphSunCaptureOwner {
    void *opaque; /* native owner, outlives pending batch and promoted GPU leases */
    /* Acquire protects against BOTH destruction and in-place content writes.
     * It validates the key against the packet's actual handles/ranges and
     * reserves retention-budget and promotion capacity. Acquire/valid/cancel
     * must not record GPU commands. Decline unsupported borrowed views rather
     * than assuming a live VkImage is immutable. No packet pointer retained.
     * If a failure returns a partial ticket, cancel will release it. */
    bool (*acquire)(void *, enum PgraphSunResourceSlot, PgraphSunResource,
                    const PgraphVkSunPass *, const PgraphVkSunDraw *, uint64_t *ticket);
    /* Also verify that producer dependencies can execute before replay.
     * A protected live image with a future unresolved producer is not ready. */
    bool (*valid)(void *, uint64_t ticket);
    void (*cancel)(void *, uint64_t ticket); /* release CPU ownership only */
    /* Cannot fail/allocate/wait: acquisition reserved this handoff. Replace
     * CPU ownership with retirement protection in ALL applicable domains,
     * preserving other main/display/aux users of the same resource version.
     * Copy the use value; its pointer is borrowed only during this call. */
    void (*promote)(void *, uint64_t ticket, const PgraphSunUse *use);
    /* Optional owner-domain check, before ANY replay commands. */
    bool (*use_valid)(void *, const PgraphSunUse *use);
} PgraphSunCaptureOwner;

#define PGRAPH_SUN_CAPTURE_LEASES 6
#define PGRAPH_SUN_CAPTURE_MAX_PACKETS 4096

typedef struct PgraphSunCapturePacket {
    uint64_t sequence;
    PgraphVkSunPass pass;
    PgraphVkSunDraw draw;
    uint64_t tickets[PGRAPH_SUN_CAPTURE_LEASES];
    unsigned ticket_count;
} PgraphSunCapturePacket;

enum PgraphSunCapturePhase {
    PGRAPH_SUN_CAPTURE_IDLE,
    PGRAPH_SUN_CAPTURE_PENDING,
    PGRAPH_SUN_CAPTURE_ACQUIRING,
    PGRAPH_SUN_CAPTURE_PREFLIGHT,
    PGRAPH_SUN_CAPTURE_RECORDING,
    PGRAPH_SUN_CAPTURE_CANCELING,
};

/* GPU-owner only. Storage is allocated by enhancement activation, never by
 * normal drawing. A batch has one owned scene and one open-recording token.
 * Guest/renderer dependency boundaries must submit or cancel it before reuse.
 * This is not permission to read the CPU's latest camera for a queued draw:
 * the caller must supply scene constants correlated to this command stream. */
typedef struct PgraphSunCapture {
    PgraphSunCaptureOwner owner;
    PgraphSunCapturePacket *packets;
    size_t capacity, count;
    uint64_t lifetime, scene_epoch, recording, last_sequence;
    PgraphSunScene scene;
    enum PgraphSunCapturePhase phase;
    bool failed;
} PgraphSunCapture;

static inline void pgraph_sun_capture_close(PgraphSunCapture *capture)
{
    /* End the storage/context lend, retaining owned scalar scene identity. */
    capture->owner = (PgraphSunCaptureOwner){0};
    capture->packets = NULL;
    capture->capacity = capture->count = 0;
    capture->phase = PGRAPH_SUN_CAPTURE_IDLE;
}

static inline bool pgraph_sun_capture_cancel(PgraphSunCapture *capture)
{
    if (capture->phase != PGRAPH_SUN_CAPTURE_IDLE &&
        capture->phase != PGRAPH_SUN_CAPTURE_PENDING) return false;
    if (capture->count) capture->failed = true;
    capture->phase = PGRAPH_SUN_CAPTURE_CANCELING;
    for (size_t i = capture->count; i > 0; i--) {
        PgraphSunCapturePacket *packet = &capture->packets[i - 1];
        while (packet->ticket_count) {
            capture->owner.cancel(capture->owner.opaque, packet->tickets[--packet->ticket_count]);
        }
    }
    pgraph_sun_capture_close(capture);
    return true;
}

static inline bool pgraph_sun_capture_fail(PgraphSunCapture *capture)
{
    capture->failed = true;
    pgraph_sun_capture_cancel(capture);
    return false;
}

static inline bool pgraph_sun_capture_begin(bool enabled, PgraphSunCapture *capture,
    PgraphSunCapturePacket *storage, size_t capacity, PgraphSunCaptureOwner owner,
    enum PgraphSunBuild build, uint64_t lifetime, uint64_t scene_epoch,
    uint64_t recording, const PgraphSunScene *scene)
{
    if (!enabled || !capture || capture->phase != PGRAPH_SUN_CAPTURE_IDLE || !storage ||
        !capacity || capacity > PGRAPH_SUN_CAPTURE_MAX_PACKETS || !recording || !scene_epoch || !scene ||
        build != PGRAPH_SUN_DOAXVB_US_11106 || !lifetime || lifetime == UINT64_MAX ||
        !owner.acquire || !owner.valid || !owner.cancel || !owner.promote) return false;
    bool same_scene = capture->lifetime == lifetime && capture->scene_epoch == scene_epoch;
    if (same_scene && capture->failed) return false;
    if (same_scene && memcmp(&capture->scene, scene, sizeof(*scene))) {
        return pgraph_sun_capture_fail(capture);
    }
    float fit[16];
    for (unsigned i = 0; i < 16; i++) {
        if (!isfinite(scene->view[i]) || !isfinite(scene->projection[i])) return false;
    }
    if (!pgraph_sun_fit(scene, 4096, 64, 20, 120, fit)) return false;
    uint64_t sequence = capture->lifetime == lifetime && capture->recording == recording ?
                        capture->last_sequence : 0;
    *capture = (PgraphSunCapture){.owner = owner, .packets = storage, .capacity = capacity,
        .lifetime = lifetime, .scene_epoch = scene_epoch, .recording = recording, .last_sequence = sequence,
        .scene = *scene, .phase = PGRAPH_SUN_CAPTURE_PENDING};
    return true;
}

static inline bool pgraph_sun_capture_append(PgraphSunCapture *capture,
    uint64_t sequence, const PgraphVkSunPass *pass, const PgraphVkSunDraw *draw,
    const PgraphSunDrawResources *resources)
{
    if (!capture || capture->phase != PGRAPH_SUN_CAPTURE_PENDING) return false;
    if (!sequence || sequence <= capture->last_sequence) return pgraph_sun_capture_fail(capture);
    /* Preserve the stream high-water mark even when this transaction is
     * canceled. A later batch in the same recording must not replay an old
     * draw using newer inputs. A fresh recording/lifetime starts a new order. */
    capture->last_sequence = sequence;
    if (capture->count == capture->capacity || !resources || !pgraph_vk_sun_draw_valid(pass, draw)) {
        return pgraph_sun_capture_fail(capture);
    }
    const PgraphSunResource keys[] = {resources->vertex, resources->index, resources->weights,
        resources->uv, resources->material, resources->pipeline};
    const bool required[] = {true, draw->index != VK_NULL_HANDLE, pass->weight_bytes != 0,
        pass->variant.cutout, pass->weight_bytes || pass->variant.cutout, true};
    PgraphSunCapturePacket packet = {.sequence = sequence, .pass = *pass, .draw = *draw};
    capture->phase = PGRAPH_SUN_CAPTURE_ACQUIRING;
    for (unsigned i = 0; i < PGRAPH_SUN_CAPTURE_LEASES; i++) {
        if (!required[i]) continue;
        uint64_t ticket = 0;
        bool acquired = keys[i].id && keys[i].generation && capture->owner.acquire(
            capture->owner.opaque, (enum PgraphSunResourceSlot)i, keys[i], &packet.pass, &packet.draw, &ticket);
        if (ticket) packet.tickets[packet.ticket_count++] = ticket;
        if (!acquired || !ticket) {
            while (packet.ticket_count) capture->owner.cancel(capture->owner.opaque,
                packet.tickets[--packet.ticket_count]);
            capture->phase = PGRAPH_SUN_CAPTURE_PENDING;
            return pgraph_sun_capture_fail(capture);
        }
    }
    capture->packets[capture->count++] = packet;
    capture->phase = PGRAPH_SUN_CAPTURE_PENDING;
    return true;
}

/* Preflight the ENTIRE batch before referencing any input in GPU commands.
 * Once recording starts, even failure promotes all leases: an earlier callback
 * may already have emitted commands. The caller retains the original game
 * shadow path unless the whole batch succeeds. No callback may retain packet
 * or scene pointers; command constants belong to its command recording. */
static inline bool pgraph_sun_capture_submit(PgraphSunCapture *capture, PgraphSunUse use,
    bool (*record)(void *, const PgraphSunScene *, const PgraphSunCapturePacket *), void *opaque)
{
    if (!capture || capture->phase != PGRAPH_SUN_CAPTURE_PENDING) return false;
    if (!record || !capture->count || !use.recording || use.recording != capture->recording ||
        use.lifetime != capture->lifetime || use.scene_epoch != capture->scene_epoch) {
        return pgraph_sun_capture_fail(capture);
    }
    capture->phase = PGRAPH_SUN_CAPTURE_PREFLIGHT;
    if (capture->owner.use_valid && !capture->owner.use_valid(capture->owner.opaque, &use)) {
        capture->phase = PGRAPH_SUN_CAPTURE_PENDING;
        return pgraph_sun_capture_fail(capture);
    }
    for (size_t i = 0; i < capture->count; i++) {
        const PgraphSunCapturePacket *packet = &capture->packets[i];
        for (unsigned j = 0; j < packet->ticket_count; j++) {
            if (!capture->owner.valid(capture->owner.opaque, packet->tickets[j])) {
                capture->phase = PGRAPH_SUN_CAPTURE_PENDING;
                return pgraph_sun_capture_fail(capture);
            }
        }
    }
    capture->phase = PGRAPH_SUN_CAPTURE_RECORDING;
    bool success = true;
    for (size_t i = 0; i < capture->count; i++) {
        if (!record(opaque, &capture->scene, &capture->packets[i])) {
            success = false;
            break;
        }
    }
    for (size_t i = 0; i < capture->count; i++) {
        PgraphSunCapturePacket *packet = &capture->packets[i];
        while (packet->ticket_count) {
            capture->owner.promote(capture->owner.opaque, packet->tickets[--packet->ticket_count], &use);
        }
    }
    pgraph_sun_capture_close(capture);
    capture->failed = !success;
    return success;
}

#endif
