/* SPDX-License-Identifier: GPL-2.0-or-later
 * Isolated production conversion benchmark; no ownership/walker timing. */
#ifndef DSP_DMA_SOURCE
#define DSP_DMA_SOURCE "hw/xbox/mcpx/apu/dsp/dsp_dma.c"
#endif
#include DSP_DMA_SOURCE
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

void g_assertion_message_expr(const char *domain, const char *file, int line,
                             const char *func, const char *expr)
{
    (void)domain;
    fprintf(stderr, "%s:%d: %s: %s\n", file, line, func, expr ? expr : "");
    abort();
}

static uint32_t memory[4096], host[4096];
static uint32_t read_word(void *opaque, int space, uint32_t addr)
{
    (void)opaque; (void)space;
    return memory[addr];
}
static void write_word(void *opaque, int space, uint32_t addr, uint32_t value)
{
    (void)opaque; (void)space;
    memory[addr] = value;
}
static double now(void)
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
static uint64_t hash_bytes(const void *data, size_t bytes, uint64_t hash)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < bytes; i++) hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}
/* A volatile dispatch prevents dead-store elimination across repeated calls.
 * Both revisions use the identical driver, inputs and dispatch overhead. */
static void (*volatile transfer)(void *, uint8_t *, size_t) = dma_run;

int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    unsigned mode = strtoul(argv[1], NULL, 10);
    unsigned count = strtoul(argv[2], NULL, 10);
    unsigned iterations = strtoul(argv[3], NULL, 10);
    unsigned seed = strtoul(argv[4], NULL, 10);
    if (mode >= 10 || !count || count > 2048 || !iterations) return 2;
    for (unsigned i = 0; i < 4096; i++) {
        memory[i] = 0xfedcba98u ^ (i * 0x1234567u) ^ seed;
        host[i] = 0xabcdfedcu ^ (i * 0x7654321u) ^ seed;
    }
    DSPDMAState s = { .mem_read = read_word, .mem_write = write_word };
    bool callback = mode >= 6;
    bool interleave = mode == 4 || mode == 5 || mode == 8 || mode == 9;
    bool output = interleave || mode == 1 || mode == 3 || mode == 6;
    unsigned width = (mode == 0 || mode == 1 || mode == 4 || mode == 8) ? 2 : 4;
    DmaTransfer t = { .s = &s, .mem = callback ? NULL : memory, .item_size = width,
        .item_mask = 0xffffff, .to_host = output, .interleave = interleave,
        .channels = 6, .blocks = 512, .n = 5 };
    uint8_t *bytes = (uint8_t *)host + 1;
    /* Warm code/data; restore inputs before the measured repetitions. */
    transfer(&t, bytes, count * width);
    for (unsigned i = 0; i < 4096; i++) {
        memory[i] = 0xfedcba98u ^ (i * 0x1234567u) ^ seed;
        host[i] = 0xabcdfedcu ^ (i * 0x7654321u) ^ seed;
    }
    double start = now();
    for (unsigned i = 0; i < iterations; i++) {
        t.n = 5;
        transfer(&t, bytes, count * width);
    }
    double elapsed = now() - start;
    uint64_t hash = hash_bytes(memory, sizeof(memory), UINT64_C(14695981039346656037));
    hash = hash_bytes(host, sizeof(host), hash);
    printf("%.9f %016" PRIx64 " %u\n", elapsed, hash, t.n);
    return t.n != 5 + count;
}
