/* Production backend, including its immutable mapped lane without a VFS SDK.
 * Only the small mapped byte view is modeled; native file I/O is real. */
#define LR_FILE_FILESTREAM 0
#ifndef FILE_VECTOR_SOURCE
#define FILE_VECTOR_SOURCE "block/libretro-file.c"
#endif
#include FILE_VECTOR_SOURCE
#include <assert.h>
#include <stdio.h>
#include <time.h>

static void rejected_scalars(LRFile *f, bool writable)
{
    uint8_t out[32];
    memset(out, 0xcc, sizeof(out));
    assert(lr_file_pread(f, -1, out, 0) == -EINVAL);
    assert(lr_file_pread(f, INT64_MAX, out, 1) == -EINVAL);
    assert(lr_file_pread(f, INT64_MAX - 15, out, 16) == -EINVAL);
    if (SIZE_MAX > INT64_MAX) {
        assert(lr_file_pread(f, 0, out, SIZE_MAX) == -EINVAL);
    }
    assert(lr_file_pread(f, INT64_MAX, NULL, 0) == 0);
    for (unsigned i = 0; i < sizeof(out); i++) { assert(out[i] == 0xcc); }
    if (writable) {
        assert(lr_file_pwrite(f, -1, out, 0) == -EINVAL);
        assert(lr_file_pwrite(f, INT64_MAX, out, 1) == -EINVAL);
        assert(lr_file_pwrite(f, INT64_MAX - 15, out, 16) == -EINVAL);
        if (SIZE_MAX > INT64_MAX) {
            assert(lr_file_pwrite(f, 0, out, SIZE_MAX) == -EINVAL);
        }
        assert(lr_file_pwrite(f, INT64_MAX, NULL, 0) == 0);
    } else {
        assert(lr_file_pwrite(f, INT64_MAX, out, 1) == -EPERM);
    }
}

static void rejected_reads(LRFile *f)
{
    uint8_t out[32];
    LRFileIOV overflow[] = {{out, 1}, {out + 16, SIZE_MAX}};
    LRFileIOV empty[] = {{NULL, 0}, {out, 0}};
    memset(out, 0xcc, sizeof(out));
    if (SIZE_MAX > INT64_MAX) {
        assert(lr_file_preadv(f, 0, overflow, 2) == -EINVAL);
    }
    assert(lr_file_preadv(f, INT64_MAX - 1, overflow, 2) == -EINVAL);
    assert(lr_file_preadv(f, 0, NULL, -1) == -EINVAL);
    assert(lr_file_preadv(f, 0, NULL, 1) == -EINVAL);
    assert(lr_file_preadv(f, -1, NULL, 0) == -EINVAL);
    assert(lr_file_preadv(f, INT64_MAX, NULL, 0) == 0);
    assert(lr_file_preadv(f, 0, empty, 2) == 0);
    for (unsigned i = 0; i < sizeof(out); i++) { assert(out[i] == 0xcc); }
}

static uint32_t random_state = 0x271af329;
static unsigned random_word(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void mapped_oracle(void)
{
    uint8_t source[2048], actual[8192], expected[8192];
    for (unsigned j = 0; j < sizeof(source); j++) source[j] = (uint8_t)(j * 29 + j / 17);
    for (unsigned trial = 0; trial < 20000; trial++) {
        LRFile mapped = {.mapped = source, .mapped_size = random_word() % (sizeof(source) + 1)};
        LRFileIOV pieces[32];
        int n = random_word() % 33;
        int64_t offset = random_word() % 4097;
        size_t place = 0, consumed = 0;
        memset(actual, 0xcc, sizeof(actual));
        memset(expected, 0xcc, sizeof(expected));
        for (int i = 0; i < n; i++) {
            size_t len = random_word() % 129;
            place += random_word() % 9;
            pieces[i] = (LRFileIOV){len ? actual + place : NULL, len};
            for (size_t j = 0; j < len; j++) {
                uint64_t at = (uint64_t)offset + consumed + j;
                expected[place + j] = at < (uint64_t)mapped.mapped_size ? source[at] : 0;
            }
            place += len;
            consumed += len;
        }
        assert(!lr_file_preadv(&mapped, offset, pieces, n));
        assert(!memcmp(actual, expected, sizeof(actual)));
    }
    puts("mapped file vectors: 20000 empty/adjacent/gapped/EOF requests and untouched guards PASS");
}

#ifdef _WIN32
static double now_ns(void) { LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return (double)t.QuadPart * 1e9 / f.QuadPart; }
#else
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }
#endif
static uint8_t bench_source[65536], bench_destination[131072];
static void benchmark(void)
{
    for (unsigned j = 0; j < sizeof(bench_source); j++) bench_source[j] = (uint8_t)(j * 29 + j / 17);
    LRFile mapped = {.mapped = bench_source, .mapped_size = sizeof(bench_source)};
    int (*volatile readv_fn)(LRFile *,int64_t,const LRFileIOV *,int) = lr_file_preadv;
    for (unsigned layout = 0; layout < 3; layout++) for (unsigned eof = 0; eof < 2; eof++) {
        const unsigned sizes[] = {512, 2048, 65536};
        for (unsigned k = 0; k < 3; k++) {
            unsigned size = sizes[k], loops = size == 65536 ? 20000 : 500000;
            int n = layout ? 16 : 1;
            LRFileIOV pieces[16];
            for (int i = 0; i < n; i++) pieces[i] = (LRFileIOV){bench_destination + i * (size / n + (layout == 2 ? 16 : 0)), size / n};
            int64_t offset = eof ? sizeof(bench_source) - size / 2 : 0;
            assert(!readv_fn(&mapped, offset, pieces, n));
            int ret = 0; double begin = now_ns();
            for (unsigned j = 0; j < loops; j++) {
                ret |= readv_fn(&mapped, offset, pieces, n);
                __asm__ __volatile__("" : : "r"(bench_destination) : "memory");
            }
            assert(!ret);
            printf("layout=%u eof=%u size=%u ns=%.3f\n",layout,eof,size,(now_ns()-begin)/loops);
        }
    }
}

#ifdef FILE_VECTOR_SHARED
#ifdef _WIN32
__declspec(dllexport)
#endif
int file_vector_fixture(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif

{
    if (argc > 1 && !strcmp(argv[1], "--bench")) { benchmark(); return 0; }
    assert(argc == 2);
    mapped_oracle();
    uint8_t bytes[64], out[64], source[64];
    memset(bytes, 0x5a, sizeof(bytes));
    memset(source, 0xa5, sizeof(source));
    LRFile mapped = {.mapped = bytes, .mapped_size = sizeof(bytes)};
    rejected_reads(&mapped);
    rejected_scalars(&mapped, false);
    LRFileIOV v[] = {{out, 8}, {out + 16, 8}};
    assert(lr_file_preadv(&mapped, 0, v, 2) == 0);
    assert(!memcmp(out, bytes, 8) && !memcmp(out + 16, bytes + 8, 8));
    assert(lr_file_pwritev(&mapped, 0, NULL, 0) == -EPERM);

    LRFile *f = lr_file_open(argv[1], true, true);
    assert(f && lr_file_set_size(f, sizeof(bytes)) == 0);
    assert(lr_file_pwrite(f, 0, bytes, sizeof(bytes)) == 0);
    rejected_reads(f);
    rejected_scalars(f, true);
    /* Force the non-preadv fallback as well as the native vectored lane. */
    LRFileIOV invalid[1300] = {{source, 1}, {source + 16, SIZE_MAX}};
    for (int count = 2; count <= 1300; count += 1298) {
        int64_t offset = SIZE_MAX > INT64_MAX ? 0 : INT64_MAX - 1;
        assert(lr_file_pwritev(f, offset, invalid, count) == -EINVAL);
        memset(out, 0xcc, sizeof(out));
        invalid[0].base = out;
        invalid[1].base = out + 16;
        assert(lr_file_preadv(f, offset, invalid, count) == -EINVAL);
        for (unsigned i = 0; i < sizeof(out); i++) { assert(out[i] == 0xcc); }
        invalid[0].base = source;
        invalid[1].base = source + 16;
    }
    assert(lr_file_pwritev(f, 0, NULL, -1) == -EINVAL);
    assert(lr_file_pwritev(f, 0, NULL, 1) == -EINVAL);
    assert(lr_file_pwritev(f, -1, NULL, 0) == -EINVAL);
    assert(lr_file_pwritev(f, INT64_MAX, NULL, 0) == 0);
    assert(lr_file_size(f) == sizeof(bytes));
    assert(lr_file_pread(f, 0, out, sizeof(out)) == 0);
    assert(!memcmp(out, bytes, sizeof(bytes)));
    LRFileIOV valid[] = {{source, 7}, {source + 16, 9}, {NULL, 0}};
    assert(lr_file_pwritev(f, 0, valid, 3) == 0);
    memset(out, 0xcc, sizeof(out));
    assert(lr_file_preadv(f, 56, v, 2) == 0);
    assert(!memcmp(out, bytes, 8));
    assert(!memcmp(out + 16, (uint8_t[8]){0}, 8));
    assert(out[8] == 0xcc);
    /* Exercise the same empty-separated contiguous run in mapped and native
     * fallbacks, including Windows where native preadv is unavailable. */
    LRFileIOV many[1300] = {{NULL,0}}, saved[1300];
    many[1] = (LRFileIOV){out,5};
    many[650] = (LRFileIOV){out+5,7};
    many[900] = (LRFileIOV){out+37,0};
    many[1298] = (LRFileIOV){out+12,9};
    memcpy(saved, many, sizeof(many));
    memset(out, 0xa5, sizeof(out));
    assert(lr_file_pwritev(f, 0, many, 1300) == 0);
    memset(out, 0xcc, sizeof(out));
    assert(lr_file_preadv(f, 0, many, 1300) == 0);
    for (unsigned i = 0; i < 21; i++) { assert(out[i] == 0xa5); }
    assert(out[21] == 0xcc && out[63] == 0xcc);
    memset(out, 0xcc, sizeof(out));
    assert(lr_file_preadv(&mapped, 56, many, 1300) == 0);
    for (unsigned i = 0; i < 21; i++) { assert(out[i] == (i < 8 ? 0x5a : 0)); }
    assert(out[21] == 0xcc && out[63] == 0xcc);
    assert(!memcmp(saved, many, sizeof(many)));
    lr_file_close(f);
    puts("file scalar/vector ranges: preflight preserves mapped/native destinations and native files: PASS");
    return 0;
}
