/*
 * A qcow2 image's disk as a raw image (block/qcow2-raw.h).
 *
 * The format, as docs/interop/qcow2.rst has it: a big-endian header, an L1
 * table of L2 tables, each L2 entry naming the cluster of the image that
 * holds a cluster of the disk, compressed (bit 62: the offset and size of
 * a raw deflate stream), all zeroes (bit 0, version 3) or nowhere (offset
 * 0: zeroes too, without a backing file). The refcounts and the snapshots
 * are not needed to read the disk.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qcow2-raw.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <encodings/deflate.h>

#define QCOW2_MAGIC             0x514649fbu     /* "QFI\xfb" */
#define QCOW2_OFFSET_MASK       0x00fffffffffffe00ull
#define QCOW2_COMPRESSED        (1ull << 62)
#define QCOW2_ZERO              1ull
#define QCOW2_INCOMPAT_DIRTY    (1ull << 0)
#define QCOW2_INCOMPAT_CORRUPT  (1ull << 1)
#define QCOW2_INCOMPAT_DATAFILE (1ull << 2)
#define QCOW2_INCOMPAT_COMPRESS (1ull << 3)
#define QCOW2_INCOMPAT_EXTL2    (1ull << 4)

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}

static bool all_zero(const uint8_t *p, size_t len)
{
    const uint64_t *w = (const uint64_t *)p;

    /* The cluster buffer is malloc-aligned; the final logical range may
     * finish inside a word. Check its remaining bytes with one bounded copy. */
    for (size_t i = 0; i < len / 8; i++) {
        if (w[i]) {
            return false;
        }
    }
    uint64_t tail = 0;
    memcpy(&tail, p + len / 8 * 8, len % 8);
    return tail == 0;
}

bool lr_qcow2_is_image(const void *head, size_t len)
{
    return len >= 4 && be32(head) == QCOW2_MAGIC;
}

/* the whole of len at off, the image's end read as zeroes */
static int read_full(LRQcow2ReadFunc read, void *opaque, uint64_t off,
                     void *buf, size_t len)
{
    int64_t got = read(opaque, off, buf, len);

    if (got < 0) {
        return (int)got;
    }
    if ((size_t)got < len) {
        memset((uint8_t *)buf + got, 0, len - (size_t)got);
    }
    return 0;
}

int lr_qcow2_to_raw(LRQcow2ReadFunc read, void *opaque, LRFile *dst,
                    char *err, size_t errlen)
{
    uint8_t hdr[112];
    uint32_t version, cluster_bits, l1_size;
    uint64_t size, l1_offset, cluster, incompat = 0;
    uint64_t *l1 = NULL, csize_shift, csize_mask, coff_mask;
    uint8_t *l2 = NULL, *data = NULL, *cbuf = NULL;
    void *inflate = NULL;
    size_t l2_entries;
    int ret;

#define FAIL(code, ...) do { \
        snprintf(err, errlen, __VA_ARGS__); \
        ret = (code); \
        goto out; \
    } while (0)

    ret = read_full(read, opaque, 0, hdr, sizeof(hdr));
    if (ret < 0) {
        FAIL(ret, "cannot read its header");
    }
    if (be32(hdr) != QCOW2_MAGIC) {
        FAIL(-EINVAL, "not a qcow2 image");
    }
    version = be32(hdr + 4);
    cluster_bits = be32(hdr + 20);
    size = be64(hdr + 24);
    l1_size = be32(hdr + 36);
    l1_offset = be64(hdr + 40);
    if (version != 2 && version != 3) {
        FAIL(-ENOTSUP, "qcow2 version %u", version);
    }
    if (be64(hdr + 8)) {
        FAIL(-ENOTSUP, "it has a backing file");
    }
    if (be32(hdr + 32)) {
        FAIL(-ENOTSUP, "it is encrypted");
    }
    if (cluster_bits < 9 || cluster_bits > 21) {
        FAIL(-EINVAL, "clusters of 2^%u bytes", cluster_bits);
    }
    if (version == 3) {
        incompat = be64(hdr + 72);
        if (incompat & QCOW2_INCOMPAT_CORRUPT) {
            FAIL(-EINVAL, "qemu-img marked it corrupt");
        }
        if (incompat & QCOW2_INCOMPAT_DATAFILE) {
            FAIL(-ENOTSUP, "its data is in another file");
        }
        if (incompat & QCOW2_INCOMPAT_EXTL2) {
            FAIL(-ENOTSUP, "it has subclusters");
        }
        if ((incompat & QCOW2_INCOMPAT_COMPRESS) && be32(hdr + 100) >= 105 &&
            hdr[104] != 0) {
            FAIL(-ENOTSUP, "its clusters are compressed with zstd");
        }
        if (incompat & ~(QCOW2_INCOMPAT_DIRTY | QCOW2_INCOMPAT_COMPRESS)) {
            FAIL(-ENOTSUP, "features this core does not know (0x%llx)",
                 (unsigned long long)incompat);
        }
    }
    cluster = 1ull << cluster_bits;
    l2_entries = (size_t)(cluster / 8);
    if ((uint64_t)l1_size * l2_entries * cluster < size ||
        l1_size > 32 * 1024 * 1024) {
        FAIL(-EINVAL, "its L1 table does not cover the disk");
    }
    csize_shift = 62 - (cluster_bits - 8);
    csize_mask = (1ull << (cluster_bits - 8)) - 1;
    coff_mask = (1ull << csize_shift) - 1;

    l1 = malloc((size_t)l1_size * 8 + 8);
    l2 = malloc((size_t)cluster);
    data = malloc((size_t)cluster);
    /* a compressed cluster takes at most a cluster and its last sector */
    cbuf = malloc((size_t)cluster * 2 + 512);
    inflate = rinflate_new(-12);
    if (!l1 || !l2 || !data || !cbuf || !inflate) {
        FAIL(-ENOMEM, "out of memory");
    }
    ret = read_full(read, opaque, l1_offset, l1, (size_t)l1_size * 8);
    if (ret < 0) {
        FAIL(ret, "cannot read its L1 table");
    }
    ret = lr_file_set_size(dst, (int64_t)size);
    if (ret < 0) {
        FAIL(ret, "cannot size the raw image: %s", strerror(-ret));
    }

    for (uint32_t i = 0; i < l1_size; i++) {
        uint64_t l2_offset = be64((uint8_t *)&l1[i]) & QCOW2_OFFSET_MASK;

        if (!l2_offset) {
            continue;
        }
        ret = read_full(read, opaque, l2_offset, l2, (size_t)cluster);
        if (ret < 0) {
            FAIL(ret, "cannot read an L2 table");
        }
        for (size_t j = 0; j < l2_entries; j++) {
            uint64_t e = be64(l2 + j * 8);
            uint64_t guest = ((uint64_t)i * l2_entries + j) << cluster_bits;
            size_t len = (size_t)cluster;

            if (guest >= size) {
                break;
            }
            if (size - guest < len) {
                len = (size_t)(size - guest);
            }
            if (e & QCOW2_COMPRESSED) {
                uint64_t coff = e & coff_mask;
                size_t csize = (size_t)((((e >> csize_shift) & csize_mask) + 1)
                                        * 512 - (coff & 511));
                size_t wrote = 0;
                int r;

                ret = read_full(read, opaque, coff, cbuf, csize);
                if (ret < 0) {
                    FAIL(ret, "cannot read a compressed cluster");
                }
                rinflate_reset(inflate, -12);
                rinflate_set_in(inflate, cbuf, csize);
                rinflate_set_out(inflate, data, (size_t)cluster);
                r = rinflate_process(inflate, NULL, &wrote);
                /* the size is known to a sector: the stream may end before
                 * the input does, and a full cluster is what counts */
                if (r == RDEFLATE_PROCESS_ERROR || wrote != cluster) {
                    FAIL(-EIO, "a compressed cluster does not decompress "
                         "(disk offset 0x%llx)", (unsigned long long)guest);
                }
            } else if ((version == 3 && (e & QCOW2_ZERO)) ||
                       !(e & QCOW2_OFFSET_MASK)) {
                continue;
            } else {
                ret = read_full(read, opaque, e & QCOW2_OFFSET_MASK, data,
                                len);
                if (ret < 0) {
                    FAIL(ret, "cannot read a cluster");
                }
            }
            if (all_zero(data, len)) {
                continue;
            }
            ret = lr_file_pwrite(dst, (int64_t)guest, data, len);
            if (ret < 0) {
                FAIL(ret, "cannot write the raw image: %s", strerror(-ret));
            }
        }
    }
    ret = 0;
    if (errlen) {
        err[0] = '\0';
    }
out:
#undef FAIL
    if (inflate) {
        rinflate_free(inflate);
    }
    free(cbuf);
    free(data);
    free(l2);
    free(l1);
    return ret;
}
