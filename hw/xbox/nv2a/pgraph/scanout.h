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

/* Native background tracking: fixed storage, no pixel snapshots. Covered
 * dirty cells remain pending so exposing them cannot show an old CPU image. */
#define PGRAPH_SCANOUT_TILE 16
#define PGRAPH_SCANOUT_MAX_DIM 8192
#define PGRAPH_SCANOUT_BG_WORDS ((PGRAPH_SCANOUT_MAX_DIM / PGRAPH_SCANOUT_TILE) * \
                                (PGRAPH_SCANOUT_MAX_DIM / PGRAPH_SCANOUT_TILE) / 64)
typedef struct PGRAPHScanoutBackground {
    unsigned cols, rows, width, height;
    uint64_t pending[PGRAPH_SCANOUT_BG_WORDS];
    uint64_t covered[PGRAPH_SCANOUT_BG_WORDS];
    bool valid;
} PGRAPHScanoutBackground;

static inline bool pgraph_scanout_background_init(PGRAPHScanoutBackground *b,
                                                 const PGRAPHScanout *scan)
{
    b->valid = scan->width <= PGRAPH_SCANOUT_MAX_DIM && scan->height <= PGRAPH_SCANOUT_MAX_DIM;
    if (!b->valid) {
        return false;
    }
    b->width = scan->width;
    b->height = scan->height;
    b->cols = (b->width + PGRAPH_SCANOUT_TILE - 1) / PGRAPH_SCANOUT_TILE;
    b->rows = (b->height + PGRAPH_SCANOUT_TILE - 1) / PGRAPH_SCANOUT_TILE;
    unsigned words = (b->cols * b->rows + 63) / 64;
    memset(b->pending, 0xFF, words * sizeof(*b->pending));
    memset(b->covered, 0, words * sizeof(*b->covered));
    return true;
}

static inline void pgraph_scanout_background_mark(PGRAPHScanoutBackground *b,
        unsigned x, unsigned y, unsigned width, unsigned height)
{
    if (!b->valid || !width || !height) {
        return;
    }
    unsigned end_x = (x + width + PGRAPH_SCANOUT_TILE - 1) / PGRAPH_SCANOUT_TILE;
    unsigned end_y = (y + height + PGRAPH_SCANOUT_TILE - 1) / PGRAPH_SCANOUT_TILE;
    for (unsigned ty = y / PGRAPH_SCANOUT_TILE; ty < end_y; ty++) {
        for (unsigned tx = x / PGRAPH_SCANOUT_TILE; tx < end_x; tx++) {
            unsigned i = ty * b->cols + tx;
            b->pending[i / 64] |= UINT64_C(1) << (i % 64);
        }
    }
}

static inline void pgraph_scanout_background_dirty(PGRAPHScanoutBackground *b,
        const PGRAPHScanout *scan, uint64_t offset, uint64_t length)
{
    if (!b->valid || !length || offset >= scan->size) {
        return;
    }
    if (length > scan->size - offset) {
        length = scan->size - offset;
    }
    uint64_t end = offset + length;
    unsigned y = offset / scan->pitch;
    while ((uint64_t)y * scan->pitch < end) {
        uint64_t row = (uint64_t)y * scan->pitch;
        unsigned lo = offset > row ? (offset - row) / scan->bpp : 0;
        unsigned hi = end - row < scan->pitch ?
                      (end - row + scan->bpp - 1) / scan->bpp : scan->width;
        if (hi > scan->width) {
            hi = scan->width;
        }
        if (lo < hi) {
            pgraph_scanout_background_mark(b, lo, y, hi - lo, 1);
        }
        y++;
    }
}

static inline void pgraph_scanout_background_cover(PGRAPHScanoutBackground *b,
        const PGRAPHScanoutKey *keys, unsigned count)
{
    if (!b->valid) {
        return;
    }
    unsigned words = (b->cols * b->rows + 63) / 64;
    memset(b->covered, 0, words * sizeof(*b->covered));
    for (unsigned n = 0; n < count; n++) {
        const PGRAPHScanoutRect *r = &keys[n].rect;
        unsigned x0 = (r->dx + PGRAPH_SCANOUT_TILE - 1) / PGRAPH_SCANOUT_TILE;
        unsigned y0 = (r->dy + PGRAPH_SCANOUT_TILE - 1) / PGRAPH_SCANOUT_TILE;
        unsigned x1 = r->dx + r->width == b->width ? b->cols :
                      (r->dx + r->width) / PGRAPH_SCANOUT_TILE;
        unsigned y1 = r->dy + r->height == b->height ? b->rows :
                      (r->dy + r->height) / PGRAPH_SCANOUT_TILE;
        for (unsigned y = y0; y < y1; y++) {
            for (unsigned x = x0; x < x1; x++) {
                unsigned i = y * b->cols + x;
                b->covered[i / 64] |= UINT64_C(1) << (i % 64);
            }
        }
    }
}

static inline bool pgraph_scanout_background_visible(const PGRAPHScanoutBackground *b,
                                                     unsigned i)
{
    return ((b->pending[i / 64] & ~b->covered[i / 64]) >> (i % 64)) & 1;
}

/* One horizontal run of dirty visible cells; the cursor permits a size
 * pass without copying metadata. Mark it clean only once its upload is queued. */
static inline bool pgraph_scanout_background_next(PGRAPHScanoutBackground *b,
        unsigned *cursor, PGRAPHScanoutRect *rect)
{
    if (!b->valid) {
        return false;
    }
    unsigned end = b->cols * b->rows, i = *cursor;
    while (i < end && !pgraph_scanout_background_visible(b, i)) {
        i++;
    }
    if (i == end) {
        *cursor = end;
        return false;
    }
    unsigned start = i, row_end = (i / b->cols + 1) * b->cols;
    while (i < row_end && pgraph_scanout_background_visible(b, i)) {
        i++;
    }
    unsigned x = (start % b->cols) * PGRAPH_SCANOUT_TILE;
    unsigned y = (start / b->cols) * PGRAPH_SCANOUT_TILE;
    unsigned right = (i - start) * PGRAPH_SCANOUT_TILE + x;
    if (right > b->width) right = b->width;
    unsigned bottom = y + PGRAPH_SCANOUT_TILE;
    if (bottom > b->height) bottom = b->height;
    *rect = (PGRAPHScanoutRect){ x, y, x, y, right - x, bottom - y };
    *cursor = i;
    return true;
}

static inline void pgraph_scanout_background_clean(PGRAPHScanoutBackground *b,
                                                  const PGRAPHScanoutRect *rect)
{
    unsigned x0 = rect->dx / PGRAPH_SCANOUT_TILE;
    unsigned x1 = (rect->dx + rect->width + PGRAPH_SCANOUT_TILE - 1) / PGRAPH_SCANOUT_TILE;
    unsigned y = rect->dy / PGRAPH_SCANOUT_TILE;
    for (unsigned x = x0; x < x1; x++) {
        unsigned i = y * b->cols + x;
        b->pending[i / 64] &= ~(UINT64_C(1) << (i % 64));
    }
}

/* Exact complement of opaque scanout regions, bounded to avoid replacing
 * one background draw with an unbounded number of small draws. */
#define PGRAPH_SCANOUT_BACKGROUND_RECTS 8
typedef struct PGRAPHScanoutRegions {
    PGRAPHScanoutRect rects[PGRAPH_SCANOUT_BACKGROUND_RECTS];
    unsigned count;
    bool fallback;
} PGRAPHScanoutRegions;

static inline void pgraph_scanout_regions_merge(PGRAPHScanoutRegions *p)
{
    bool changed;
    do {
        changed = false;
        for (unsigned i = 0; i < p->count && !changed; i++) {
            for (unsigned j = i + 1; j < p->count; j++) {
                PGRAPHScanoutRect *a = &p->rects[i], *b = &p->rects[j];
                if (a->dx == b->dx && a->width == b->width &&
                    (a->dy + a->height == b->dy || b->dy + b->height == a->dy)) {
                    if (b->dy < a->dy) a->dy = b->dy;
                    a->height += b->height;
                } else if (a->dy == b->dy && a->height == b->height &&
                    (a->dx + a->width == b->dx || b->dx + b->width == a->dx)) {
                    if (b->dx < a->dx) a->dx = b->dx;
                    a->width += b->width;
                } else {
                    continue;
                }
                a->sx = a->dx;
                a->sy = a->dy;
                p->rects[j] = p->rects[--p->count];
                changed = true;
                break;
            }
        }
    } while (changed);
}

static inline bool pgraph_scanout_regions_add(PGRAPHScanoutRegions *p,
        unsigned x, unsigned y, unsigned width, unsigned height)
{
    if (!width || !height) return true;
    if (p->count == PGRAPH_SCANOUT_BACKGROUND_RECTS) return false;
    p->rects[p->count++] = (PGRAPHScanoutRect){ x, y, x, y, width, height };
    return true;
}

static inline void pgraph_scanout_background_regions(PGRAPHScanoutRegions *out,
        const PGRAPHScanout *scan, const PGRAPHScanoutKey *keys, unsigned count)
{
    *out = (PGRAPHScanoutRegions){ 0 };
    pgraph_scanout_regions_add(out, 0, 0, scan->width, scan->height);
    for (unsigned n = 0; n < count; n++) {
        const PGRAPHScanoutRect *cover = &keys[n].rect;
        if (!cover->width || !cover->height ||
            cover->dx >= scan->width || cover->dy >= scan->height) continue;
        unsigned cw = cover->width < scan->width - cover->dx ?
                      cover->width : scan->width - cover->dx;
        unsigned ch = cover->height < scan->height - cover->dy ?
                      cover->height : scan->height - cover->dy;
        PGRAPHScanoutRegions next = { 0 };
        for (unsigned i = 0; i < out->count; i++) {
            const PGRAPHScanoutRect *a = &out->rects[i];
            unsigned x0 = a->dx > cover->dx ? a->dx : cover->dx;
            unsigned y0 = a->dy > cover->dy ? a->dy : cover->dy;
            unsigned x1 = a->dx + a->width < cover->dx + cw ?
                          a->dx + a->width : cover->dx + cw;
            unsigned y1 = a->dy + a->height < cover->dy + ch ?
                          a->dy + a->height : cover->dy + ch;
            bool ok;
            if (x0 >= x1 || y0 >= y1) {
                ok = pgraph_scanout_regions_add(&next, a->dx, a->dy, a->width, a->height);
            } else {
                /* Four disjoint pieces; corners belong to top/bottom. */
                ok = pgraph_scanout_regions_add(&next, a->dx, a->dy, a->width, y0 - a->dy) &&
                     pgraph_scanout_regions_add(&next, a->dx, y1, a->width, a->dy + a->height - y1) &&
                     pgraph_scanout_regions_add(&next, a->dx, y0, x0 - a->dx, y1 - y0) &&
                     pgraph_scanout_regions_add(&next, x1, y0, a->dx + a->width - x1, y1 - y0);
            }
            if (!ok) {
                *out = (PGRAPHScanoutRegions){ .fallback = true };
                pgraph_scanout_regions_add(out, 0, 0, scan->width, scan->height);
                return;
            }
        }
        pgraph_scanout_regions_merge(&next);
        *out = next;
    }
}

/* Small occluders do not justify multiplying CPU draw commands. Require
 * 64K covered output pixels per extra background draw; zero/one-rect plans
 * introduce no extra draw and are always useful (or the ordinary fallback). */
static inline bool pgraph_scanout_regions_worthwhile(const PGRAPHScanoutRegions *p,
        unsigned width, unsigned height, unsigned scale)
{
    if (p->count <= 1) return true;
    uint64_t visible = 0;
    for (unsigned i = 0; i < p->count; i++) {
        visible += (uint64_t)p->rects[i].width * p->rects[i].height;
    }
    uint64_t saved = (uint64_t)width * height - visible;
    return saved * scale * scale >= (uint64_t)(p->count - 1) * 65536;
}

#endif
