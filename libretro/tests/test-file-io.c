/* Exercise real libretro-common mapped views and the native fallback.
 * Optional second argument benchmarks reads from an existing read-only image. */
#include "block/libretro-file.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

static double seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart / frequency.QuadPart;
#else
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return t.tv_sec + t.tv_nsec * 1e-9;
#endif
}

int main(int argc, char **argv)
{
    assert(argc >= 2);
    const int64_t large = (int64_t)4 * 1024 * 1024 * 1024 + 8192;
    uint8_t block[65536], out[8192];
    memset(block, 0xa5, sizeof(block));
    LRFile *f = lr_file_open(argv[1], true, true);
    assert(f && !lr_file_view(f, 0, 1));
    assert(lr_file_set_size(f, large) == 0);
    assert(lr_file_pwrite(f, 0, block, sizeof(block)) == 0);
    assert(lr_file_pwrite(f, large - 4096, block, 4096) == 0);
    lr_file_close(f);
    f = lr_file_open(argv[1], false, false);
    assert(f && lr_file_size(f) == large);
    const uint8_t *view = lr_file_view(f, large - 4096, 4096);
    printf("mapped=%s\n", view ? "yes" : "no");
    if (view) {
        assert(!memcmp(view, block, 4096));
        assert(lr_file_pwrite(f, 0, block, 1) < 0);
        assert(lr_file_set_size(f, 0) < 0);
        assert(lr_file_zero(f, 0, 4096) < 0);
    }
    assert(!lr_file_view(f, -1, 1));
    assert(!lr_file_view(f, large - 1, 2));
    memset(out, 0xcc, sizeof(out));
    LRFileIOV iov[] = { {out, 1024}, {out + 2048, 3072} };
    assert(lr_file_preadv(f, large - 4096, iov, 2) == 0);
    assert(!memcmp(out, block, 1024));
    assert(!memcmp(out + 2048, block, 3072));
    assert(out[1024] == 0xcc && out[8191] == 0xcc);
    assert(lr_file_pread(f, large - 4, out, 16) == 0);
    assert(!memcmp(out, block, 4));
    assert(!memcmp(out + 4, (uint8_t[12]) {0}, 12));
    assert(lr_file_pread(f, large + 4096, out, sizeof(out)) == 0);
    assert(!memcmp(out, (uint8_t[8192]) {0}, sizeof(out)));
    lr_file_close(f);
    puts("positional/scatter reads, >4GiB offsets, EOF and write guards: PASS");
    if (argc > 2) {
        f = lr_file_open(argv[2], false, false);
        assert(f);
        uint64_t pages = (uint64_t)lr_file_size(f) / 4096;
        assert(pages);
        uint64_t sum = 0, rng = 1;
        LRFileIOV pieces[8];
        for (unsigned i = 0; i < 8; i++) {
            pieces[i].base = out + i * 1024;
            pieces[i].len = 512;
        }
        double begin = seconds();
        unsigned requests = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 10) : 100000;
        assert(requests && requests <= 1000000);
        for (unsigned i = 0; i < requests; i++) {
            /* Warm cache workload, limited to 16 MiB; no full-image read. */
            rng = rng * 6364136223846793005ULL + 1;
            uint64_t page = (rng >> 32) % (pages < 4096 ? pages : 4096);
            assert(lr_file_preadv(f, page * 4096, pieces, 8) == 0);
            sum += out[0] + out[7168];
        }
        printf("scatter_%u_ms=%.3f checksum=%llu\n", requests,
               (seconds() - begin) * 1000, (unsigned long long)sum);
        lr_file_close(f);
    }
    return 0;
}
