/* Real POSIX transfers, capped and interrupted at the syscall boundary. */
#define _GNU_SOURCE
#include "block/libretro-file.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

/* Linker wrapping remains effective when the compiler gives pread its native
 * assembler name. Fixture I/O explicitly bypasses the injected boundary. */
ssize_t __real_pread(int, void *, size_t, off_t);
ssize_t __real_pwrite(int, const void *, size_t, off_t);
ssize_t __real_preadv(int, const struct iovec *, int, off_t);
ssize_t __real_pwritev(int, const struct iovec *, int, off_t);
#define pread __real_pread
#define pwrite __real_pwrite
#define preadv __real_preadv
#define pwritev __real_pwritev

static size_t vector_limit, scalar_limit;
static off_t next_offset;
static int vector_intr, scalar_intr, vector_error, scalar_error, scalar_zero;
static unsigned vector_calls, scalar_calls, eof_calls;

static int fault(int *interrupt, int error)
{
    if (*interrupt) {
        --*interrupt;
        errno = EINTR;
        return 1;
    }
    if (error) {
        errno = error;
        return 1;
    }
    return 0;
}

static ssize_t vector_io(int fd, const struct iovec *iov, int count,
                         off_t off, int write)
{
    struct iovec spans[8];
    size_t left = vector_limit;
    int n = 0;
    vector_calls++;
    assert(off == next_offset && count <= 8 && !eof_calls);
    if (fault(&vector_intr, vector_error)) { return -1; }
    for (int i = 0; i < count && left; i++) {
        spans[n] = iov[i];
        if (spans[n].iov_len > left) { spans[n].iov_len = left; }
        left -= spans[n++].iov_len;
    }
    ssize_t got = write ? pwritev(fd, spans, n, off) : preadv(fd, spans, n, off);
    assert(got >= 0);
    next_offset += got;
    if (!write && !got) { eof_calls++; }
    return got;
}

ssize_t __wrap_preadv(int fd, const struct iovec *v, int n, off_t off)
{ return vector_io(fd, v, n, off, 0); }
ssize_t __wrap_pwritev(int fd, const struct iovec *v, int n, off_t off)
{ return vector_io(fd, v, n, off, 1); }

static ssize_t scalar_io(int fd, void *buf, size_t len, off_t off, int write)
{
    scalar_calls++;
    assert(off == next_offset && !eof_calls);
    if (fault(&scalar_intr, scalar_error)) { return -1; }
    if (scalar_zero) {
        if (!write) { eof_calls++; }
        return 0;
    }
    if (len > scalar_limit) { len = scalar_limit; }
    ssize_t got = write ? pwrite(fd, buf, len, off) : pread(fd, buf, len, off);
    assert(got >= 0);
    next_offset += got;
    if (!write && !got) { eof_calls++; }
    return got;
}
ssize_t __wrap_pread(int fd, void *p, size_t n, off_t off)
{ return scalar_io(fd, p, n, off, 0); }
ssize_t __wrap_pwrite(int fd, const void *p, size_t n, off_t off)
{ return scalar_io(fd, (void *)p, n, off, 1); }

static void reset(size_t vector, size_t scalar)
{
    vector_limit = vector; scalar_limit = scalar; next_offset = 3;
    vector_intr = scalar_intr = 1;
    vector_error = scalar_error = scalar_zero = 0;
    vector_calls = scalar_calls = eof_calls = 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    int fd = open(argv[1], O_CREAT | O_RDWR | O_TRUNC, 0600);
    assert(fd >= 0);
    LRFile *f = lr_file_open(argv[1], true, false);
    assert(f);
    uint8_t input[32], out[40], readback[40];
    for (unsigned i = 0; i < sizeof(input); i++) { input[i] = i * 7 + 1; }
    LRFileIOV v[] = {{NULL,0}, {out,5}, {NULL,0}, {out+8,7}, {out+24,9}, {NULL,0}};
    unsigned cases = 0;
    for (size_t cap = 1; cap <= 21; cap++) {
        for (size_t scalar = 1; scalar <= 7; scalar++) {
            for (size_t length = 0; length <= 21; length++) {
                assert(ftruncate(fd, 3 + length) == 0);
                assert(pwrite(fd, input, length, 3) == (ssize_t)length);
                memset(out, 0xcc, sizeof(out)); reset(cap, scalar);
                assert(lr_file_preadv(f, 3, v, 6) == 0);
                if (next_offset != (off_t)(3 + length) || eof_calls > 1) {
                    fprintf(stderr, "cap=%zu scalar=%zu length=%zu next=%lld eof=%u\n", cap, scalar, length, (long long)next_offset, eof_calls);
                    assert(0);
                }
                for (unsigned i = 0; i < 21; i++) {
                    unsigned dest = i < 5 ? i : i < 12 ? i + 3 : i + 12;
                    assert(out[dest] == (i < length ? input[i] : 0));
                }
                assert(out[5] == 0xcc && out[15] == 0xcc && out[39] == 0xcc);
                cases++;
            }
            memset(out, 0xa5, sizeof(out)); reset(cap, scalar);
            assert(ftruncate(fd, 0) == 0);
            assert(lr_file_pwritev(f, 3, v, 6) == 0);
            assert(next_offset == 24 && vector_calls == 2);
            assert(pread(fd, readback, 21, 3) == 21);
            for (unsigned i = 0; i < 21; i++) { assert(readback[i] == 0xa5); }
            cases++;
        }
    }
    /* Terminal errors at the vector boundary and after a transferred prefix. */
    assert(pwrite(fd, input, 21, 3) == 21);
    for (int write = 0; write <= 1; write++) {
        for (int partial = 0; partial <= 1; partial++) {
            reset(3, 2); memset(out, 0xcc, sizeof(out));
            if (partial) { scalar_error = ENOSPC; } else { vector_error = EIO; }
            int ret = write ? lr_file_pwritev(f, 3, v, 6) : lr_file_preadv(f, 3, v, 6);
            assert(ret == (partial ? -ENOSPC : -EIO));
            assert(next_offset == (partial ? 6 : 3));
            assert(vector_calls == 2 && scalar_calls == (partial ? 2 : 0));
            if (!write) { assert(out[3] == 0xcc && out[24] == 0xcc); }
            cases++;
        }
    }
    /* No-progress vector writes can complete through the scalar fallback.
     * A no-progress scalar write must instead terminate with an I/O error. */
    reset(0, 2);
    assert(lr_file_pwritev(f, 3, v, 6) == 0 && next_offset == 24);
    cases++;
    for (int write = 0; write <= 1; write++) {
        reset(3, 2); scalar_zero = 1; memset(out, 0xcc, sizeof(out));
        int ret = write ? lr_file_pwritev(f, 3, v, 6) : lr_file_preadv(f, 3, v, 6);
        assert(ret == (write ? -EIO : 0));
        assert(next_offset == 6 && scalar_calls == 2);
        if (!write) { assert(out[3] == 0 && out[24] == 0 && out[5] == 0xcc); }
        cases++;
    }
    lr_file_close(f); close(fd);
    printf("native partial transfers: %u short/EINTR/EOF/error cases PASS\n", cases);
}
