/* Public qcow2 I/O with counted file calls. Use disposable 64-KiB-cluster
 * images: a bundled HDD copy or generated replacement fixtures, never a
 * user's disk. */
#include "block/libretro-qcow2.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned reads, writes;
static int last_read_iov;
static unsigned scalar_reads;
static const void *last_write_base;
static unsigned zero_calls, scalar_writes;
static unsigned sync_calls, sync_failures;
static unsigned header_attempts, header_failures;
static bool watch_header;
static bool partial_header;

int counted_sync(LRFile *f)
{
    sync_calls++;
    if (sync_failures) {
        sync_failures--;
        return -EIO;
    }
    return lr_file_sync(f);
}

static void flush_retry(LRQcow2 *q, int64_t offset, size_t cs)
{
    /* After the transfer checks, release an allocated test cluster so even
     * in-place-only fixtures have pending metadata. Failed syncs must retry;
     * once a real sync succeeds, the next clean flush needs no host call. */
    assert(lr_qcow2_zero(q, offset, cs) == 0);
    sync_calls = 0;
    sync_failures = 2;
    assert(lr_qcow2_flush(q) == -EIO);
    assert(lr_qcow2_flush(q) == -EIO);
    assert(lr_qcow2_flush(q) == 0 && sync_calls == 3);
    assert(lr_qcow2_flush(q) == 0 && sync_calls == 3);
    puts("metadata flush: failed syncs retried, clean flush skipped: PASS");
}

int counted_zero(LRFile *f, int64_t off, int64_t len)
{
    zero_calls++;
    return lr_file_zero(f, off, len);
}

int counted_pwrite(LRFile *f, int64_t off, const void *buf, size_t len)
{
    scalar_writes++;
    if (watch_header && off == 88 && len == 8) {
        header_attempts++;
        if (header_failures) {
            header_failures--;
            if (partial_header) {
                assert(lr_file_pwrite(f, off, buf, 4) == 0);
            }
            return -EIO;
        }
    }
    return lr_file_pwrite(f, off, buf, len);
}

int counted_pread(LRFile *f, int64_t off, void *buf, size_t len)
{
    scalar_reads++;
    return lr_file_pread(f, off, buf, len);
}

int counted_preadv(LRFile *f, int64_t off, const LRFileIOV *iov, int n)
{
    reads++;
    last_read_iov = n;
    return lr_file_preadv(f, off, iov, n);
}

int counted_pwritev(LRFile *f, int64_t off, const LRFileIOV *iov, int n)
{
    writes++;
    last_write_base = n ? iov[0].base : NULL;
    return lr_file_pwritev(f, off, iov, n);
}

static void autoclear_retry(LRFile *f)
{
    char why[256];
    uint8_t block[65536], before[16], after[16], header[8];
    const uint8_t flags[8] = {0x80, 0, 0, 0, 0, 1, 0, 0};
    const uint8_t cleared[8] = {0};
    const uint8_t partial[8] = {0, 0, 0, 0, 0, 1, 0, 0};
    LRFileIOV whole = {block, sizeof(block)}, out = {before, sizeof(before)};
    LRQcow2 *q = lr_qcow2_open(f, true, why, sizeof(why));
    assert(q && lr_qcow2_size(q) > 32 * 1024 * 1024 + 65536);
    memset(block, 0x5a, sizeof(block));
    assert(lr_qcow2_pwritev(q, 0, &whole, 1) == 0); /* private allocated data */
    assert(lr_qcow2_zero(q, 32 * 1024 * 1024, 65536) == 0); /* new allocation */
    assert(lr_qcow2_flush(q) == 0);
    lr_qcow2_close(q);
    for (unsigned mode = 0; mode < 3; mode++) {
        assert(lr_file_pwrite(f, 88, flags, sizeof(flags)) == 0);
        q = lr_qcow2_open(f, true, why, sizeof(why));
        assert(q);
        int64_t offset = mode == 1 ? 32 * 1024 * 1024 : 0;
        assert(lr_qcow2_preadv(q, offset, &out, 1) == 0);
        header_attempts = 0;
        header_failures = 2;
        partial_header = mode == 2;
        watch_header = true;
        LRFileIOV empty = {NULL, 0}, input = {block, 16}, result = {after, 16};
        assert(lr_qcow2_pwritev(q, offset, &empty, 1) == 0);
        assert(lr_qcow2_zero(q, offset, 0) == 0 && header_attempts == 0);
        memset(block, 0xa5, 16);
        for (unsigned attempt = 0; attempt < 3; attempt++) {
            int ret = mode == 2 ? lr_qcow2_zero(q, offset, 16)
                               : lr_qcow2_pwritev(q, offset, &input, 1);
            assert(ret == (attempt < 2 ? -EIO : 0));
            assert(header_attempts == attempt + 1);
            assert(lr_file_pread(f, 88, header, 8) == 0);
            assert(!memcmp(header, attempt < 2 ? (partial_header ? partial : flags)
                                               : cleared, 8));
            assert(lr_qcow2_preadv(q, offset, &result, 1) == 0);
            if (attempt < 2) {
                assert(!memcmp(before, after, 16));
            } else {
                uint8_t zero[16] = {0};
                assert(!memcmp(after, mode == 2 ? zero : block, 16));
            }
        }
        assert(lr_qcow2_pwritev(q, offset, &input, 1) == 0);
        assert(header_attempts == 3); /* no redundant header write */
        watch_header = false;
        assert(lr_qcow2_flush(q) == 0);
        lr_qcow2_close(q);
    }
    puts("autoclear: in-place/allocation/zero retries, unchanged failed data, no-op I/O: PASS");
}

static void replacements(LRQcow2 *q, int64_t offset)
{
    uint8_t input[65536], output[65536], preserved[65536];
    LRFileIOV src[256], out = {output, sizeof(output)};
    for (unsigned i = 0; i < sizeof(input); i++) {
        input[i] = (uint8_t)(i * 17 + i / 251);
    }
    for (unsigned i = 0; i < 256; i++) {
        src[i] = (LRFileIOV) {input + i * 256, 256};
    }
    /* Prime a different compressed cluster: its cached bytes must be
     * invalidated when that cluster is replaced in the second iteration. */
    assert(lr_qcow2_preadv(q, offset + 65536, &out, 1) == 0);
    for (unsigned k = 0; k < 2; k++) {
        scalar_reads = reads = writes = 0;
        last_write_base = NULL;
        assert(lr_qcow2_pwritev(q, offset + k * 65536, src, 256) == 0);
        assert(scalar_reads == 0 && reads == 0 && writes == 1);
        assert(last_write_base == input); /* original spans, no scratch copy */
        assert(lr_qcow2_preadv(q, offset + k * 65536, &out, 1) == 0);
        assert(!memcmp(input, output, sizeof(input)));
        input[0] ^= 0x5a;
    }
    /* Partial replacement must preserve both sides, including after COW. */
    int64_t partial = offset + 2 * 65536;
    assert(lr_qcow2_preadv(q, partial, &out, 1) == 0);
    memcpy(preserved, output, sizeof(output));
    LRFileIOV fragment = {input, 13};
    memcpy(preserved + 37, input, 13);
    assert(lr_qcow2_pwritev(q, partial + 37, &fragment, 1) == 0);
    assert(lr_qcow2_preadv(q, partial, &out, 1) == 0);
    assert(!memcmp(preserved, output, sizeof(output)));
    puts("full replacements: no old reads, original source spans; partial preservation: PASS");
}

static void sparse_zero(LRQcow2 *q, size_t cs)
{
    uint8_t *input = malloc(cs), *output = malloc(cs);
    assert(input && output);
    LRFileIOV in = {input, cs}, out = {output, cs};
    /* A shared/compressed original must preserve both edges through COW. */
    assert(lr_qcow2_preadv(q, cs, &in, 1) == 0);
    assert(lr_qcow2_zero(q, cs + 13, cs - 29) == 0);
    memset(input + 13, 0, cs - 29);
    assert(lr_qcow2_preadv(q, cs, &out, 1) == 0);
    assert(!memcmp(input, output, cs));
    for (size_t i = 0; i < cs; i++) {
        input[i] = (uint8_t)(i * 17 + i / 251);
    }
    assert(lr_qcow2_pwritev(q, 0, &in, 1) == 0);
    zero_calls = scalar_writes = 0;
    assert(lr_qcow2_zero(q, 13, cs - 29) == 0);
    assert(zero_calls == 1 && scalar_writes == 0);
    memset(input + 13, 0, cs - 29);
    assert(lr_qcow2_preadv(q, 0, &out, 1) == 0);
    assert(!memcmp(input, output, cs));
    printf("partial zero: %zu-byte cluster, range calls=%u buffer writes=%u; edges preserved\n",
           cs, zero_calls, scalar_writes);
    for (size_t len = 4096; len <= 4097; len++) {
        memset(input, 0x5a, 4097);
        LRFileIOV fragment = {input, 4097}, result = {output, 4097};
        assert(lr_qcow2_pwritev(q, 8192, &fragment, 1) == 0);
        zero_calls = scalar_writes = 0;
        assert(lr_qcow2_zero(q, 8192, len) == 0);
        assert(zero_calls == (len == 4097) && scalar_writes == (len == 4096));
        memset(input, 0, len);
        assert(lr_qcow2_preadv(q, 8192, &result, 1) == 0);
        assert(!memcmp(input, output, 4097));
    }
    free(output);
    free(input);
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[2], "--autoclear")) {
        LRFile *f = lr_file_open(argv[1], true, false);
        assert(f);
        autoclear_retry(f);
        lr_file_close(f);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[2], "--make-raw")) {
        uint8_t data[65536];
        memset(data, 0x35, sizeof(data));
        FILE *raw = fopen(argv[1], "wb");
        assert(raw);
        for (unsigned i = 0; i < 3; i++) {
            data[i]++;
            assert(fwrite(data, 1, sizeof(data), raw) == sizeof(data));
        }
        assert(fclose(raw) == 0);
        return 0;
    }
    enum { SPANS = 130, PIECE = 256, BYTES = SPANS * PIECE };
    uint8_t input[BYTES], output[BYTES * 2], gap[PIECE], bits[4];
    LRFileIOV src[SPANS + 2] = {{0}}, dst[SPANS + 2] = {{0}};
    char why[256];
    const int64_t offset = 16 * 1024 * 1024;
    assert(argc == 2 || (argc == 3 &&
           (!strcmp(argv[2], "--replace") || !strcmp(argv[2], "--zero"))));
    LRFile *f = lr_file_open(argv[1], true, false);
    assert(f && lr_file_pread(f, 20, bits, sizeof(bits)) == 0);
    assert(!memcmp(bits, (uint8_t[]) {0, 0, 0, 16}, 4) ||
           (argc == 3 && !strcmp(argv[2], "--zero") &&
            !memcmp(bits, (uint8_t[]) {0, 0, 0, 21}, 4)));
    LRQcow2 *q = lr_qcow2_open(f, true, why, sizeof(why));
    if (argc == 3) {
        size_t cs = (size_t)1 << bits[3];
        assert(q && lr_qcow2_size(q) >= (int64_t)(3 * cs));
        if (!strcmp(argv[2], "--zero")) {
            sparse_zero(q, cs);
        } else {
            replacements(q, 0);
        }
        flush_retry(q, 0, cs);
        lr_qcow2_close(q);
        lr_file_close(f);
        return 0;
    }
    assert(q && lr_qcow2_size(q) >= offset + 65536 + BYTES);
    replacements(q, 32 * 1024 * 1024);
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
    zero_calls = scalar_writes = 0;
    assert(lr_qcow2_zero(q, crossing, BYTES) == 0);
    assert(zero_calls == 1 && scalar_writes == 1); /* 13-byte edge stays one write */
    assert(lr_qcow2_preadv(q, crossing, dst, SPANS + 2) == 0);
    memset(input, 0, sizeof(input));
    for (unsigned i = 0; i < SPANS; i++) {
        assert(!memcmp(output + i * PIECE * 2, input + i * PIECE, PIECE));
        assert(!memcmp(output + (i * 2 + 1) * PIECE, gap, PIECE));
    }
    flush_retry(q, offset, 65536);
    lr_qcow2_close(q);
    lr_file_close(f);
    puts("qcow2 bulk spans, bounded gaps, empty spans and cluster splits: PASS");
}
