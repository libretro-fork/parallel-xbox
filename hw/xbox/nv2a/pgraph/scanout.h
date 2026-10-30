/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XEMU_PGRAPH_SCANOUT_H
#define XEMU_PGRAPH_SCANOUT_H

#include <stdbool.h>
#include <stdint.h>

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

#endif
