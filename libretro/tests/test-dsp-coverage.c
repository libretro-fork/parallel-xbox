/* SPDX-License-Identifier: GPL-2.0-or-later */
int dspdiff_main(int argc, char **argv);
#define main dspdiff_main
#include "dspdiff.c"
#undef main
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#define WORDS ((DSP_PRAM_SIZE + 63) / 64)
typedef struct Bitmap {
    uint64_t before, bits[WORDS], after;
} Bitmap;
static uint64_t cases;
static void scalar(uint64_t *bits, uint32_t start, uint32_t end)
{
    for (uint32_t pc = start; pc < MIN(end, DSP_PRAM_SIZE); pc++)
        bits[pc >> 6] |= UINT64_C(1) << (pc & 63);
}
static void verify(const uint64_t *initial, const uint64_t *expected, uint64_t expected_mask, uint32_t start, uint32_t end)
{
    Bitmap actual = { .before = UINT64_C(0x123456789abcdef0), .after = UINT64_C(0xfedcba9876543210) };
    memcpy(actual.bits, initial, sizeof(actual.bits));
    uint64_t mask = dspjit_test_cover_span_mask(actual.bits, start, end);
    if (actual.before != UINT64_C(0x123456789abcdef0) || actual.after != UINT64_C(0xfedcba9876543210) ||
        memcmp(actual.bits, expected, sizeof(actual.bits)) || mask != expected_mask) {
        fprintf(stderr,"DSP coverage mismatch: start=%u end=%u\n",start,end);
        exit(1);
    }
    cases++;
}
static uint64_t scalar_mask(uint32_t start, uint32_t end)
{
    uint64_t mask = 0;
    for (uint32_t pc = start; pc < MIN(end, DSP_PRAM_SIZE); pc++) mask |= UINT64_C(1) << (pc >> 6);
    return mask;
}
static double seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER tick, frequency;
    QueryPerformanceCounter(&tick);
    QueryPerformanceFrequency(&frequency);
    return (double)tick.QuadPart / frequency.QuadPart;
#else
    struct timespec tick;
    clock_gettime(CLOCK_MONOTONIC, &tick);
    return tick.tv_sec + tick.tv_nsec * 1e-9;
#endif
}
static void (*volatile mark)(uint64_t *, uint32_t, uint32_t);
static void benchmark(void)
{
    for (unsigned trial = 0; trial < 7; trial++) {
        uint64_t results[2][WORDS];
        double elapsed[2];
        for (unsigned order = 0; order < 2; order++) {
            unsigned which = order ^ (trial & 1);
            memset(results[which], 0, sizeof(results[which]));
            mark = which ? dspjit_test_cover_span : scalar;
            double begin = seconds();
            for (unsigned i = 0; i < 1000000; i++) mark(results[which], i & 63, (i & 63) + 128);
            elapsed[which] = seconds() - begin;
        }
        if (memcmp(results[0], results[1], sizeof(results[0]))) exit(1);
        printf("coverage trial %u: scalar %.9f, masks %.9f seconds; bitmaps agree\n",trial,elapsed[0],elapsed[1]);
    }
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--bench")) { benchmark(); return 0; }
    if (argc != 1) return 2;
    uint64_t zero[WORDS] = {0}, expected[WORDS], initial[WORDS];
    /* Exhaust every start/end pair within PRAM, including inverted ranges.
     * Extend the scalar oracle by exactly one word as the endpoint advances. */
    for (uint32_t start = 0; start <= DSP_PRAM_SIZE; start++) {
        memset(expected, 0, sizeof(expected));
        uint64_t mask = 0;
        for (uint32_t end = 0; end <= DSP_PRAM_SIZE; end++) {
            if (end > start) {
                expected[(end - 1) >> 6] |= UINT64_C(1) << ((end - 1) & 63);
                mask |= UINT64_C(1) << ((end - 1) >> 6);
            }
            verify(zero, expected, mask, start, end);
        }
    }
    /* Preserve prior coverage and test overlaps at every possible short-block
     * alignment, with byte-order-independent deterministic initial bits. */
    for (uint32_t start = 0; start <= DSP_PRAM_SIZE; start++) {
        for (unsigned i = 0; i < WORDS; i++)
            initial[i] = UINT64_C(0x0123456789abcdef) ^ (UINT64_C(0x9e3779b97f4a7c15) * (start + i));
        for (unsigned n = 0; n <= 129; n++) {
            memcpy(expected, initial, sizeof(expected));
            scalar(expected, start, start + n);
            verify(initial, expected, scalar_mask(start, start + n), start, start + n);
        }
        memcpy(expected, initial, sizeof(expected));
        scalar(expected, start, UINT32_MAX);
        verify(initial, expected, scalar_mask(start, UINT32_MAX), start, UINT32_MAX);
    }
    static const uint32_t edge[] = {0,1,63,64,65,4095,4096,4097,UINT32_MAX-1,UINT32_MAX};
    for (unsigned a = 0; a < sizeof(edge)/sizeof(*edge); a++) {
        for (unsigned b = 0; b < sizeof(edge)/sizeof(*edge); b++) {
            memcpy(expected, initial, sizeof(expected));
            scalar(expected, edge[a], edge[b]);
            verify(initial, expected, scalar_mask(edge[a], edge[b]), edge[a], edge[b]);
        }
    }
    printf("DSP coverage: %" PRIu64 " scalar bitmap/mask comparisons agree\n",cases);
    return 0;
}
