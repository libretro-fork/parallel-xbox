/*
 * Fixed pools and kept scratch buffers: no allocation mid-game
 *
 * An ObjPool holds a renderer's objects of one kind (surfaces, textures,
 * query reports) in a block allocated once: taking one and giving it back
 * never go to the allocator. A pool run dry falls back to g_malloc, and
 * tells a pointer from its block from one it did not hand out.
 *
 * A Scratch is a buffer kept for the renderer's life, grown to the largest
 * size asked of it: the first surface of a new size grows it once, then
 * nothing allocates.
 *
 * Copyright (c) 2026 Réda Chérif-Touil
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef HW_XBOX_NV2A_PGRAPH_POOL_H
#define HW_XBOX_NV2A_PGRAPH_POOL_H

#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct ObjPool {
    uint8_t *block;
    size_t size;  /* of one object */
    size_t count; /* in the block */
    void **free;  /* the objects not handed out */
    size_t nfree;
} ObjPool;

static inline void obj_pool_init(ObjPool *p, size_t size, size_t count)
{
    p->size = size;
    p->count = count;
    p->block = g_malloc(size * count);
    p->free = g_malloc_n(count, sizeof(void *));
    for (size_t i = 0; i < count; i++) {
        p->free[i] = p->block + (count - 1 - i) * size;
    }
    p->nfree = count;
}

static inline void obj_pool_finalize(ObjPool *p)
{
    g_free(p->free);
    g_free(p->block);
    memset(p, 0, sizeof(*p));
}

/* An object, zeroed */
static inline void *obj_pool_take(ObjPool *p)
{
    void *o = p->nfree ? p->free[--p->nfree] : g_malloc(p->size);
    memset(o, 0, p->size);
    return o;
}

static inline void obj_pool_give(ObjPool *p, void *o)
{
    uint8_t *b = o;
    if (b >= p->block && b < p->block + p->size * p->count) {
        p->free[p->nfree++] = o;
    } else {
        g_free(o);
    }
}

typedef struct Scratch {
    uint8_t *data;
    size_t size;
} Scratch;

/* The buffer, at least len bytes */
static inline uint8_t *scratch_reserve(Scratch *s, size_t len)
{
    if (len > s->size) {
        g_free(s->data);
        s->data = g_malloc(len);
        s->size = len;
    }
    return s->data;
}

static inline void scratch_init(Scratch *s, size_t len)
{
    s->data = g_malloc(len);
    s->size = len;
}

static inline void scratch_finalize(Scratch *s)
{
    g_free(s->data);
    memset(s, 0, sizeof(*s));
}

#endif
