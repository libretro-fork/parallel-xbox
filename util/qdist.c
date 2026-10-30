/*
 * qdist.c - QEMU helpers for handling frequency distributions of data.
 *
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/qdist.h"

#include <math.h>
#ifndef NAN
#define NAN (0.0 / 0.0)
#endif

#define QDIST_EMPTY_STR "(empty)"

void qdist_init(struct qdist *dist)
{
    dist->entries = g_new(struct qdist_entry, 1);
    dist->size = 1;
    dist->n = 0;
}

void qdist_destroy(struct qdist *dist)
{
    g_free(dist->entries);
}

static inline int qdist_cmp_double(double a, double b)
{
    if (a > b) {
        return 1;
    } else if (a < b) {
        return -1;
    }
    return 0;
}

static int qdist_cmp(const void *ap, const void *bp)
{
    const struct qdist_entry *a = ap;
    const struct qdist_entry *b = bp;

    return qdist_cmp_double(a->x, b->x);
}

void qdist_add(struct qdist *dist, double x, long count)
{
    struct qdist_entry *entry = NULL;

    if (dist->n) {
        struct qdist_entry e;

        e.x = x;
        entry = bsearch(&e, dist->entries, dist->n, sizeof(e), qdist_cmp);
    }

    if (entry) {
        entry->count += count;
        return;
    }

    if (unlikely(dist->n == dist->size)) {
        dist->size *= 2;
        dist->entries = g_renew(struct qdist_entry, dist->entries, dist->size);
    }
    dist->n++;
    entry = &dist->entries[dist->n - 1];
    entry->x = x;
    entry->count = count;
    qsort(dist->entries, dist->n, sizeof(*entry), qdist_cmp);
}

void qdist_inc(struct qdist *dist, double x)
{
    qdist_add(dist, x, 1);
}

/*
 * Unicode for block elements. See:
 *   https://en.wikipedia.org/wiki/Block_Elements
 */

#define QDIST_NR_BLOCK_CODES ARRAY_SIZE(qdist_blocks)

static inline double qdist_x(const struct qdist *dist, int index)
{
    if (dist->n == 0) {
        return NAN;
    }
    return dist->entries[index].x;
}

