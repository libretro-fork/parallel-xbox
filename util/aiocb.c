/*
 * BlockAIOCB allocation
 *
 * Copyright (c) 2003-2017 Fabrice Bellard and other QEMU contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/aio.h"

/* The libretro core's DMA and TRIM requests belong to the emulation owner.
 * Retain a bounded pool without TLS or thread-exit notifiers. Content close
 * clears it after disk draining; late completing requests are freed unless
 * a subsequent submission has started using the cache again. */
#define AIOCB_CACHE_KINDS 8
#define AIOCB_CACHE_DEPTH 8

typedef struct AIOCBCache {
    const AIOCBInfo *info;
    BlockAIOCB *head;          /* chained through ->opaque */
    unsigned n;
} AIOCBCache;

static AIOCBCache aiocb_cache[AIOCB_CACHE_KINDS];
static bool aiocb_cache_enabled;

void lr_aiocb_cache_clear(void)
{
    aiocb_cache_enabled = false;
    for (int i = 0; i < AIOCB_CACHE_KINDS; i++) {
        AIOCBCache *c = &aiocb_cache[i];

        while (c->head) {
            BlockAIOCB *next = c->head->opaque;

            g_free(c->head);
            c->head = next;
        }
        c->n = 0;
        c->info = NULL;
    }
}

/* the cache of this kind, or with @make an unused one for it */
static AIOCBCache *aiocb_cache_of(const AIOCBInfo *info, bool make)
{
    for (int i = 0; i < AIOCB_CACHE_KINDS; i++) {
        AIOCBCache *c = &aiocb_cache[i];

        if (c->info == info) {
            return c;
        }
        if (!c->info) {
            if (!make) {
                return NULL;
            }
            c->info = info;
            return c;
        }
    }
    return NULL;
}

void *qemu_aio_get(const AIOCBInfo *aiocb_info, BlockDriverState *bs,
                   BlockCompletionFunc *cb, void *opaque)
{
    AIOCBCache *c = aiocb_cache_of(aiocb_info, false);
    BlockAIOCB *acb;

    aiocb_cache_enabled = true;

    if (c && c->head) {
        acb = c->head;
        c->head = acb->opaque;
        c->n--;
    } else {
        acb = g_malloc(aiocb_info->aiocb_size);
    }
    acb->aiocb_info = aiocb_info;
    acb->bs = bs;
    acb->cb = cb;
    acb->opaque = opaque;
    acb->refcnt = 1;
    return acb;
}

void qemu_aio_ref(void *p)
{
    BlockAIOCB *acb = p;
    acb->refcnt++;
}

void qemu_aio_unref(void *p)
{
    BlockAIOCB *acb = p;
    assert(acb->refcnt > 0);
    if (--acb->refcnt == 0) {
        AIOCBCache *c = aiocb_cache_enabled ?
                         aiocb_cache_of(acb->aiocb_info, true) : NULL;

        if (c && c->n < AIOCB_CACHE_DEPTH) {
            acb->opaque = c->head;
            c->head = acb;
            c->n++;
        } else {
            g_free(acb);
        }
    }
}
