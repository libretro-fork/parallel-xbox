/* Callback/drain boundary model around the extracted production IDE code.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qemu/queue.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef void BlockCompletionFunc(void *, int);
typedef struct QEMUIOVector {
    size_t size;
    struct { void *iov_base; } local_iov;
} QEMUIOVector;
typedef struct IDEBufferedRequest {
    QLIST_ENTRY(IDEBufferedRequest) list;
    QEMUIOVector qiov;
    QEMUIOVector *original_qiov;
    BlockCompletionFunc *original_cb;
    void *original_opaque;
    bool orphaned;
} IDEBufferedRequest;
typedef struct { void *aiocb; } DMA;
typedef struct { DMA *dma; } IDEBus;
typedef struct IDEState {
    QLIST_HEAD(, IDEBufferedRequest) buffered_requests;
    IDEBus *bus;
    void *blk;
} IDEState;

static IDEState *active;
static unsigned copies, frees, drains;
static void complete_all(void);
static void blk_drain(void *blk)
{
    assert(blk == active);
    drains++;
    complete_all();
    active->bus->dma->aiocb = NULL;
}
static void qemu_iovec_from_buf(QEMUIOVector *dst, size_t off,
                               const void *buf, size_t len)
{
    assert(off == 0 && len == dst->size);
    copies++;
    memcpy(dst->local_iov.iov_base, buf, len);
}
static void tracked_free(void *p) { frees++; free(p); }
#define qemu_iovec_buf(v) ((v)->local_iov.iov_base)
#define qemu_vfree tracked_free
#define g_free tracked_free
#define trace_ide_cancel_dma_sync_buffered(cb, req) ((void)0)
#define trace_ide_cancel_dma_sync_remaining() ((void)0)

/* PRODUCTION */

typedef struct {
    unsigned id, calls;
    int ret;
    uint8_t out[8];
    QEMUIOVector qiov;
} Payload;
static Payload payload[MAX_BUFFERED_REQS + 2];
static const char *mode;
static unsigned original_count;
static bool acted;
static void completion(void *opaque, int ret);

static IDEBufferedRequest *add_request(unsigned id)
{
    IDEBufferedRequest *r = calloc(1, sizeof(*r));
    assert(r);
    r->qiov.size = 8;
    r->qiov.local_iov.iov_base = malloc(8);
    assert(r->qiov.local_iov.iov_base);
    memset(r->qiov.local_iov.iov_base, 0x55, 8);
    Payload *p = &payload[id];
    p->id = id;
    memset(p->out, 0xcc, sizeof(p->out));
    p->qiov.size = 8;
    p->qiov.local_iov.iov_base = p->out;
    r->original_qiov = &p->qiov;
    r->original_cb = completion;
    r->original_opaque = p;
    QLIST_INSERT_HEAD(&active->buffered_requests, r, list);
    return r;
}

static void complete_all(void)
{
    while (!QLIST_EMPTY(&active->buffered_requests)) {
        ide_buffered_readv_cb(QLIST_FIRST(&active->buffered_requests), 0);
    }
}

static void completion(void *opaque, int ret)
{
    Payload *p = opaque;
    p->calls++;
    p->ret = ret;
    bool completed = !strcmp(mode, "complete-cancel") || !strcmp(mode, "error-cancel") ||
                     !strcmp(mode, "complete-submit");
    if (completed && p->id == 0 && p->calls == 1) {
        assert(frees == 0); /* the completing request still owns its buffer */
        for (unsigned j = 0; j < 8; j++) {
            assert(p->out[j] == (ret == 0 ? 0x55 : 0xcc));
        }
    }
    if (p->id == 0 && !acted) {
        acted = true;
        if (!strcmp(mode, "recursive") || !strcmp(mode, "recursive-unbuffered") ||
            !strcmp(mode, "complete-cancel") || !strcmp(mode, "error-cancel")) {
            ide_cancel_dma_sync(active);
        } else if (!strcmp(mode, "callback-drain")) {
            blk_drain(active);
        } else if (!strcmp(mode, "new-request") || !strcmp(mode, "complete-submit")) {
            add_request(original_count);
        }
    }
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    mode = argv[1];
    bool orphan = !strcmp(mode, "orphan");
    bool extra = !strcmp(mode, "new-request") || !strcmp(mode, "complete-submit");
    bool completed = !strcmp(mode, "complete-cancel") || !strcmp(mode, "error-cancel") ||
                     !strcmp(mode, "complete-submit");
    bool failed = !strcmp(mode, "error-cancel");
    bool submitted = !strcmp(mode, "complete-submit");
    bool drain = !strcmp(mode, "unbuffered") || !strcmp(mode, "callback-drain") ||
                 !strcmp(mode, "recursive-unbuffered");
    original_count = !strcmp(mode, "limit") ? MAX_BUFFERED_REQS + 1 : 3;
    DMA dma = {0};
    IDEBus bus = {&dma};
    IDEState state = {0};
    active = &state;
    state.bus = &bus;
    state.blk = &state;
    QLIST_INIT(&state.buffered_requests);
    for (unsigned i = original_count; i-- > 0;) {
        IDEBufferedRequest *r = add_request(i);
        r->orphaned = orphan && i == 1;
    }
    if (!strcmp(mode, "unbuffered") || !strcmp(mode, "recursive-unbuffered")) {
        dma.aiocb = &state;
    }
    if (completed) {
        ide_buffered_readv_cb(QLIST_FIRST(&state.buffered_requests), failed ? -EIO : 0);
    } else {
        ide_cancel_dma_sync(&state);
    }
    for (unsigned i = 0; i < original_count; i++) {
        if (submitted && i != 0) {
            assert(payload[i].calls == 0);
            continue;
        }
        assert(payload[i].calls == (orphan && i == 1 ? 0u : 1u));
        if (payload[i].calls) {
            int expected = completed && i == 0 ? (failed ? -EIO : 0) : -ECANCELED;
            assert(payload[i].ret == expected);
        }
        for (unsigned j = 0; j < 8; j++) {
            assert(payload[i].out[j] == (completed && i == 0 && !failed ? 0x55 : 0xcc));
        }
    }
    if (extra) {
        assert(payload[original_count].calls == 0);
    }
    complete_all();
    assert(QLIST_EMPTY(&state.buffered_requests));
    unsigned expected_copies = submitted ? original_count + 1 :
                               completed ? (failed ? 0 : 1) : (extra ? 1 : 0);
    assert(copies == expected_copies);
    assert(frees == 2 * (original_count + (extra ? 1 : 0)));
    assert(drains == (drain ? 1u : 0u));
    if (extra) {
        assert(payload[original_count].calls == 1 && payload[original_count].ret == 0);
        for (unsigned j = 0; j < 8; j++) {
            assert(payload[original_count].out[j] == 0x55);
        }
    }
    if (submitted) {
        for (unsigned i = 0; i < original_count; i++) {
            assert(payload[i].calls == 1 && payload[i].ret == 0);
            for (unsigned j = 0; j < 8; j++) {
                assert(payload[i].out[j] == 0x55);
            }
        }
    }
    printf("IDE cancellation %s: callbacks once, no stale copy, resources released: PASS\n", mode);
    return 0;
}
