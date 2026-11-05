/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "libretro-chd.h"
#include "libretro-read-view.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define RETRO_ATOMIC_REQUIRE_LOCK_FREE
#include <retro_atomic.h>
#include <retro_spsc.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#include <formats/rchd.h>

struct LRCHDRequest {
    LRReadViewOwner owner;
    retro_atomic_int_t ready, cancel;
    uint64_t offset;
    size_t length;
    int result;
    /* Serialized frontend owner only; never inspected by the worker. */
    bool used, abandoned, attached;
    uint8_t *data;
    /* Align payload independently of malloc's base alignment, without an
     * allocation or adjustment on the submission/completion path. */
    uint8_t storage[LR_CHD_MAX_READ + 63];
};

struct LRCHD {
    LRCHDSource source;
    rchd_t *decoder;
    uint64_t length;
    uint8_t *input;
    size_t input_capacity;
    LRCHDRequest *slot[LR_CHD_SLOTS];
    retro_spsc_t queue;
    retro_eventcount_t wake;
    retro_atomic_int_t stop;
    sthread_t *thread;
};

static int chd_error(int e)
{
    switch (e) {
    case RCHD_OK: return 0;
    case RCHD_ERROR_MEM: return -ENOMEM;
    case RCHD_ERROR_UNSUPPORTED: case RCHD_ERROR_NO_PARENT: return -ENOTSUP;
    case RCHD_ERROR_PARAM: return -EINVAL;
    default: return -EIO;
    }
}

static int chd_supply(LRCHD *c, const rchd_request_t *r, bool opening)
{
    const uint8_t *bytes = NULL;
    int e;
    if (r->source != RCHD_SOURCE_SELF || r->offset > c->source.size ||
        r->length > c->source.size - r->offset) return -EIO;
    if (!r->length) return 0;
    if (c->source.view)
        bytes = c->source.view(c->source.opaque, r->offset, r->length);
    if (!bytes) {
        if (r->length > c->input_capacity) {
            uint8_t *p;
            /* Resizing is restricted to lifecycle metadata parsing. */
            if (!opening || r->length > 64u * 1024u * 1024u) return -EFBIG;
            p = realloc(c->input, r->length);
            if (!p) return -ENOMEM;
            c->input = p; c->input_capacity = r->length;
        }
        e = c->source.read(c->source.opaque, r->offset, c->input, r->length);
        if (e) return e < 0 ? e : -EIO;
        bytes = c->input;
    }
    /* The read step consumed the preceding borrowed range before requesting
     * another. Our fixed worker scratch may now be refilled. Mapped bytes
     * stay borrowed through the source lifetime; no staging-ring copy. */
    return chd_error(opening ? rchd_feed(c->decoder, bytes, r->length) :
        rchd_feed_borrow(c->decoder, r->offset, r->source, bytes, r->length));
}

static int chd_decode(LRCHD *c, LRCHDRequest *r)
{
    rchd_request_t request;
    int e = rchd_read_begin(c->decoder, r->offset, r->data, r->length);
    if (e != RCHD_OK) return chd_error(e);
    for (;;) {
        if (retro_atomic_load_acquire_int(&c->stop) ||
            retro_atomic_load_acquire_int(&r->cancel)) return -ECANCELED;
        e = rchd_read_step(c->decoder, &request);
        if (e != RCHD_PENDING) return chd_error(e);
        e = chd_supply(c, &request, false);
        if (e) return e;
    }
}

static void chd_worker(void *opaque)
{
    LRCHD *c = opaque;
    for (;;) {
        LRCHDRequest *r;
        if (retro_spsc_read(&c->queue, &r, sizeof(r)) == sizeof(r)) {
            r->result = chd_decode(c, r);
            retro_atomic_store_release_int(&r->ready, 1);
            continue;
        }
        if (retro_atomic_load_acquire_int(&c->stop)) {
            /* Stop publication orders all earlier producer submissions.
             * Recheck after acquiring it before declaring the queue empty. */
            if (retro_spsc_read_avail(&c->queue)) continue;
            break;
        }
        int key = retro_eventcount_prepare_wait(&c->wake);
        if (retro_spsc_read_avail(&c->queue) ||
            retro_atomic_load_acquire_int(&c->stop)) {
            retro_eventcount_cancel_wait(&c->wake);
        } else {
            retro_eventcount_commit_wait(&c->wake, key);
        }
    }
}

static void chd_release_owner(void *owner)
{
    lr_chd_release(owner);
}

LRCHD *lr_chd_open(const LRCHDSource *source, int *error)
{
    LRCHD *c = NULL;
    rchd_request_t request;
    const rchd_info_t *info;
    int e = -EINVAL;
    bool queue = false, event = false;
    if (!error) return NULL;
    *error = -EINVAL;
    if (!source || !source->read) return NULL;
    c = calloc(1, sizeof(*c));
    if (!c) { *error = -ENOMEM; return NULL; }
    c->source = *source;
    c->decoder = rchd_new();
    if (!c->decoder) { e = -ENOMEM; goto fail; }
    while ((e = rchd_open_step(c->decoder, &request)) == RCHD_PENDING) {
        e = chd_supply(c, &request, true);
        if (e) goto fail;
    }
    if (e != RCHD_OK) { e = chd_error(e); goto fail; }
    info = rchd_info(c->decoder);
    if (!info || info->has_parent || info->unit_bytes != 2048 ||
        rchd_track_count(c->decoder) || info->hunk_bytes > LR_CHD_MAX_READ ||
        !info->logical_bytes || info->logical_bytes % 2048) {
        e = -ENOTSUP; goto fail;
    }
    c->length = info->logical_bytes;
    /* Incompressible blobs may exceed the decoded hunk by frame overhead.
     * Reserve from the parsed map, not just the output geometry. */
    size_t input = info->hunk_bytes;
    for (uint32_t hunk = 0; hunk < info->hunk_count; hunk++) {
        e = rchd_hunk_location(c->decoder, hunk, &request);
        if (e != RCHD_OK) { e = chd_error(e); goto fail; }
        if (request.length > input) input = request.length;
    }
    if (input > 2u * LR_CHD_MAX_READ) { e = -EFBIG; goto fail; }
    free(c->input);
    c->input = malloc(input);
    c->input_capacity = input;
    if (!c->input) { e = -ENOMEM; goto fail; }
    for (unsigned i = 0; i < LR_CHD_SLOTS; i++) {
        c->slot[i] = calloc(1, sizeof(*c->slot[i]));
        if (!c->slot[i]) { e = -ENOMEM; goto fail; }
        c->slot[i]->data = (uint8_t *)(((uintptr_t)c->slot[i]->storage + 63) &
                                      ~(uintptr_t)63);
        c->slot[i]->owner.release = chd_release_owner;
        c->slot[i]->attached = true;
        retro_atomic_int_init(&c->slot[i]->ready, 0);
        retro_atomic_int_init(&c->slot[i]->cancel, 0);
    }
    retro_atomic_int_init(&c->stop, 0);
    if (!(queue = retro_spsc_init(&c->queue, LR_CHD_SLOTS * sizeof(void *)))) {
        e = -ENOMEM; goto fail;
    }
    if (!(event = retro_eventcount_init_lock_free(&c->wake))) {
        e = -ENOTSUP; goto fail;
    }
    c->thread = sthread_create(chd_worker, c);
    if (!c->thread) { e = -EAGAIN; goto fail; }
    *error = 0;
    return c;
fail:
    if (event) retro_eventcount_free(&c->wake);
    if (queue) retro_spsc_free(&c->queue);
    for (unsigned i = 0; i < LR_CHD_SLOTS; i++) free(c->slot[i]);
    rchd_free(c->decoder); free(c->input); free(c);
    *error = e;
    return NULL;
}

uint64_t lr_chd_size(const LRCHD *c) { return c->length; }

int lr_chd_submit(LRCHD *c, uint64_t offset, size_t length, LRCHDRequest **out)
{
    if (!out) return -EINVAL;
    *out = NULL;
    if (!c || !length || length > LR_CHD_MAX_READ || offset > c->length ||
        length > c->length - offset) return -EINVAL;
    for (unsigned i = 0; i < LR_CHD_SLOTS; i++) {
        LRCHDRequest *r = c->slot[i];
        if (r->used && r->abandoned &&
            retro_atomic_load_acquire_int(&r->ready)) r->used = false;
        if (r->used) continue;
        if (retro_spsc_write_avail(&c->queue) < sizeof(r)) return -EAGAIN;
        r->used = true; r->abandoned = false;
        r->offset = offset; r->length = length;
        retro_atomic_store_relaxed_int(&r->ready, 0);
        retro_atomic_store_relaxed_int(&r->cancel, 0);
        size_t wrote = retro_spsc_write(&c->queue, &r, sizeof(r));
        assert(wrote == sizeof(r));
        (void)wrote;
        *out = r;
        retro_eventcount_notify(&c->wake);
        return 0;
    }
    return -EAGAIN;
}

int lr_chd_poll(LRCHDRequest *r, const void **data, size_t *len)
{
    *data = NULL; *len = 0;
    if (!retro_atomic_load_acquire_int(&r->ready)) return 0;
    if (r->result) return r->result;
    *data = r->data; *len = r->length;
    return 1;
}

void lr_chd_release(LRCHDRequest *r)
{
    if (!r) return;
    assert(r->used && !r->abandoned);
    if (!r->attached) { free(r); return; }
    if (retro_atomic_load_acquire_int(&r->ready)) r->used = false;
    else {
        r->abandoned = true;
        retro_atomic_store_release_int(&r->cancel, 1);
    }
}

void lr_chd_close(LRCHD *c)
{
    if (!c) return;
    /* Lifecycle only. A frame owner never calls this to obtain a result. */
    retro_atomic_store_release_int(&c->stop, 1);
    retro_eventcount_notify(&c->wake);
    sthread_join(c->thread);
    for (unsigned i = 0; i < LR_CHD_SLOTS; i++) {
        LRCHDRequest *r = c->slot[i];
        r->attached = false;
        if (!r->used || r->abandoned) free(r);
    }
    retro_eventcount_free(&c->wake);
    retro_spsc_free(&c->queue);
    rchd_free(c->decoder); free(c->input);
    if (c->source.close) c->source.close(c->source.opaque);
    free(c);
}
