/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_PGRAPH_SCANOUT_H
#define XEMU_PGRAPH_SCANOUT_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct PGRAPHScanout {
    uint64_t start, size;
    unsigned width, height, pitch, bpp;
} PGRAPHScanout;

typedef struct PGRAPHScanoutRect {
    unsigned sx, sy, dx, dy, width, height;
} PGRAPHScanoutRect;

static inline bool pgraph_scanout_init(PGRAPHScanout *s, uint64_t start,
                                       unsigned width, unsigned height,
                                       unsigned pitch, unsigned bits,
                                       uint64_t ram_size)
{
    unsigned bpp = bits == 15 ? 2 : bits / 8;
    if ((bits != 15 && bits != 16 && bits != 32) || !pitch ||
        pitch % bpp || !height || !width || start % bpp) {
        return false;
    }
    /* The TV's overscan may exceed the stored row. */
    if (width > pitch / bpp) {
        width = pitch / bpp;
    }
    uint64_t size = (uint64_t)pitch * height;
    if (!width || start > ram_size || size > ram_size - start) {
        return false;
    }
    *s = (PGRAPHScanout){ start, size, width, height, pitch, bpp };
    return true;
}

static inline bool pgraph_scanout_overlaps(const PGRAPHScanout *s,
                                           uint64_t addr, uint64_t size)
{
    return size && addr < s->start + s->size &&
           (addr >= s->start || size > s->start - addr);
}

/* A linear surface at the same byte stride maps to a clipped rectangle.
 * Anything else must be interpreted as guest bytes, not a color blit. */
static inline bool pgraph_scanout_rect(const PGRAPHScanout *s, uint64_t addr,
                                       unsigned width, unsigned height,
                                       unsigned pitch, unsigned bpp,
                                       PGRAPHScanoutRect *r)
{
    if (pitch != s->pitch || bpp != s->bpp || !width || !height ||
        addr % bpp || width > pitch / bpp || addr > INT64_MAX ||
        s->start > INT64_MAX) {
        return false;
    }
    int64_t delta = (int64_t)addr - (int64_t)s->start;
    int64_t y = delta / pitch, x = delta % pitch;
    if (x < 0) {
        x += pitch;
        y--;
    }
    x /= bpp;
    if (x + width > pitch / bpp || x >= s->width ||
        y >= s->height || y + height <= 0) {
        return false;
    }
    unsigned sy = y < 0 ? -y : 0;
    unsigned dy = y < 0 ? 0 : y;
    unsigned w = width < s->width - x ? width : s->width - x;
    unsigned h = height - sy < s->height - dy ? height - sy : s->height - dy;
    *r = (PGRAPHScanoutRect){ 0, sy, x, dy, w, h };
    return w && h;
}

/* Exact metadata comparisons, not pixel hashes: no collision can hide a change.
 * The VGA dirty client is independent of surface/texture dirty consumers. */
#define PGRAPH_SCANOUT_LAYERS 1024
typedef struct PGRAPHScanoutKey {
    uint64_t image;
    unsigned draw_time;
    PGRAPHScanoutRect rect;
} PGRAPHScanoutKey;

typedef struct PGRAPHScanoutCache {
    PGRAPHScanout scan;
    PGRAPHScanoutKey keys[PGRAPH_SCANOUT_LAYERS];
    unsigned count;
    bool valid;
    uint64_t background, geometry, content;
} PGRAPHScanoutCache;

static inline void pgraph_scanout_cache_update(PGRAPHScanoutCache *c,
        const PGRAPHScanout *scan, const PGRAPHScanoutKey *keys,
        unsigned count, bool ram_dirty)
{
    bool mode = !c->valid || c->scan.start != scan->start ||
        c->scan.size != scan->size || c->scan.width != scan->width ||
        c->scan.height != scan->height || c->scan.pitch != scan->pitch ||
        c->scan.bpp != scan->bpp;
    bool geometry = mode || c->count != count;
    bool content = geometry;
    if (!geometry) {
        for (unsigned i = 0; i < count; i++) {
            geometry |= memcmp(&c->keys[i].rect, &keys[i].rect,
                               sizeof(keys[i].rect)) != 0;
            content |= geometry || c->keys[i].image != keys[i].image ||
                       c->keys[i].draw_time != keys[i].draw_time;
        }
    }
    if (mode || ram_dirty) {
        c->background++;
        content = true;
    }
    if (geometry) {
        c->geometry++;
    }
    if (content) {
        c->content++;
        if (count) {
            memcpy(c->keys, keys, count * sizeof(*keys));
        }
    }
    c->scan = *scan;
    c->count = count;
    c->valid = true;
}

/* A raw-byte fallback only materializes incompatible views and earlier
 * writers they overlap. Independent and later compatible views stay on GPU. */
typedef struct PGRAPHScanoutSource {
    uint64_t addr, size;
    bool native;
} PGRAPHScanoutSource;

static inline void pgraph_scanout_readback_sources(
        const PGRAPHScanoutSource *sources, unsigned count, bool *readback)
{
    for (unsigned i = count; i-- > 0;) {
        readback[i] = !sources[i].native;
        for (unsigned j = i + 1; !readback[i] && j < count; j++) {
            if (!readback[j]) {
                continue;
            }
            uint64_t a = sources[i].addr, b = sources[j].addr;
            readback[i] = sources[i].size && sources[j].size &&
                (a <= b ? sources[i].size > b - a : sources[j].size > a - b);
        }
    }
}

#endif
