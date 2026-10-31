/* Minimal read-only qcow2 fixtures; no refcounts are needed for conversion.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "block/qcow2-raw.h"
#include <encodings/deflate.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *image;
    size_t size, data_offset, data_bytes;
} Source;

static unsigned writes;

int counted_convert_write(LRFile *f, int64_t off, const void *buf, size_t len)
{
    writes++;
    return lr_file_pwrite(f, off, buf, len);
}

static void put64(uint8_t *p, uint64_t v)
{
    for (unsigned i = 0; i < 8; i++) {
        p[7 - i] = (uint8_t)v;
        v >>= 8;
    }
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) {
        p[3 - i] = (uint8_t)v;
        v >>= 8;
    }
}

static int64_t read_source(void *opaque, uint64_t off, void *buf, size_t len)
{
    Source *s = opaque;
    if (off == s->data_offset) {
        s->data_bytes += len;
    }
    if (off >= s->size) {
        return 0;
    }
    if (len > s->size - off) {
        len = s->size - off;
    }
    memcpy(buf, s->image + off, len);
    return (int64_t)len;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    unsigned cases = 0;
    const unsigned sizes[] = {9, 16};
    const size_t lengths[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                             15, 16, 17, 511, 512, 513, 4096, 65535, 65536};
    for (unsigned version = 2; version <= 3; version++) {
        for (unsigned b = 0; b < 2; b++) {
            unsigned bits = sizes[b];
            size_t cs = (size_t)1 << bits;
            uint8_t *image = calloc(4, cs), *data = malloc(cs), *packed = malloc(cs * 2);
            uint8_t *out = malloc(cs);
            assert(image && data && packed && out);
            put32(image, 0x514649fb);
            put32(image + 4, version);
            put32(image + 20, bits);
            put32(image + 36, 1);
            put64(image + 40, cs);
            put64(image + cs, (uint64_t)2 * cs | (UINT64_C(1) << 63));
            if (version == 3) {
                put32(image + 96, 4);
                put32(image + 100, 112);
            }
            for (unsigned compressed = 0; compressed < 2; compressed++) {
                for (unsigned l = 0; l < sizeof(lengths) / sizeof(lengths[0]); l++) {
                    size_t len = lengths[l];
                    if (len > cs) {
                        continue;
                    }
                    for (unsigned nonzero = 0; nonzero < 2; nonzero++) {
                        memset(data, 0xa5, cs); /* outside the virtual disk */
                        memset(data, 0, len);
                        if (nonzero) {
                            data[len - 1] = 0x7c;
                        }
                        put64(image + 24, len);
                        size_t expected_read = len;
                        if (compressed) {
                            void *d = rdeflate_new(6, -12);
                            size_t wrote = 0;
                            assert(d);
                            rdeflate_set_in(d, data, cs);
                            rdeflate_set_out(d, packed, cs * 2);
                            rdeflate_finish(d);
                            rdeflate_process(d, NULL, &wrote);
                            rdeflate_free(d);
                            assert(wrote && wrote <= cs);
                            memset(image + 3 * cs, 0, cs);
                            memcpy(image + 3 * cs, packed, wrote);
                            uint64_t sectors = (wrote + 511) / 512;
                            put64(image + 2 * cs, (UINT64_C(1) << 62) |
                                  (sectors - 1) << (62 - (bits - 8)) | 3 * cs);
                            expected_read = (size_t)sectors * 512;
                        } else {
                            memcpy(image + 3 * cs, data, cs);
                            put64(image + 2 * cs, (UINT64_C(1) << 63) | 3 * cs);
                        }
                        Source source = {image, 4 * cs, 3 * cs, 0};
                        LRFile *dst = lr_file_open(argv[1], true, true);
                        assert(dst && lr_file_set_size(dst, 0) == 0);
                        char err[256];
                        writes = 0;
                        assert(lr_qcow2_to_raw(read_source, &source, dst, err, sizeof(err)) == 0);
                        assert(source.data_bytes == expected_read);
                        assert(writes == nonzero);
                        assert(lr_file_size(dst) == (int64_t)len);
                        assert(lr_file_pread(dst, 0, out, len) == 0);
                        assert(!memcmp(out, data, len));
                        lr_file_close(dst);
                        cases++;
                    }
                }
            }
            free(packed);
            free(out);
            free(data);
            free(image);
        }
    }
    printf("qcow2 conversion: %u tails, minimal reads, zero-write elision and final byte: PASS\n", cases);
    return 0;
}
