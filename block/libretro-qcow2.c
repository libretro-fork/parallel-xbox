/*
 * A qcow2 image read and written in place (block/libretro-qcow2.h).
 *
 * The format, as docs/interop/qcow2.rst has it: a big-endian header, an L1
 * table of L2 tables, each L2 entry naming the cluster of the file that
 * holds a cluster of the disk (bit 63, COPIED: only this image's current
 * state uses it), compressed (bit 62: a raw deflate stream's offset and
 * length in sectors), all zeroes (bit 0, version 3) or nowhere (zeroes too,
 * without a backing file); and a refcount table of refcount blocks, how
 * many users each cluster of the file has (the L1 and L2 tables and the
 * internal snapshots' copies of them included).
 *
 * Every L2 table, the refcount table and its blocks are read into memory
 * at open: for the Xbox's 8 GB disk in 64 KB clusters, 1 MB and a quarter
 * of that. A read or a write of clusters that are there is then one
 * positional system call (into the guest's own memory, for a run of
 * clusters that lie one after another in the file), as on a raw image.
 *
 * What a write changes in the tables is written at once, in an order a
 * crash can only leave leaked clusters after (QEMU repairs those, and
 * reports them as "leaked", not as corruption): the data in its new
 * cluster, the cluster's refcount, then the L2 entry that names it (then
 * the L1 entry, for a new L2 table). A cluster a snapshot still uses (no
 * COPIED) is copied before it is written, an L2 table likewise; a
 * compressed one is written uncompressed into a cluster of its own. The
 * clusters given back (a range zeroed, a cluster rewritten) get their
 * refcount lowered, and the space of one nothing uses any more is freed
 * from the file (a hole); free clusters are used again before the file
 * grows. An image marked dirty (lazy refcounts, its counts possibly short)
 * only grows, and keeps the mark, for QEMU to rebuild the counts. Not
 * written: images whose refcounts are not 16 bits. Not opened: corrupt,
 * encrypted, with a backing or an external data file, zstd clusters or
 * subclusters (none of which xemu makes).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "libretro-qcow2.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <encodings/deflate.h>

#define QCOW2_MAGIC             0x514649fbu
#define OFFSET_MASK             0x00fffffffffffe00ull
#define F_COPIED                (1ull << 63)
#define F_COMPRESSED            (1ull << 62)
#define F_ZERO                  1ull
#define INCOMPAT_DIRTY          (1ull << 0)
#define INCOMPAT_CORRUPT        (1ull << 1)
#define INCOMPAT_DATAFILE       (1ull << 2)
#define INCOMPAT_COMPRESS       (1ull << 3)
#define INCOMPAT_EXTL2          (1ull << 4)
#define HDR_AUTOCLEAR           88

/* pieces of a request handed to one system call at a time */
#define IOV_MAX_PIECES          64

struct LRQcow2 {
    LRFile *f;
    bool writable;
    bool dirty_image;           /* marked dirty: refcounts not trusted */
    bool meta_dirty;            /* tables written since the last flush */
    bool autoclear_done;
    uint32_t version, cluster_bits;
    uint64_t cs;                /* cluster size */
    uint64_t size;              /* the disk's */
    uint64_t autoclear;

    uint32_t l1_size;
    uint64_t l1_offset;
    uint64_t *l1;
    size_t l2e;                 /* entries in an L2 table */
    uint64_t *l2;               /* every L1 entry's table, zero when none */

    uint64_t rt_offset;
    size_t rt_entries;
    uint64_t *rt;
    size_t rbe;                 /* entries in a refcount block */
    size_t rb_count;            /* blocks there is room for in rb */
    uint16_t *rb;

    uint64_t host_end;          /* in clusters: past it, nothing */
    uint64_t free_from;         /* in clusters: none free before it */

    uint64_t csize_shift, csize_mask, coff_mask;
    void *inflate;
    uint8_t *cbuf;              /* a compressed cluster as it is stored */
    uint8_t *cluster;           /* a cluster being made */
    uint8_t *table;             /* a table on its way to the file */
    uint64_t cached;            /* the guest cluster in dcache, or ~0 */
    uint8_t *dcache;            /* the last compressed cluster read */
};

static uint64_t be64(const uint8_t *p)
{
    return (uint64_t)p[0] << 56 | (uint64_t)p[1] << 48 |
           (uint64_t)p[2] << 40 | (uint64_t)p[3] << 32 |
           (uint64_t)p[4] << 24 | (uint64_t)p[5] << 16 |
           (uint64_t)p[6] << 8 | p[7];
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static void put_be64(uint8_t *p, uint64_t v)
{
    for (int i = 7; i >= 0; i--) {
        p[i] = (uint8_t)v;
        v >>= 8;
    }
}

/* Tiny ranges need only one write, without a size query or hole-punch call. */
static const uint8_t zeroes[4096];

/* ---- requests in pieces ------------------------------------------------- */

/* where a request's I/O vector is: piece i, byte off of it */
typedef struct {
    const LRFileIOV *iov;
    int n, i;
    size_t off;
} Cursor;

/* Up to max contiguous runs of the next len bytes; no staging copy. */
static int cursor_take(Cursor *c, size_t len, LRFileIOV *out, int max,
                       size_t *took)
{
    int k = 0;

    *took = 0;
    while (len && c->i < c->n) {
        size_t here = c->iov[c->i].len - c->off;
        if (!here) {
            c->i++;
            c->off = 0;
            continue;
        }
        size_t n = here < len ? here : len;
        uint8_t *base = (uint8_t *)c->iov[c->i].base + c->off;
        if (k && out[k - 1].len <= UINTPTR_MAX - (uintptr_t)out[k - 1].base &&
            (uintptr_t)base == (uintptr_t)out[k - 1].base + out[k - 1].len &&
            n <= SIZE_MAX - out[k - 1].len) {
            out[k - 1].len += n;
        } else {
            if (k == max) {
                break;
            }
            out[k].base = base;
            out[k++].len = n;
        }
        *took += n;
        len -= n;
        c->off += n;
        if (c->off == c->iov[c->i].len) {
            c->i++;
            c->off = 0;
        }
    }
    return k;
}

static void cursor_copy_out(Cursor *c, const uint8_t *src, size_t len)
{
    while (len) {
        LRFileIOV p;
        size_t took;
        cursor_take(c, len, &p, 1, &took);
        memcpy(p.base, src, took);
        src += took;
        len -= took;
    }
}

static void cursor_copy_in(Cursor *c, uint8_t *dst, size_t len)
{
    while (len) {
        LRFileIOV p;
        size_t took;
        cursor_take(c, len, &p, 1, &took);
        memcpy(dst, p.base, took);
        dst += took;
        len -= took;
    }
}

static void cursor_zero(Cursor *c, size_t len)
{
    while (len) {
        LRFileIOV p;
        size_t took;
        cursor_take(c, len, &p, 1, &took);
        memset(p.base, 0, took);
        len -= took;
    }
}

/* len bytes of the request at host offset host, to or from the file */
static int cursor_io(LRQcow2 *q, Cursor *c, uint64_t host, size_t len,
                     bool write)
{
    LRFileIOV pieces[IOV_MAX_PIECES];

    while (len) {
        size_t took;
        int k = cursor_take(c, len, pieces, IOV_MAX_PIECES, &took);
        int ret = write ? lr_file_pwritev(q->f, (int64_t)host, pieces, k)
                        : lr_file_preadv(q->f, (int64_t)host, pieces, k);
        if (ret < 0) {
            return ret;
        }
        host += took;
        len -= took;
    }
    return 0;
}

/* ---- the tables --------------------------------------------------------- */

static uint64_t *l2_entry(LRQcow2 *q, uint64_t guest_cluster)
{
    return &q->l2[guest_cluster];
}

static uint64_t rc_get(LRQcow2 *q, uint64_t cl)
{
    size_t b = (size_t)(cl / q->rbe);

    if (b >= q->rb_count || !(q->rt[b] & OFFSET_MASK)) {
        return 0;
    }
    return q->rb[b * q->rbe + cl % q->rbe];
}

static int write_be64(LRQcow2 *q, uint64_t at, uint64_t v)
{
    uint8_t b[8];

    put_be64(b, v);
    q->meta_dirty = true;
    return lr_file_pwrite(q->f, (int64_t)at, b, 8);
}

static int ensure_block(LRQcow2 *q, size_t b);

/* a cluster of the file for something new, its refcount 1 */
static int alloc_cluster(LRQcow2 *q, uint64_t *out)
{
    uint64_t cl = q->host_end;
    int ret;

    if (!q->dirty_image) {
        for (uint64_t c = q->free_from; c < q->host_end; c++) {
            if (rc_get(q, c) == 0) {
                cl = c;
                break;
            }
        }
    }
    if (cl == q->host_end) {
        q->host_end++;
    }
    q->free_from = cl + 1;
    ret = ensure_block(q, (size_t)(cl / q->rbe));
    if (ret < 0) {
        return ret;
    }
    q->rb[(cl / q->rbe) * q->rbe + cl % q->rbe] = 1;
    ret = lr_file_pwrite(q->f, (int64_t)((q->rt[cl / q->rbe] & OFFSET_MASK) +
                                         (cl % q->rbe) * 2),
                         (const uint8_t[]){ 0, 1 }, 2);
    q->meta_dirty = true;
    *out = cl << q->cluster_bits;
    return ret;
}

/* The refcount block for clusters b * rbe on, made when there is none: it
 * goes at the file's end, and counts itself (or the block for where it
 * lies does) */
static int ensure_block(LRQcow2 *q, size_t b)
{
    uint64_t cl;
    uint8_t *t = q->table;
    int ret;

    if (b < q->rb_count && (q->rt[b] & OFFSET_MASK)) {
        return 0;
    }
    if (b >= q->rb_count || b >= q->rt_entries) {
        return -ENOSPC;     /* the refcount table would have to grow */
    }
    cl = q->host_end++;
    if (cl / q->rbe != b) {
        /* it lies in another block's range: that one first */
        ret = ensure_block(q, (size_t)(cl / q->rbe));
        if (ret < 0) {
            return ret;
        }
        q->rb[(cl / q->rbe) * q->rbe + cl % q->rbe] = 1;
        ret = lr_file_pwrite(q->f, (int64_t)((q->rt[cl / q->rbe] &
                                              OFFSET_MASK) +
                                             (cl % q->rbe) * 2),
                             (const uint8_t[]){ 0, 1 }, 2);
        if (ret < 0) {
            return ret;
        }
    } else {
        q->rb[b * q->rbe + cl % q->rbe] = 1;
    }
    /* the block, then the table's entry for it */
    memset(t, 0, (size_t)q->cs);
    for (size_t i = 0; i < q->rbe; i++) {
        uint16_t v = q->rb[b * q->rbe + i];
        t[i * 2] = (uint8_t)(v >> 8);
        t[i * 2 + 1] = (uint8_t)v;
    }
    ret = lr_file_pwrite(q->f, (int64_t)(cl << q->cluster_bits), t,
                         (size_t)q->cs);
    if (ret < 0) {
        return ret;
    }
    q->rt[b] = cl << q->cluster_bits;
    return write_be64(q, q->rt_offset + b * 8, q->rt[b]);
}

/* one user fewer for cluster cl; nothing using it: its space freed */
static int rc_dec(LRQcow2 *q, uint64_t cl)
{
    size_t b = (size_t)(cl / q->rbe);
    uint16_t *v;
    uint8_t be[2];
    int ret;

    /* a dirty image's refcounts may be short of its users: nothing is
     * given back, so nothing in use is freed (QEMU repairs the counts) */
    if (q->dirty_image || b >= q->rb_count || !(q->rt[b] & OFFSET_MASK)) {
        return 0;
    }
    v = &q->rb[b * q->rbe + cl % q->rbe];
    if (*v == 0) {
        return 0;           /* already free: a leak's opposite, left be */
    }
    (*v)--;
    be[0] = (uint8_t)(*v >> 8);
    be[1] = (uint8_t)*v;
    ret = lr_file_pwrite(q->f, (int64_t)((q->rt[b] & OFFSET_MASK) +
                                         (cl % q->rbe) * 2), be, 2);
    q->meta_dirty = true;
    if (ret == 0 && *v == 0) {
        lr_file_zero(q->f, (int64_t)(cl << q->cluster_bits), (int64_t)q->cs);
        if (cl < q->free_from) {
            q->free_from = cl;
        }
    }
    return ret;
}

/* what an L2 entry held, given back */
static int free_entry(LRQcow2 *q, uint64_t e)
{
    if (e & F_COMPRESSED) {
        /* every cluster the stream's sectors touch, as qcow2 counts them */
        uint64_t off = e & q->coff_mask & ~511ull;
        uint64_t len = (((e >> q->csize_shift) & q->csize_mask) + 1) * 512;
        for (uint64_t c = off >> q->cluster_bits;
             c <= (off + len - 1) >> q->cluster_bits; c++) {
            int ret = rc_dec(q, c);
            if (ret < 0) {
                return ret;
            }
        }
        return 0;
    }
    if (e & OFFSET_MASK) {
        return rc_dec(q, (e & OFFSET_MASK) >> q->cluster_bits);
    }
    return 0;
}

/* L2 table i ready to be written: made when there is none, copied when a
 * snapshot shares it */
static int l2_writable(LRQcow2 *q, uint32_t i)
{
    uint64_t old = q->l1[i] & OFFSET_MASK, host;
    uint64_t *tbl = &q->l2[(size_t)i * q->l2e];
    int ret;

    if (old && (q->l1[i] & F_COPIED)) {
        return 0;
    }
    ret = alloc_cluster(q, &host);
    if (ret < 0) {
        return ret;
    }
    for (size_t j = 0; j < q->l2e; j++) {
        put_be64(q->table + j * 8, tbl[j]);
    }
    ret = lr_file_pwrite(q->f, (int64_t)host, q->table, (size_t)q->cs);
    if (ret < 0) {
        return ret;
    }
    q->l1[i] = host | F_COPIED;
    ret = write_be64(q, q->l1_offset + (uint64_t)i * 8, q->l1[i]);
    if (ret < 0) {
        return ret;
    }
    return old ? rc_dec(q, old >> q->cluster_bits) : 0;
}

static int set_l2(LRQcow2 *q, uint64_t gc, uint64_t e)
{
    uint32_t i = (uint32_t)(gc / q->l2e);
    int ret = write_be64(q, (q->l1[i] & OFFSET_MASK) + (gc % q->l2e) * 8, e);

    if (ret == 0) {
        *l2_entry(q, gc) = e;
    }
    return ret;
}

/* compressed cluster gc into dcache */
static int read_compressed(LRQcow2 *q, uint64_t gc, uint64_t e)
{
    uint64_t coff = e & q->coff_mask;
    size_t csize = (size_t)((((e >> q->csize_shift) & q->csize_mask) + 1) *
                            512 - (coff & 511));
    size_t wrote = 0;
    int r;

    if (q->cached == gc) {
        return 0;
    }
    r = lr_file_pread(q->f, (int64_t)coff, q->cbuf, csize);
    if (r < 0) {
        return r;
    }
    rinflate_reset(q->inflate, -12);
    rinflate_set_in(q->inflate, q->cbuf, csize);
    rinflate_set_out(q->inflate, q->dcache, (size_t)q->cs);
    r = rinflate_process(q->inflate, NULL, &wrote);
    if (r == RDEFLATE_PROCESS_ERROR || wrote != q->cs) {
        q->cached = ~0ull;
        return -EIO;
    }
    q->cached = gc;
    return 0;
}

/* whether entry e names a cluster written in place: its own, data */
static bool in_place(LRQcow2 *q, uint64_t e)
{
    return (e & F_COPIED) && !(e & F_COMPRESSED) && (e & OFFSET_MASK) &&
           !(q->version == 3 && (e & F_ZERO));
}

static bool reads_zero(LRQcow2 *q, uint64_t e)
{
    return !(e & F_COMPRESSED) &&
           (!(e & OFFSET_MASK) || (q->version == 3 && (e & F_ZERO)));
}

/* ---- open and close ----------------------------------------------------- */

LRQcow2 *lr_qcow2_open(LRFile *f, bool writable, char *err, size_t errlen)
{
    uint8_t hdr[112];
    LRQcow2 *q = calloc(1, sizeof(*q));
    uint64_t incompat = 0, file_size, cover, l1_bytes;
    uint32_t refcount_order = 4, rt_clusters;
    int64_t fs;


#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); goto fail; } \
    while (0)

    if (!q) {
        snprintf(err, errlen, "out of memory");
        return NULL;
    }
    q->f = f;
    q->writable = writable;
    q->cached = ~0ull;
    if (lr_file_pread(f, 0, hdr, sizeof(hdr)) < 0 || be32(hdr) != QCOW2_MAGIC) {
        FAIL("not a qcow2 image");
    }
    q->version = be32(hdr + 4);
    q->cluster_bits = be32(hdr + 20);
    q->size = be64(hdr + 24);
    q->l1_size = be32(hdr + 36);
    q->l1_offset = be64(hdr + 40);
    q->rt_offset = be64(hdr + 48);
    rt_clusters = be32(hdr + 56);
    if (q->version != 2 && q->version != 3) {
        FAIL("qcow2 version %u", q->version);
    }
    if (be64(hdr + 8)) {
        FAIL("it has a backing file");
    }
    if (be32(hdr + 32)) {
        FAIL("it is encrypted");
    }
    if (q->cluster_bits < 9 || q->cluster_bits > 21) {
        FAIL("clusters of 2^%u bytes", q->cluster_bits);
    }
    if (q->version == 3) {
        incompat = be64(hdr + 72);
        q->autoclear = be64(hdr + HDR_AUTOCLEAR);
        refcount_order = be32(hdr + 96);
        if (incompat & INCOMPAT_CORRUPT) {
            FAIL("qemu-img marked it corrupt");
        }
        if (incompat & INCOMPAT_DATAFILE) {
            FAIL("its data is in another file");
        }
        if (incompat & INCOMPAT_EXTL2) {
            FAIL("it has subclusters");
        }
        if ((incompat & INCOMPAT_COMPRESS) && be32(hdr + 100) >= 105 &&
            hdr[104] != 0) {
            FAIL("its clusters are compressed with zstd");
        }
        if (incompat & ~(INCOMPAT_DIRTY | INCOMPAT_COMPRESS)) {
            FAIL("features this core does not know (0x%llx)",
                 (unsigned long long)incompat);
        }
        q->dirty_image = (incompat & INCOMPAT_DIRTY) != 0;
    }
    if (writable && refcount_order != 4) {
        FAIL("its refcounts are %u bits; only 16-bit ones are written here",
             1u << refcount_order);
    }
    q->cs = 1ull << q->cluster_bits;
    q->l2e = (size_t)(q->cs / 8);
    q->rbe = (size_t)(q->cs / 2);
    q->csize_shift = 62 - (q->cluster_bits - 8);
    q->csize_mask = (1ull << (q->cluster_bits - 8)) - 1;
    q->coff_mask = (1ull << q->csize_shift) - 1;
    if ((uint64_t)q->l1_size * q->l2e * q->cs < q->size) {
        FAIL("its L1 table does not cover the disk");
    }
    l1_bytes = (uint64_t)q->l1_size * q->cs;
    if (l1_bytes > (1ull << 30)) {
        FAIL("its tables would take %llu MB", (unsigned long long)(l1_bytes >> 20));
    }
    fs = lr_file_size(f);
    if (fs < 0) {
        FAIL("cannot tell its size");
    }
    file_size = (uint64_t)fs;
    q->host_end = (file_size + q->cs - 1) >> q->cluster_bits;

    q->l1 = calloc(q->l1_size ? q->l1_size : 1, 8);
    q->l2 = calloc((size_t)q->l1_size * q->l2e + 1, 8);
    q->table = malloc((size_t)q->cs);
    q->cluster = malloc((size_t)q->cs);
    q->dcache = malloc((size_t)q->cs);
    q->cbuf = malloc((size_t)q->cs * 2 + 512);
    q->inflate = rinflate_new(-12);
    if (!q->l1 || !q->l2 || !q->table || !q->cluster || !q->dcache ||
        !q->cbuf || !q->inflate) {
        FAIL("out of memory");
    }
    for (uint32_t i = 0; i < q->l1_size; i += (uint32_t)q->l2e) {
        uint32_t n = q->l1_size - i < q->l2e ? q->l1_size - i : (uint32_t)q->l2e;
        if (lr_file_pread(f, (int64_t)(q->l1_offset + (uint64_t)i * 8),
                          q->table, n * 8) < 0) {
            FAIL("cannot read its L1 table");
        }
        for (uint32_t k = 0; k < n; k++) {
            q->l1[i + k] = be64(q->table + k * 8);
        }
    }
    for (uint32_t i = 0; i < q->l1_size; i++) {
        uint64_t off = q->l1[i] & OFFSET_MASK;
        if (!off) {
            continue;
        }
        if (lr_file_pread(f, (int64_t)off, q->table, (size_t)q->cs) < 0) {
            FAIL("cannot read an L2 table");
        }
        for (size_t j = 0; j < q->l2e; j++) {
            q->l2[(size_t)i * q->l2e + j] = be64(q->table + j * 8);
        }
    }

    if (writable) {
        uint32_t highest = 0;
        q->rt_entries = (size_t)rt_clusters * q->l2e;
        q->rt = calloc(q->rt_entries + 1, 8);
        if (!q->rt) {
            FAIL("out of memory");
        }
        for (size_t i = 0; i < q->rt_entries; i += q->l2e) {
            if (lr_file_pread(f, (int64_t)(q->rt_offset + i * 8), q->table,
                              (size_t)q->cs) < 0) {
                FAIL("cannot read its refcount table");
            }
            for (size_t k = 0; k < q->l2e; k++) {
                q->rt[i + k] = be64(q->table + k * 8);
                if (q->rt[i + k] & OFFSET_MASK) {
                    highest = (uint32_t)(i + k + 1);
                }
            }
        }
        /* room for the blocks the file has, and for it to grow by the whole
         * disk again (every cluster rewritten once) and its tables */
        cover = (q->host_end + (q->size >> q->cluster_bits) + q->l1_size + 64);
        q->rb_count = (size_t)((cover + q->rbe - 1) / q->rbe);
        if (q->rb_count < highest) {
            q->rb_count = highest;
        }
        if (q->rb_count > q->rt_entries) {
            q->rb_count = q->rt_entries;
        }
        q->rb = calloc(q->rb_count * q->rbe + 1, 2);
        if (!q->rb) {
            FAIL("out of memory");
        }
        for (size_t b = 0; b < q->rb_count; b++) {
            uint64_t off = q->rt[b] & OFFSET_MASK;
            if (!off) {
                continue;
            }
            if (lr_file_pread(f, (int64_t)off, q->table, (size_t)q->cs) < 0) {
                FAIL("cannot read a refcount block");
            }
            for (size_t k = 0; k < q->rbe; k++) {
                q->rb[b * q->rbe + k] = (uint16_t)(q->table[k * 2] << 8 |
                                                   q->table[k * 2 + 1]);
            }
        }
    }
    return q;

fail:
#undef FAIL
    lr_qcow2_close(q);
    return NULL;
}

void lr_qcow2_close(LRQcow2 *q)
{
    if (!q) {
        return;
    }
    if (q->inflate) {
        rinflate_free(q->inflate);
    }
    free(q->cbuf);
    free(q->dcache);
    free(q->cluster);
    free(q->table);
    free(q->rb);
    free(q->rt);
    free(q->l2);
    free(q->l1);
    free(q);
}

int64_t lr_qcow2_size(LRQcow2 *q)
{
    return (int64_t)q->size;
}

int lr_qcow2_flush(LRQcow2 *q)
{
    int ret;

    if (!q->meta_dirty) {
        return 0;
    }
    ret = lr_file_sync(q->f);
    if (ret == 0) {
        q->meta_dirty = false;
    }
    return ret;
}

/* ---- reads -------------------------------------------------------------- */

static int64_t request_len(const LRFileIOV *iov, int n)
{
    int64_t len = 0;

    if (n < 0 || (n && !iov)) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        if (iov[i].len > (uint64_t)(INT64_MAX - len)) {
            return -1;
        }
        len += (int64_t)iov[i].len;
    }
    return len;
}

int lr_qcow2_preadv(LRQcow2 *q, int64_t off, const LRFileIOV *iov, int n)
{
    Cursor c = { iov, n, 0, 0 };
    uint64_t pos = (uint64_t)off;
    int64_t bytes = request_len(iov, n);
    uint64_t left = (uint64_t)bytes;

    if (bytes < 0 || off < 0 || pos > q->size || left > q->size - pos) {
        return -EIO;
    }
    while (left) {
        uint64_t gc = pos >> q->cluster_bits, in = pos & (q->cs - 1);
        uint64_t len = q->cs - in < left ? q->cs - in : left;
        uint64_t e = *l2_entry(q, gc);
        int ret;

        if (e & F_COMPRESSED) {
            ret = read_compressed(q, gc, e);
            if (ret < 0) {
                return ret;
            }
            cursor_copy_out(&c, q->dcache + in, (size_t)len);
        } else if (reads_zero(q, e)) {
            cursor_zero(&c, (size_t)len);
        } else {
            /* and the clusters after it that follow it in the file */
            uint64_t host = (e & OFFSET_MASK) + in;
            while (len < left) {
                uint64_t ne = *l2_entry(q, gc + 1);
                uint64_t nlen = q->cs < left - len ? q->cs : left - len;
                if ((ne & F_COMPRESSED) || reads_zero(q, ne) ||
                    (ne & OFFSET_MASK) != (e & OFFSET_MASK) + q->cs) {
                    break;
                }
                e = ne;
                gc++;
                len += nlen;
            }
            ret = cursor_io(q, &c, host, (size_t)len, false);
            if (ret < 0) {
                return ret;
            }
        }
        pos += len;
        left -= len;
    }
    return 0;
}

/* ---- writes ------------------------------------------------------------- */

static int clear_autoclear(LRQcow2 *q)
{
    if (q->autoclear_done) {
        return 0;
    }
    /* features this does not keep up (bitmaps) are declared stale, as the
     * format asks of a writer that does not know them */
    if (q->autoclear) {
        int ret = write_be64(q, HDR_AUTOCLEAR, 0);
        if (ret < 0) {
            return ret;
        }
        q->autoclear = 0;
    }
    q->autoclear_done = true;
    return 0;
}

/* cluster gc made anew, len bytes of the request at in written into it */
static int write_new(LRQcow2 *q, uint64_t gc, uint64_t in, uint64_t len,
                     Cursor *c, bool zero)
{
    uint64_t e = *l2_entry(q, gc), host;
    uint32_t i = (uint32_t)(gc / q->l2e);
    bool reuse = (e & F_COPIED) && (e & OFFSET_MASK) &&
                 !(e & F_COMPRESSED);   /* an allocated zero cluster */
    bool direct = !zero && in == 0 && len == q->cs;
    int ret;

    /* A complete replacement has no old bytes to preserve or decompress.
     * Partial writes still assemble the unchanged bytes in scratch. */
    if (!direct) {
        if (e & F_COMPRESSED) {
            ret = read_compressed(q, gc, e);
            if (ret < 0) {
                return ret;
            }
            memcpy(q->cluster, q->dcache, (size_t)q->cs);
        } else if (reads_zero(q, e)) {
            memset(q->cluster, 0, (size_t)q->cs);
        } else {
            ret = lr_file_pread(q->f, (int64_t)(e & OFFSET_MASK), q->cluster,
                                (size_t)q->cs);
            if (ret < 0) {
                return ret;
            }
        }
        if (zero) {
            memset(q->cluster + in, 0, (size_t)len);
        } else {
            cursor_copy_in(c, q->cluster + in, (size_t)len);
        }
    }
    if (q->cached == gc) {
        q->cached = ~0ull;
    }
    ret = l2_writable(q, i);
    if (ret < 0) {
        return ret;
    }
    if (reuse) {
        host = e & OFFSET_MASK;
    } else {
        ret = alloc_cluster(q, &host);
        if (ret < 0) {
            return ret;
        }
    }
    ret = direct ? cursor_io(q, c, host, (size_t)len, true)
                 : lr_file_pwrite(q->f, (int64_t)host, q->cluster,
                                   (size_t)q->cs);
    if (ret < 0) {
        return ret;
    }
    ret = set_l2(q, gc, host | F_COPIED);
    if (ret < 0) {
        return ret;
    }
    return reuse ? 0 : free_entry(q, e);
}

int lr_qcow2_pwritev(LRQcow2 *q, int64_t off, const LRFileIOV *iov, int n)
{
    Cursor c = { iov, n, 0, 0 };
    uint64_t pos = (uint64_t)off;
    int64_t bytes = request_len(iov, n);
    uint64_t left = (uint64_t)bytes;
    int ret;

    if (!q->writable) {
        return -EPERM;
    }
    if (bytes < 0 || off < 0 || pos > q->size || left > q->size - pos) {
        return -EIO;
    }
    if (!left) {
        return 0;
    }
    ret = clear_autoclear(q);
    if (ret < 0) {
        return ret;
    }
    while (left) {
        uint64_t gc = pos >> q->cluster_bits, in = pos & (q->cs - 1);
        uint64_t len = q->cs - in < left ? q->cs - in : left;
        uint64_t e = *l2_entry(q, gc);

        if (in_place(q, e)) {
            uint64_t host = (e & OFFSET_MASK) + in;
            while (len < left) {
                uint64_t ne = *l2_entry(q, gc + 1);
                uint64_t nlen = q->cs < left - len ? q->cs : left - len;
                if (!in_place(q, ne) ||
                    (ne & OFFSET_MASK) != (e & OFFSET_MASK) + q->cs) {
                    break;
                }
                e = ne;
                gc++;
                len += nlen;
            }
            ret = cursor_io(q, &c, host, (size_t)len, true);
        } else {
            ret = write_new(q, gc, in, len, &c, false);
        }
        if (ret < 0) {
            return ret;
        }
        pos += len;
        left -= len;
    }
    return 0;
}

int lr_qcow2_zero(LRQcow2 *q, int64_t off, int64_t length)
{
    uint64_t pos = (uint64_t)off, left = (uint64_t)length;
    int ret;

    if (!q->writable) {
        return -EPERM;
    }
    if (off < 0 || length < 0 || pos > q->size || left > q->size - pos) {
        return -EIO;
    }
    if (!left) {
        return 0;
    }
    ret = clear_autoclear(q);
    while (ret == 0 && left) {
        uint64_t gc = pos >> q->cluster_bits, in = pos & (q->cs - 1);
        uint64_t len = q->cs - in < left ? q->cs - in : left;
        uint64_t e = *l2_entry(q, gc);

        if (reads_zero(q, e) && !(e & OFFSET_MASK)) {
            /* nothing there */
        } else if (len < q->cs && !(in == 0 && pos + len == q->size)) {
            /* part of a cluster: the rest stays */
            if (in_place(q, e)) {
                /* Only exclusive clusters can have their host ranges
                 * punched; shared entries still preserve data through COW. */
                uint64_t host = (e & OFFSET_MASK) + in;
                ret = len <= sizeof(zeroes)
                    ? lr_file_pwrite(q->f, (int64_t)host, zeroes, (size_t)len)
                    : lr_file_zero(q->f, (int64_t)host, (int64_t)len);
            } else if (!reads_zero(q, e)) {
                ret = write_new(q, gc, in, len, NULL, true);
            }
        } else {
            /* the whole cluster: no data, its space given back */
            ret = l2_writable(q, (uint32_t)(gc / q->l2e));
            if (ret == 0) {
                ret = set_l2(q, gc, q->version == 3 ? F_ZERO : 0);
            }
            if (ret == 0) {
                ret = free_entry(q, e);
            }
            if (q->cached == gc) {
                q->cached = ~0ull;
            }
        }
        pos += len;
        left -= len;
    }
    return ret;
}
