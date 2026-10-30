/* The HDD's qcow2 images, on their own, without the core:
 *
 *   test-qcow2 <data/xbox_hdd.qcow2> <work dir>
 *       the bundled image written out as a raw one (block/qcow2-raw.c),
 *       as the core does for a new HDD
 *   test-qcow2 --workload <image.qcow2> <reference.raw> <seed> <ops>
 *       qcow2 read and written in place (block/libretro-qcow2.c): ops
 *       random writes, zeroed ranges and reads, each mirrored on the raw
 *       reference and every read checked against it, the image closed and
 *       opened again now and then; at the end the whole of what was
 *       touched read back. run.sh then has qemu-img check the image and
 *       compare it with the reference (and its snapshot, untouched).
 *
 * The conversion:
 * - qcow2 images it writes itself, versions 2 and 3, clusters of 512 bytes,
 *   4 KB and 64 KB, a disk that ends inside its last cluster: clusters
 *   that hold data, that are compressed (deflate), that are marked zero,
 *   that are not allocated and that are allocated but all zero. The raw
 *   image must read back each cluster's bytes, be the disk's size, and
 *   (where the file system has holes) take no space for the clusters
 *   without data.
 * - What it must refuse: a backing file, encryption, zstd clusters.
 * - The HDD image the core carries: its clusters against the CRC-32s of
 *   qemu-img convert's raw image of it (qemu-img 8.2), the rest holes.
 *
 * Exit status 0 when all hold. Plain C99, with libretro-common's deflate.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <encodings/crc32.h>
#include <encodings/deflate.h>

#include "../../block/libretro-file.h"
#include "../../block/qcow2-raw.h"
#include "../../block/libretro-qcow2.h"

static int failures;

#define CHECK(cond, ...) do {         if (!(cond)) {             printf("FAIL: " __VA_ARGS__);             printf("\n");             failures++;         }     } while (0)

/* an image in memory */
typedef struct {
    uint8_t *p;
    size_t len, cap;
} Img;

static void img_put(Img *m, size_t off, const void *src, size_t len)
{
    if (off + len > m->cap) {
        size_t cap = (off + len) * 2;
        m->p = realloc(m->p, cap);
        memset(m->p + m->cap, 0, cap - m->cap);
        m->cap = cap;
    }
    memcpy(m->p + off, src, len);
    if (off + len > m->len) {
        m->len = off + len;
    }
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void put_be64(uint8_t *p, uint64_t v)
{
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

static int64_t img_read(void *opaque, uint64_t off, void *buf, size_t len)
{
    Img *m = opaque;
    if (off >= m->len) {
        return 0;
    }
    if (len > m->len - off) {
        len = m->len - off;
    }
    memcpy(buf, m->p + off, len);
    return (int64_t)len;
}

static int64_t file_read(void *opaque, uint64_t off, void *buf, size_t len)
{
    FILE *f = opaque;
    if (fseek(f, (long)off, SEEK_SET) != 0) {
        return -5;
    }
    return (int64_t)fread(buf, 1, len, f);
}

/* the byte at a disk offset of cluster k, for the clusters that hold data */
static uint8_t pattern(uint64_t off, unsigned k)
{
    return (uint8_t)((off * 2654435761u >> 7) ^ (k * 37) ^ 0x5a);
}

enum { C_NONE, C_DATA, C_COMPRESSED, C_ZERO, C_DATA_ZERO, C_KINDS };

/* a compressed cluster: text-like, so it shrinks */
static void fill_cluster(uint8_t *buf, size_t cs, unsigned k, int kind)
{
    for (size_t i = 0; i < cs; i++) {
        buf[i] = kind == C_COMPRESSED ? (uint8_t)("xbox fatx "[(i + k) % 10])
                                      : pattern(i, k);
    }
}

/* A version 2 or 3 image of nclusters clusters (the last one short by
 * tail bytes), cluster k of kind k % C_KINDS. */
static Img make_image(unsigned version, unsigned bits, unsigned nclusters,
                      unsigned tail, uint64_t incompat)
{
    Img m = { 0 };
    size_t cs = (size_t)1 << bits;
    size_t l2e = cs / 8;
    unsigned nl2 = (unsigned)((nclusters + l2e - 1) / l2e);
    uint64_t size = (uint64_t)nclusters * cs - tail;
    size_t next, l1off = cs, l2off = 2 * cs;
    uint8_t hdr[112] = { 0 }, *buf = malloc(cs), *cbuf = malloc(cs * 2);
    uint8_t *l1 = calloc(nl2, 8), *l2 = calloc(nl2, cs);
    size_t cpos = 0, cend = 0;

    put_be32(hdr, 0x514649fb);
    put_be32(hdr + 4, version);
    put_be32(hdr + 20, bits);
    put_be64(hdr + 24, size);
    put_be32(hdr + 36, nl2);
    put_be64(hdr + 40, l1off);
    if (version == 3) {
        put_be64(hdr + 72, incompat);
        put_be32(hdr + 96, 4);
        put_be32(hdr + 100, 112);
    }
    next = l2off + nl2 * cs;
    for (unsigned k = 0; k < nclusters; k++) {
        int kind = (int)(k % C_KINDS);
        uint64_t e = 0;
        size_t len = cs;

        if (kind == C_ZERO && version == 2) {
            kind = C_NONE;
        }
        switch (kind) {
        case C_DATA:
        case C_DATA_ZERO:
            if (kind == C_DATA) {
                fill_cluster(buf, cs, k, kind);
            } else {
                memset(buf, 0, cs);
            }
            img_put(&m, next, buf, cs);
            e = next | (1ull << 63);
            next += cs;
            break;
        case C_COMPRESSED: {
            void *d = rdeflate_new(6, -12);
            size_t wrote = 0;
            uint64_t shift = 62 - (bits - 8), sectors;
            fill_cluster(buf, cs, k, kind);
            rdeflate_set_in(d, buf, cs);
            rdeflate_set_out(d, cbuf, cs * 2);
            rdeflate_finish(d);
            rdeflate_process(d, NULL, &wrote);
            rdeflate_free(d);
            /* packed one after another, from a byte offset, as qemu does */
            if (!cpos || cpos + wrote > cend) {
                cpos = next;
                next += cs * 4;
                cend = next;
            }
            img_put(&m, cpos, cbuf, wrote);
            sectors = ((cpos & 511) + wrote + 511) / 512;
            e = (1ull << 62) | (sectors - 1) << shift | cpos;
            cpos += wrote;
            (void)len;
            break;
        }
        case C_ZERO:
            e = 1;
            break;
        default:
            break;
        }
        put_be64(l2 + (size_t)k * 8, e);
    }
    for (unsigned i = 0; i < nl2; i++) {
        put_be64(l1 + i * 8, (l2off + i * cs) | (1ull << 63));
    }
    img_put(&m, 0, hdr, sizeof(hdr));
    img_put(&m, l1off, l1, nl2 * 8);
    img_put(&m, l2off, l2, nl2 * cs);
    free(buf);
    free(cbuf);
    free(l1);
    free(l2);
    return m;
}

/* whether holes take no space here: a file grown by its size alone */
static int have_holes(const char *dir)
{
    char path[4096];
    LRFile *f;
    int64_t a;
    snprintf(path, sizeof(path), "%s/holes.img", dir);
    remove(path);
    f = lr_file_open(path, 1, 1);
    if (!f) {
        return 0;
    }
    lr_file_set_size(f, 256ll << 20);
    a = lr_file_allocated(f);
    lr_file_close(f);
    remove(path);
    return a >= 0 && a < (1 << 20);
}

static void test_made(const char *dir, int holes, unsigned version,
                      unsigned bits, unsigned nclusters, unsigned tail,
                      uint64_t incompat)
{
    Img m = make_image(version, bits, nclusters, tail, incompat);
    size_t cs = (size_t)1 << bits;
    uint64_t size = (uint64_t)nclusters * cs - tail;
    char path[4096], err[256];
    uint8_t *want = malloc(cs), *got = malloc(cs);
    unsigned data = 0, bad = 0;
    LRFile *f;
    int ret;

    snprintf(path, sizeof(path), "%s/made.img", dir);
    remove(path);
    f = lr_file_open(path, 1, 1);
    ret = lr_qcow2_to_raw(img_read, &m, f, err, sizeof(err));
    CHECK(ret == 0, "v%u, %u-byte clusters: %s", version, (unsigned)cs, err);
    CHECK(lr_file_size(f) == (int64_t)size, "v%u, %u-byte clusters: size "
          "%lld, not %llu", version, (unsigned)cs,
          (long long)lr_file_size(f), (unsigned long long)size);
    for (unsigned k = 0; k < nclusters; k++) {
        int kind = (int)(k % C_KINDS);
        size_t len = k == nclusters - 1 ? cs - tail : cs;
        if (kind == C_DATA || kind == C_COMPRESSED) {
            fill_cluster(want, cs, k, kind);
            data++;
        } else {
            memset(want, 0, cs);
        }
        lr_file_pread(f, (int64_t)k * cs, got, len);
        if (memcmp(want, got, len)) {
            bad++;
        }
    }
    CHECK(!bad, "v%u, %u-byte clusters: %u of %u clusters read back wrong",
          version, (unsigned)cs, bad, nclusters);
    if (holes) {
        int64_t a = lr_file_allocated(f);
        /* the data's clusters, and a block of slack for each run */
        int64_t most = (int64_t)data * (cs < 4096 ? 4096 : cs) * 2 + 65536;
        CHECK(a <= most, "v%u, %u-byte clusters: %lld bytes on the disk "
              "for %u clusters of data", version, (unsigned)cs,
              (long long)a, data);
    }
    lr_file_close(f);
    remove(path);
    free(m.p);
    free(want);
    free(got);
    printf("v%u, %5u-byte clusters, %u clusters (%u of data): %s\n",
           version, (unsigned)cs, nclusters, data, bad ? "wrong" : "ok");
}

static void test_refused(const char *dir, const char *what, size_t at,
                         uint64_t v, int is64)
{
    Img m = make_image(3, 12, 8, 0, 0);
    char path[4096], err[256];
    LRFile *f;
    if (is64) {
        put_be64(m.p + at, v);
    } else {
        m.p[at] = (uint8_t)v;
        put_be64(m.p + 72, 1ull << 3);
    }
    snprintf(path, sizeof(path), "%s/refused.img", dir);
    remove(path);
    f = lr_file_open(path, 1, 1);
    CHECK(lr_qcow2_to_raw(img_read, &m, f, err, sizeof(err)) < 0,
          "an image with %s converted", what);
    printf("refused, %s: %s\n", what, err);
    lr_file_close(f);
    remove(path);
    free(m.p);
}

static const struct {
    uint64_t off;
    uint32_t crc;
} bundled[] = {
    { 0x0ull, 0x13f15f48u },
    { 0x80000ull, 0x687c1875u },
    { 0x90000ull, 0x2b9700d2u },
    { 0x2ee80000ull, 0xddb9b871u },
    { 0x2ee90000ull, 0x2b9700d2u },
    { 0x5dc80000ull, 0x021b30aeu },
    { 0x5dc90000ull, 0x2b9700d2u },
    { 0x8ca80000ull, 0x02c70349u },
    { 0x8ca90000ull, 0xdbd90b7fu },
    { 0x8caa0000ull, 0xc9b2dd29u },
    { 0x8cab0000ull, 0x80a4748eu },
    { 0x8cac0000ull, 0x45753ec0u },
    { 0x8cad0000ull, 0x025d2134u },
    { 0x8cae0000ull, 0x89445729u },
    { 0x8caf0000ull, 0x7c3152c7u },
    { 0x8cb00000ull, 0xf4644208u },
    { 0x8cb10000ull, 0x915e3cbau },
    { 0x8cb20000ull, 0xb797ac5cu },
    { 0x8cb30000ull, 0x901670c6u },
    { 0x8cb40000ull, 0x19d7e62bu },
    { 0x8cb50000ull, 0x9d4eeec3u },
    { 0x8cb60000ull, 0x9ff17037u },
    { 0x8cb70000ull, 0xda53e194u },
    { 0x8cb80000ull, 0xca218056u },
    { 0x8cb90000ull, 0x0a4f3be5u },
    { 0x8cba0000ull, 0x7a58b382u },
    { 0x8cbb0000ull, 0xd6a93b28u },
    { 0x8cbc0000ull, 0xb602d699u },
    { 0x8cbd0000ull, 0x8fdce656u },
    { 0x8cbe0000ull, 0x8b3f02abu },
    { 0x8cbf0000ull, 0xbcd09e5du },
    { 0x8cc00000ull, 0x8798e5d8u },
    { 0x8cc10000ull, 0x591bfb70u },
    { 0x8cc20000ull, 0x0cbbaa60u },
    { 0x8cc30000ull, 0x9f6c4f9cu },
    { 0x8cc40000ull, 0xaea8d23du },
    { 0x8cc50000ull, 0xa934f9bbu },
    { 0x8cc60000ull, 0x3208d3fcu },
    { 0xabe80000ull, 0xcbbffeedu },
    { 0xabfb0000ull, 0xbb042651u },
};

static void test_bundled(const char *qcow2, const char *dir, int holes)
{
    FILE *in = fopen(qcow2, "rb");
    char path[4096], err[256];
    uint8_t *buf = malloc(65536);
    unsigned bad = 0, n = sizeof(bundled) / sizeof(bundled[0]);
    LRFile *f;
    int ret;

    CHECK(in != NULL, "cannot open %s", qcow2);
    if (!in) {
        return;
    }
    snprintf(path, sizeof(path), "%s/bundled.img", dir);
    remove(path);
    f = lr_file_open(path, 1, 1);
    ret = lr_qcow2_to_raw(file_read, in, f, err, sizeof(err));
    CHECK(ret == 0, "the bundled HDD: %s", err);
    CHECK(lr_file_size(f) == 8589934592ll, "the bundled HDD is %lld bytes",
          (long long)lr_file_size(f));
    for (unsigned i = 0; i < n; i++) {
        lr_file_pread(f, (int64_t)bundled[i].off, buf, 65536);
        if (encoding_crc32(0, buf, 65536) != bundled[i].crc) {
            if (bad < 5) {
                printf("cluster at 0x%llx differs from qemu-img's\n",
                       (unsigned long long)bundled[i].off);
            }
            bad++;
        }
    }
    CHECK(!bad, "the bundled HDD: %u of %u clusters differ from qemu-img's",
          bad, n);
    if (holes) {
        /* the 40 clusters, nothing else */
        int64_t a = lr_file_allocated(f);
        CHECK(a <= (int64_t)n * 65536 + 65536, "the bundled HDD takes %lld "
              "bytes on the disk", (long long)a);
        printf("the bundled HDD: %u clusters as qemu-img has them, %lld KB "
               "on the disk of 8 GB\n", n - bad, (long long)(a >> 10));
    } else {
        printf("the bundled HDD: %u clusters as qemu-img has them (no holes "
               "on this file system)\n", n - bad);
    }
    lr_file_close(f);
    remove(path);
    fclose(in);
    free(buf);
}

/* ---- the workload ---- */

static uint64_t rng_state;

static uint64_t rnd(void)
{
    /* xorshift64*: the same run for the same seed */
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 2685821657736338717ull;
}

static int workload(const char *qpath, const char *rpath, uint64_t seed,
                    long ops)
{
    char err[256];
    LRFile *qf = lr_file_open(qpath, 1, 0), *rf = lr_file_open(rpath, 1, 0);
    LRQcow2 *q;
    int64_t size;
    size_t max = 512 * 1024;
    uint8_t *a = malloc(max), *b = malloc(max);
    uint64_t *touched_lo = calloc(ops + 1, 8), *touched_hi = calloc(ops + 1, 8);
    long nt = 0, bad = 0;

    rng_state = seed ? seed : 1;
    if (!qf || !rf) {
        printf("FAIL: cannot open %s or %s\n", qpath, rpath);
        return 1;
    }
    q = lr_qcow2_open(qf, 1, err, sizeof(err));
    if (!q) {
        printf("FAIL: %s: %s\n", qpath, err);
        return 1;
    }
    size = lr_qcow2_size(q);
    for (long op = 0; op < ops; op++) {
        uint64_t r = rnd(), kind = r % 10;
        uint64_t len = 1 + rnd() % max, off;
        /* half of them where the earlier ones were, so clusters are
         * rewritten, freed and used again */
        if (nt && (rnd() & 1)) {
            off = touched_lo[rnd() % nt] + rnd() % 70000;
        } else {
            off = rnd() % (uint64_t)size;
        }
        if (rnd() % 3 == 0) {
            off &= ~65535ull;           /* cluster-aligned, often whole */
            len = (len + 65535) & ~65535ull;
            if (len > max) {
                len = max;
            }
        }
        if (off >= (uint64_t)size) {
            off = (uint64_t)size - 1;
        }
        if (len > (uint64_t)size - off) {
            len = (uint64_t)size - off;
        }
        if (kind < 5) {
            /* a write */
            for (uint64_t i = 0; i < len; i++) {
                a[i] = (uint8_t)(rnd() >> 56);
            }
            LRFileIOV iov[3];
            uint64_t c1 = len / 3, c2 = len / 2;
            iov[0].base = a; iov[0].len = (size_t)c1;
            iov[1].base = a + c1; iov[1].len = (size_t)(c2 - c1);
            iov[2].base = a + c2; iov[2].len = (size_t)(len - c2);
            if (lr_qcow2_pwritev(q, (int64_t)off, iov, 3) < 0 ||
                lr_file_pwrite(rf, (int64_t)off, a, (size_t)len) < 0) {
                printf("FAIL: a write at 0x%llx failed\n",
                       (unsigned long long)off);
                return 1;
            }
            touched_lo[nt] = off;
            touched_hi[nt++] = off + len;
        } else if (kind < 7) {
            /* a range zeroed, as Trim HDD on Close does */
            uint64_t zlen = len * (1 + rnd() % 8);
            if (zlen > (uint64_t)size - off) {
                zlen = (uint64_t)size - off;
            }
            if (lr_qcow2_zero(q, (int64_t)off, (int64_t)zlen) < 0) {
                printf("FAIL: zeroing at 0x%llx failed\n",
                       (unsigned long long)off);
                return 1;
            }
            for (uint64_t k = 0; k < zlen; k += max) {
                uint64_t m = zlen - k < max ? zlen - k : max;
                memset(a, 0, (size_t)m);
                lr_file_pwrite(rf, (int64_t)(off + k), a, (size_t)m);
            }
            touched_lo[nt] = off;
            touched_hi[nt++] = off + zlen;
        } else if (kind < 9) {
            /* a read, checked */
            LRFileIOV iov[2];
            iov[0].base = a; iov[0].len = (size_t)(len / 2);
            iov[1].base = a + len / 2; iov[1].len = (size_t)(len - len / 2);
            if (lr_qcow2_preadv(q, (int64_t)off, iov, 2) < 0) {
                printf("FAIL: a read at 0x%llx failed\n",
                       (unsigned long long)off);
                return 1;
            }
            lr_file_pread(rf, (int64_t)off, b, (size_t)len);
            if (memcmp(a, b, (size_t)len)) {
                if (bad < 5) {
                    printf("read at 0x%llx (%llu bytes) differs\n",
                           (unsigned long long)off, (unsigned long long)len);
                }
                bad++;
            }
        } else {
            /* closed (its tables flushed) and opened again */
            lr_qcow2_flush(q);
            lr_qcow2_close(q);
            q = lr_qcow2_open(qf, 1, err, sizeof(err));
            if (!q) {
                printf("FAIL: opened again: %s\n", err);
                return 1;
            }
        }
    }
    /* everything touched, read back */
    for (long t = 0; t < nt; t++) {
        for (uint64_t o = touched_lo[t]; o < touched_hi[t]; o += max) {
            uint64_t m = touched_hi[t] - o < max ? touched_hi[t] - o : max;
            LRFileIOV iov = { a, (size_t)m };
            lr_qcow2_preadv(q, (int64_t)o, &iov, 1);
            lr_file_pread(rf, (int64_t)o, b, (size_t)m);
            if (memcmp(a, b, (size_t)m)) {
                if (bad < 5) {
                    printf("at the end, 0x%llx differs\n",
                           (unsigned long long)o);
                }
                bad++;
            }
        }
    }
    lr_qcow2_flush(q);
    lr_qcow2_close(q);
    lr_file_close(qf);
    lr_file_close(rf);
    printf("qcow2 workload, %ld operations on %s: %s\n", ops, qpath,
           bad ? "WRONG" : "every read as the reference");
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    int holes;

    if (argc == 6 && !strcmp(argv[1], "--workload")) {
        return workload(argv[2], argv[3], strtoull(argv[4], NULL, 0),
                        atol(argv[5]));
    }

    if (argc < 3) {
        fprintf(stderr, "usage: %s data/xbox_hdd.qcow2 workdir\n", argv[0]);
        return 2;
    }
    holes = have_holes(argv[2]);
    if (!holes) {
        printf("no holes on this file system: the space taken is not "
               "checked\n");
    }
    test_made(argv[2], holes, 3, 16, 300, 512 * 3, 0);
    test_made(argv[2], holes, 3, 16, 300, 0, 1);      /* marked dirty */
    test_made(argv[2], holes, 2, 16, 300, 0, 0);
    test_made(argv[2], holes, 3, 12, 2000, 1024, 0);  /* several L2 tables */
    test_made(argv[2], holes, 2, 9, 3000, 0, 0);
    test_refused(argv[2], "a backing file", 8, 4096, 1);
    test_refused(argv[2], "encryption", 32, 1ull << 32, 1);
    test_refused(argv[2], "zstd clusters", 104, 1, 0);
    test_bundled(argv[1], argv[2], holes);
    if (failures) {
        printf("FAIL: %d checks of the qcow2 conversion\n", failures);
        return 1;
    }
    printf("qcow2 conversion: all passed\n");
    return 0;
}
