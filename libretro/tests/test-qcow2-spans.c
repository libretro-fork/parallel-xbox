/* Public qcow2 I/O with counted file calls. Use a disposable copy of the
 * bundled 64-KiB-cluster HDD, never a user's disk. */
#include "block/libretro-qcow2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned reads, writes;
static int last_read_iov;

int counted_preadv(LRFile *f, int64_t off, const LRFileIOV *iov, int n)
{
    reads++;
    last_read_iov = n;
    return lr_file_preadv(f, off, iov, n);
}

int counted_pwritev(LRFile *f, int64_t off, const LRFileIOV *iov, int n)
{
    writes++;
    return lr_file_pwritev(f, off, iov, n);
}

int main(int argc, char **argv)
{
    enum { SPANS = 130, PIECE = 256, BYTES = SPANS * PIECE };
    uint8_t input[BYTES], output[BYTES * 2], gap[PIECE], bits[4];
    LRFileIOV src[SPANS + 2] = {{0}}, dst[SPANS + 2] = {{0}};
    char why[256];
    const int64_t offset = 16 * 1024 * 1024;
    assert(argc == 2);
    LRFile *f = lr_file_open(argv[1], true, false);
    assert(f && lr_file_pread(f, 20, bits, sizeof(bits)) == 0);
    assert(!memcmp(bits, (uint8_t[]) {0, 0, 0, 16}, 4));
    LRQcow2 *q = lr_qcow2_open(f, true, why, sizeof(why));
    assert(q && lr_qcow2_size(q) >= offset + 65536 + BYTES);
    for (unsigned i = 0; i < BYTES; i++) {
        input[i] = (uint8_t)(i * 13 + i / 251);
    }
    LRFileIOV whole = {input, BYTES};
    assert(lr_qcow2_pwritev(q, offset, &whole, 1) == 0);
    for (unsigned i = 0; i < BYTES; i++) {
        input[i] ^= 0x5a; /* the fragmented write must replace existing data */
    }
    for (unsigned i = 0; i < SPANS; i++) {
        src[i + 1] = (LRFileIOV) {input + i * PIECE, PIECE};
        dst[i + 1] = (LRFileIOV) {output + i * PIECE, PIECE};
    }
    writes = 0;
    assert(lr_qcow2_pwritev(q, offset, src, SPANS + 2) == 0);
    assert(writes == 1);
    reads = 0;
    assert(lr_qcow2_preadv(q, offset, dst, SPANS + 2) == 0);
    assert(reads == 1 && !memcmp(input, output, BYTES));
    printf("130 adjacent spans: file reads=%u writes=%u\n", reads, writes);

    memset(output, 0xcc, sizeof(output));
    memset(gap, 0xcc, sizeof(gap));
    for (unsigned i = 0; i < SPANS; i++) {
        dst[i + 1].base = output + i * PIECE * 2;
    }
    reads = 0;
    assert(lr_qcow2_preadv(q, offset, dst, SPANS + 2) == 0);
    assert(reads == 3); /* 64 + 64 + 2 actual, non-adjacent destinations */
    for (unsigned i = 0; i < SPANS; i++) {
        assert(!memcmp(output + i * PIECE * 2, input + i * PIECE, PIECE));
        assert(!memcmp(output + (i * 2 + 1) * PIECE, gap, PIECE));
    }

    /* The 64th run can keep consuming adjacent spans before starting the
     * next file call. There are 65 runs, each made of two original spans. */
    memset(output, 0xcc, sizeof(output));
    for (unsigned i = 0; i < SPANS; i++) {
        dst[i + 1].base = output + (i / 2 * 3 + i % 2) * PIECE;
    }
    reads = 0;
    assert(lr_qcow2_preadv(q, offset, dst, SPANS + 2) == 0);
    assert(reads == 2 && last_read_iov == 1);
    for (unsigned i = 0; i < SPANS; i++) {
        assert(!memcmp(dst[i + 1].base, input + i * PIECE, PIECE));
    }
    for (unsigned i = 0; i < SPANS / 2; i++) {
        assert(!memcmp(output + (i * 3 + 2) * PIECE, gap, PIECE));
    }
    for (unsigned i = 0; i < SPANS; i++) {
        dst[i + 1].base = output + i * PIECE * 2;
    }
    memset(output, 0xcc, sizeof(output));

    /* Split inside an original span at a cluster boundary. */
    int64_t crossing = offset + 65536 - 13;
    assert(lr_qcow2_pwritev(q, crossing, src, SPANS + 2) == 0);
    assert(lr_qcow2_preadv(q, crossing, dst, SPANS + 2) == 0);
    for (unsigned i = 0; i < SPANS; i++) {
        assert(!memcmp(output + i * PIECE * 2, input + i * PIECE, PIECE));
        assert(!memcmp(output + (i * 2 + 1) * PIECE, gap, PIECE));
    }
    assert(lr_qcow2_zero(q, crossing, BYTES) == 0);
    assert(lr_qcow2_preadv(q, crossing, dst, SPANS + 2) == 0);
    memset(input, 0, sizeof(input));
    for (unsigned i = 0; i < SPANS; i++) {
        assert(!memcmp(output + i * PIECE * 2, input + i * PIECE, PIECE));
        assert(!memcmp(output + (i * 2 + 1) * PIECE, gap, PIECE));
    }
    assert(lr_qcow2_flush(q) == 0);
    lr_qcow2_close(q);
    lr_file_close(f);
    puts("qcow2 bulk spans, bounded gaps, empty spans and cluster splits: PASS");
}
