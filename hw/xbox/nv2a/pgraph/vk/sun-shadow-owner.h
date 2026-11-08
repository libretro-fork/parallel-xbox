/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_VK_SUN_SHADOW_OWNER_H
#define NV2A_VK_SUN_SHADOW_OWNER_H

#include <assert.h>
#include <limits.h>
#include "sun-shadow-capture.h"

/* Enhancement-only cache adapter. Entries contain native handles and owned
 * metadata, NEVER cache/LRU/mapping pointers. The cache owner must consult
 * writable() before update, destruction, pool rewind or arena reuse. A false
 * result requires keeping this version, COW, or canceling pending capture.
 * Foreign borrowed resources cannot be registered as protected versions.
 * GPU retirement is driven by the renderer's existing settled domains.
 * Nothing here frees Vulkan objects or waits for GPU work. */
enum PgraphSunVersionType {
    PGRAPH_SUN_BUFFER_VERSION,
    PGRAPH_SUN_MATERIAL_VERSION,
    PGRAPH_SUN_PIPELINE_VERSION,
    PGRAPH_SUN_IMAGE_VERSION, /* image, view and their owning allocation */
    PGRAPH_SUN_SAMPLER_VERSION,
    PGRAPH_SUN_DESCRIPTOR_POOL_VERSION, /* pool and descriptor set layout */
};
#define PGRAPH_SUN_OWNER_DEPS 4
#define PGRAPH_SUN_OWNER_REFS 5

typedef struct PgraphSunVersion {
    uint64_t generation, handle, companion;
    VkDeviceSize offset, size;
    PgraphSunResource dependencies[PGRAPH_SUN_OWNER_DEPS];
    unsigned dependency_count, cpu_refs, gpu_refs;
    enum PgraphSunVersionType type;
    bool ready, occupied;
} PgraphSunVersion;

typedef struct PgraphSunTicket {
    PgraphSunResource refs[PGRAPH_SUN_OWNER_REFS];
    unsigned count, phase;
    uint64_t generation;
    PgraphSunUse use;
} PgraphSunTicket;

typedef struct PgraphSunOwner {
    PgraphSunVersion *versions;
    PgraphSunTicket *tickets;
    size_t version_count, ticket_count;
    uint64_t lifetime, recording;
} PgraphSunOwner;

static inline PgraphSunVersion *pgraph_sun_owner_find(PgraphSunOwner *owner, PgraphSunResource key)
{
    if (!key.id || key.id > owner->version_count) return NULL;
    PgraphSunVersion *version = &owner->versions[key.id - 1];
    return version->occupied && key.generation && version->generation == key.generation ? version : NULL;
}

static inline bool pgraph_sun_owner_writable(PgraphSunOwner *owner, PgraphSunResource key)
{
    PgraphSunVersion *version = pgraph_sun_owner_find(owner, key);
    return version && !version->cpu_refs && !version->gpu_refs;
}

/* Caller storage is zero initialized on activation. Do not restart this
 * object over outstanding leases; reset/unload cancel CPU capture and drain
 * GPU work through the normal renderer retirement path first. */
static inline bool pgraph_sun_owner_start(PgraphSunOwner *owner,
    PgraphSunVersion *versions, size_t version_count, PgraphSunTicket *tickets,
    size_t ticket_count, uint64_t lifetime, uint64_t recording)
{
    if (!owner || owner->versions || owner->tickets || !versions || !tickets || !version_count || version_count > UINT32_MAX ||
        !ticket_count || ticket_count > UINT32_MAX || !lifetime || lifetime == UINT64_MAX || !recording) return false;
    for (size_t i = 0; i < version_count; i++) {
        if (versions[i].cpu_refs || versions[i].gpu_refs) return false;
    }
    for (size_t i = 0; i < ticket_count; i++) if (tickets[i].phase) return false;
    *owner = (PgraphSunOwner){versions, tickets, version_count, ticket_count, lifetime, recording};
    return true;
}

/* Publish only a cache-owned immutable version after its mutation guard has
 * succeeded. Dependencies are flat: a material pins its descriptor pool,
 * palette allocation and alpha image/sampler versions. Nested dependencies
 * are rejected, keeping acquisition bounded and rollback allocation-free. */
static inline PgraphSunResource pgraph_sun_owner_publish(PgraphSunOwner *owner,
    size_t index, PgraphSunVersion description, bool foreign_borrow)
{
    PgraphSunResource none = {0};
    if (!owner || index >= owner->version_count || foreign_borrow || !description.handle ||
        description.dependency_count > PGRAPH_SUN_OWNER_DEPS || description.cpu_refs || description.gpu_refs ||
        (unsigned)description.type > PGRAPH_SUN_DESCRIPTOR_POOL_VERSION ||
        (description.type != PGRAPH_SUN_MATERIAL_VERSION && description.dependency_count) ||
        (description.type == PGRAPH_SUN_MATERIAL_VERSION && !description.dependency_count) ||
        (description.type == PGRAPH_SUN_BUFFER_VERSION &&
            (!description.size || description.offset > UINT64_MAX - description.size)) ||
        ((description.type == PGRAPH_SUN_PIPELINE_VERSION || description.type == PGRAPH_SUN_IMAGE_VERSION ||
          description.type == PGRAPH_SUN_DESCRIPTOR_POOL_VERSION) && !description.companion)) return none;
    PgraphSunVersion *old = &owner->versions[index];
    if (old->cpu_refs || old->gpu_refs || old->generation == UINT64_MAX) return none;
    for (unsigned i = 0; i < description.dependency_count; i++) {
        PgraphSunVersion *dep = pgraph_sun_owner_find(owner, description.dependencies[i]);
        if (!dep || dep == old || dep->dependency_count) return none;
    }
    description.generation = old->generation + 1;
    description.occupied = true;
    *old = description;
    return (PgraphSunResource){index + 1, description.generation};
}

static inline bool pgraph_sun_owner_range(PgraphSunVersion *version,
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
{
    return version->type == PGRAPH_SUN_BUFFER_VERSION && version->handle == (uint64_t)(uintptr_t)buffer &&
        offset >= version->offset && offset - version->offset <= version->size &&
        size && size <= version->size - (offset - version->offset);
}

static inline PgraphSunTicket *pgraph_sun_owner_ticket(PgraphSunOwner *owner, uint64_t ticket)
{
    uint32_t slot = ticket;
    if (!slot || slot > owner->ticket_count) return NULL;
    PgraphSunTicket *entry = &owner->tickets[slot - 1];
    return entry->phase && entry->generation == ticket >> 32 ? entry : NULL;
}

static inline bool pgraph_sun_owner_acquire(void *opaque, enum PgraphSunResourceSlot slot,
    PgraphSunResource key, const PgraphVkSunPass *pass, const PgraphVkSunDraw *draw, uint64_t *ticket)
{
    PgraphSunOwner *owner = opaque;
    *ticket = 0;
    PgraphSunVersion *version = pgraph_sun_owner_find(owner, key);
    if (!version) return false;
    bool matches = false;
    switch (slot) {
    case PGRAPH_SUN_VERTEX: matches = pgraph_sun_owner_range(version, draw->vertex, draw->vertex_offset, draw->vertex_size); break;
    case PGRAPH_SUN_INDEX: matches = pgraph_sun_owner_range(version, draw->index, draw->index_offset, draw->index_size); break;
    case PGRAPH_SUN_WEIGHTS: matches = pgraph_sun_owner_range(version, draw->weights, draw->weight_offset, draw->weight_size); break;
    case PGRAPH_SUN_UV: matches = pgraph_sun_owner_range(version, draw->texcoords, draw->uv_offset, draw->uv_size); break;
    case PGRAPH_SUN_MATERIAL:
        matches = version->type == PGRAPH_SUN_MATERIAL_VERSION && version->handle == (uint64_t)(uintptr_t)draw->material;
        if (matches) {
            bool palette = false, image = false, sampler = false, pool = false;
            for (unsigned i = 0; i < version->dependency_count; i++) {
                PgraphSunVersion *dep = pgraph_sun_owner_find(owner, version->dependencies[i]);
                if (!dep) return false;
                palette |= dep->type == PGRAPH_SUN_BUFFER_VERSION && dep->size >= 256;
                image |= dep->type == PGRAPH_SUN_IMAGE_VERSION;
                sampler |= dep->type == PGRAPH_SUN_SAMPLER_VERSION;
                pool |= dep->type == PGRAPH_SUN_DESCRIPTOR_POOL_VERSION;
            }
            matches = pool && (!pass->weight_bytes || palette) && (!pass->variant.cutout || (image && sampler));
        }
        break;
    case PGRAPH_SUN_PIPELINE:
        matches = version->type == PGRAPH_SUN_PIPELINE_VERSION && version->handle == (uint64_t)(uintptr_t)pass->pipeline &&
            version->companion == (uint64_t)(uintptr_t)pass->layout;
        break;
    }
    if (!matches) return false;
    PgraphSunResource refs[PGRAPH_SUN_OWNER_REFS] = {key};
    for (unsigned i = 0; i < version->dependency_count; i++) refs[i + 1] = version->dependencies[i];
    /* Aliased dependencies count separately. Check overflow for the whole
     * transaction before changing any refcount. */
    unsigned count = version->dependency_count + 1;
    for (unsigned i = 0; i < count; i++) {
        PgraphSunVersion *dep = pgraph_sun_owner_find(owner, refs[i]);
        unsigned aliases = 0;
        for (unsigned j = 0; j < count; j++) aliases += refs[j].id == refs[i].id;
        if (!dep || dep->gpu_refs > UINT_MAX - aliases ||
            dep->cpu_refs > UINT_MAX - aliases - dep->gpu_refs) return false;
    }
    for (size_t i = 0; i < owner->ticket_count; i++) {
        PgraphSunTicket *entry = &owner->tickets[i];
        if (entry->phase || entry->generation == UINT32_MAX) continue;
        uint64_t generation = entry->generation + 1;
        *entry = (PgraphSunTicket){.count = count, .phase = 1, .generation = generation};
        memcpy(entry->refs, refs, sizeof(refs));
        for (unsigned j = 0; j < count; j++) pgraph_sun_owner_find(owner, refs[j])->cpu_refs++;
        *ticket = generation << 32 | (i + 1);
        return true;
    }
    return false;
}

static inline bool pgraph_sun_owner_valid(void *opaque, uint64_t ticket)
{
    PgraphSunOwner *owner = opaque;
    PgraphSunTicket *entry = pgraph_sun_owner_ticket(owner, ticket);
    if (!entry || entry->phase != 1) return false;
    for (unsigned i = 0; i < entry->count; i++) {
        PgraphSunVersion *version = pgraph_sun_owner_find(owner, entry->refs[i]);
        if (!version || !version->ready) return false;
    }
    return true;
}

static inline void pgraph_sun_owner_cancel(void *opaque, uint64_t ticket)
{
    PgraphSunOwner *owner = opaque;
    PgraphSunTicket *entry = pgraph_sun_owner_ticket(owner, ticket);
    assert(entry && entry->phase == 1);
    for (unsigned i = 0; i < entry->count; i++) {
        PgraphSunVersion *version = pgraph_sun_owner_find(owner, entry->refs[i]);
        assert(version && version->cpu_refs);
        version->cpu_refs--;
    }
    entry->phase = 0;
}

static inline void pgraph_sun_owner_promote(void *opaque, uint64_t ticket, const PgraphSunUse *use)
{
    PgraphSunOwner *owner = opaque;
    PgraphSunTicket *entry = pgraph_sun_owner_ticket(owner, ticket);
    assert(entry && entry->phase == 1 && use->lifetime == owner->lifetime && use->recording == owner->recording);
    for (unsigned i = 0; i < entry->count; i++) {
        PgraphSunVersion *version = pgraph_sun_owner_find(owner, entry->refs[i]);
        assert(version && version->cpu_refs && version->gpu_refs != UINT_MAX);
        version->cpu_refs--;
        version->gpu_refs++;
    }
    entry->phase = 2;
    entry->use = *use;
}

static inline void pgraph_sun_owner_retire(PgraphSunOwner *owner,
    uint32_t main, uint64_t auxiliary, uint64_t display)
{
    /* Existing main serials are interpreted within their normal half-range
     * bound; aux/display clocks are monotonic and may not be fabricated. */
    for (size_t i = 0; i < owner->ticket_count; i++) {
        PgraphSunTicket *entry = &owner->tickets[i];
        if (entry->phase != 2 || (int32_t)(main - entry->use.main_submit) < 0 ||
            auxiliary < entry->use.auxiliary_submit || display < entry->use.display_submit) continue;
        for (unsigned j = 0; j < entry->count; j++) {
            PgraphSunVersion *version = pgraph_sun_owner_find(owner, entry->refs[j]);
            assert(version && version->gpu_refs);
            version->gpu_refs--;
        }
        entry->phase = 0;
    }
}

static inline bool pgraph_sun_owner_use_valid(void *opaque, const PgraphSunUse *use)
{
    PgraphSunOwner *owner = opaque;
    return owner->versions && owner->tickets && use->lifetime == owner->lifetime &&
        use->recording == owner->recording;
}

/* New native command recording, without draining older GPU leases. Pending
 * CPU capture must submit or cancel first; GPU tickets keep their own use
 * values, including users of a resource shared by two recordings. */
static inline bool pgraph_sun_owner_recording(PgraphSunOwner *owner, uint64_t lifetime, uint64_t recording)
{
    if (!owner->versions || lifetime != owner->lifetime || !recording || recording < owner->recording) return false;
    if (recording == owner->recording) return true;
    for (size_t i = 0; i < owner->ticket_count; i++) if (owner->tickets[i].phase == 1) return false;
    owner->recording = recording;
    return true;
}

/* Active cache mutation adapter. Cancel this pending scene rather than copy
 * geometry just to keep a deferred shadow. Other batches and submitted GPU
 * users remain protected. False means preserve/COW this version using the
 * cache's normal replacement path; it NEVER authorizes overwrite or a wait. */
static inline bool pgraph_sun_owner_prepare_write(PgraphSunOwner *owner,
    PgraphSunResource key, PgraphSunCapture *capture)
{
    if (pgraph_sun_owner_writable(owner, key)) return true;
    PgraphSunVersion *version = pgraph_sun_owner_find(owner, key);
    if (!version || version->gpu_refs || !capture || capture->owner.opaque != owner ||
        capture->owner.acquire != pgraph_sun_owner_acquire) return false;
    if (!pgraph_sun_capture_cancel(capture)) return false;
    return pgraph_sun_owner_writable(owner, key);
}

static inline bool pgraph_sun_owner_stop(PgraphSunOwner *owner)
{
    for (size_t i = 0; i < owner->version_count; i++) {
        if (owner->versions[i].cpu_refs || owner->versions[i].gpu_refs) return false;
    }
    for (size_t i = 0; i < owner->ticket_count; i++) if (owner->tickets[i].phase) return false;
    *owner = (PgraphSunOwner){0};
    return true;
}

static inline PgraphSunCaptureOwner pgraph_sun_owner_callbacks(PgraphSunOwner *owner)
{
    return (PgraphSunCaptureOwner){owner, pgraph_sun_owner_acquire, pgraph_sun_owner_valid,
        pgraph_sun_owner_cancel, pgraph_sun_owner_promote, pgraph_sun_owner_use_valid};
}
#endif
