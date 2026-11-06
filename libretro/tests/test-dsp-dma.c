/* DMA run differential tests: compare against the original scalar ordering. */
#include "hw/xbox/mcpx/apu/dsp/dsp_dma.c"

void g_assertion_message_expr(const char *domain, const char *file, int line,
                              const char *func, const char *expr)
{
    (void)domain;
    fprintf(stderr, "%s:%d: %s: %s\n", file, line, func, expr ? expr : "");
    abort();
}

static uint32_t words[128];
static unsigned reads, writes;
static bool mutate_reads, mutate_writes;
static uint32_t callback_addr[128], callback_value[128];
static int callback_space[128];
static void note_callback(int space, uint32_t addr, uint32_t value)
{
    unsigned n = reads + writes;
    assert(n < 128);
    callback_addr[n] = addr;
    callback_value[n] = value;
    callback_space[n] = space;
}
static uint32_t read_word(void *opaque, int space, uint32_t addr)
{
    (void)opaque; (void)space;
    uint32_t value = words[addr];
    note_callback(space, addr, value);
    reads++;
    if (mutate_reads) words[(addr + 1) % 128] ^= value >> 3;
    return value;
}
static void write_word(void *opaque, int space, uint32_t addr, uint32_t value)
{
    (void)opaque; (void)space;
    note_callback(space, addr, value);
    writes++;
    words[addr] = value;
    if (mutate_writes) words[(addr + 1) % 128] ^= value >> 1;
}

static void scalar(DmaTransfer *t, uint8_t *host, size_t bytes)
{
    size_t items = bytes / t->item_size;
    for (size_t i = 0; i < items; i++) {
        if (t->zero_aperture) {
            if (t->to_host) memset(host + i * t->item_size, 0, t->item_size);
            continue;
        }
        if (t->to_host) {
            uint32_t v = dma_word_read(t, t->n + i);
            if (t->item_size == 2) {
                uint16_t half = v >> 8;
                memcpy(host + i * 2, &half, 2);
            } else memcpy(host + i * 4, &v, 4);
        } else {
            uint32_t v;
            if (t->item_size == 2) {
                uint16_t half;
                memcpy(&half, host + i * 2, 2);
                v = (uint32_t)half << 8;
            } else {
                memcpy(&v, host + i * 4, 4);
                v &= t->item_mask;
            }
            dma_word_write(t, t->n + i, v);
        }
    }
    t->n += items;
}

int main(void)
{
    unsigned cases = 0;
    DSPDMAState s = { .mem_read = read_word, .mem_write = write_word };
    for (unsigned mode = 0; mode < 32; mode++) {
        for (unsigned size = 2; size <= 4; size += 2) {
            for (unsigned offset = 0; offset < 8; offset++) {
                for (unsigned count = 0; count <= 64; count++) {
                    uint32_t initial[128], expected[128];
                    uint8_t out[528], ref[528];
                    for (unsigned i = 0; i < 128; i++) initial[i] = 0xfedcba98u ^ (i * 0x1234567u);
                    for (unsigned i = 0; i < sizeof(out); i++) out[i] = ref[i] = (i * 37) ^ 0xa5;
                    memcpy(words, initial, sizeof(words));
                    DmaTransfer t = { .s = &s, .addr = 3, .mem = (mode & 1) ? words + 3 : NULL,
                        .item_size = size, .item_mask = 0xffffff, .to_host = !!(mode & 2),
                        .interleave = !!(mode & 4), .zero_aperture = !!(mode & 8),
                        .channels = 4, .blocks = 24, .n = (mode & 16) ? 7 : 0 };
                    DmaTransfer r = t;
                    reads = writes = 0;
                    scalar(&r, ref + offset, count * size);
                    unsigned ref_reads = reads, ref_writes = writes;
                    memcpy(expected, words, sizeof(words));
                    memcpy(words, initial, sizeof(words));
                    reads = writes = 0;
                    /* Scatter-gather boundaries must preserve the item cursor. */
                    unsigned split = count / 3;
                    dma_run(&t, out + offset, split * size);
                    dma_run(&t, out + offset + split * size, (count - split) * size);
                    if (memcmp(out, ref, sizeof(out)) || memcmp(words, expected, sizeof(words)) ||
                        t.n != r.n || reads != ref_reads || writes != ref_writes) {
                        fprintf(stderr, "DMA mismatch mode=%u size=%u offset=%u count=%u\n", mode, size, offset, count);
                        return 1;
                    }
                    cases++;
                }
            }
        }
    }
    /* Every channel count and resume point, for direct and callback memory.
     * Callback addresses, spaces, values and order must exactly match. */
    for (unsigned callback = 0; callback < 3; callback++) {
        mutate_reads = callback == 2;
        for (unsigned channels = 1; channels <= 16; channels++) {
            for (unsigned blocks = 1; blocks <= 7; blocks++) {
                unsigned total = channels * blocks;
                for (unsigned start = 0; start <= total; start++) {
                    for (unsigned size = 2; size <= 4; size += 2) {
                        uint8_t out[512], ref[512];
                        uint32_t expected_addr[128], expected_value[128], expected_memory[128];
                        int expected_space[128];
                        memset(ref, 0xa5, sizeof(ref));
                        for (unsigned i = 0; i < 128; i++) words[i] = 0xfedcba98u ^ (i * 0x1234567u);
                        DmaTransfer t = { .s = &s, .space = DSP_SPACE_P, .addr = 3,
                            .mem = callback ? NULL : words + 3, .item_size = size, .to_host = true,
                            .interleave = true, .channels = channels, .blocks = blocks, .n = start };
                        DmaTransfer r = t;
                        reads = writes = 0;
                        scalar(&r, ref + 1, (total - start) * size);
                        unsigned expected_reads = reads;
                        memcpy(expected_memory, words, sizeof(words));
                        memcpy(expected_addr, callback_addr, sizeof(expected_addr));
                        memcpy(expected_value, callback_value, sizeof(expected_value));
                        memcpy(expected_space, callback_space, sizeof(expected_space));
                        for (unsigned split = 0; split < 2; split++) {
                            t.n = start;
                            for (unsigned i = 0; i < 128; i++) words[i] = 0xfedcba98u ^ (i * 0x1234567u);
                            reads = writes = 0;
                            memset(out, 0xa5, sizeof(out));
                            if (split) {
                                for (unsigned n = start; n < total; n++) {
                                    dma_run(&t, out + 1 + (n - start) * size, size);
                                    dma_run(&t, out + 1 + (n - start + 1) * size, 0);
                                }
                            } else dma_run(&t, out + 1, (total - start) * size);
                            if (memcmp(out, ref, sizeof(out)) || memcmp(words, expected_memory, sizeof(words)) ||
                                t.n != r.n || reads != expected_reads || writes ||
                                memcmp(callback_addr, expected_addr, reads * sizeof(*callback_addr)) ||
                                memcmp(callback_value, expected_value, reads * sizeof(*callback_value)) ||
                                memcmp(callback_space, expected_space, reads * sizeof(*callback_space))) {
                                fprintf(stderr, "DMA channel mismatch callback=%u channels=%u blocks=%u start=%u split=%u\n",
                                        callback, channels, blocks, start, split);
                                return 1;
                            }
                        }
                        cases++;
                    }
                }
            }
        }
    }
    /* Sequential device/program callbacks: exact trace, masks, shifts and
     * side effects must survive full versus per-item host runs. */
    for (unsigned space = 0; space < 3; space++) {
        for (unsigned direction = 0; direction < 2; direction++) {
            for (unsigned size = 2; size <= 4; size += 2) {
                for (unsigned effects = 0; effects < 2; effects++) {
                    mutate_reads = mutate_writes = effects;
                    for (unsigned start = 0; start <= 7; start += 7) {
                        for (unsigned offset = 0; offset < 8; offset++) {
                            for (unsigned count = 0; count <= 64; count++) {
                                uint32_t expected_memory[128], expected_addr[128], expected_value[128];
                                int expected_space[128];
                                uint8_t out[512], ref[512];
                                for (unsigned i = 0; i < 128; i++) words[i] = 0xfedcba98u ^ (i * 0x1234567u);
                                for (unsigned i = 0; i < sizeof(ref); i++) ref[i] = (i * 37) ^ 0xa5;
                                DmaTransfer t = { .s = &s, .space = space, .addr = 3, .item_size = size,
                                    .item_mask = 0xffffff, .to_host = direction, .n = start };
                                DmaTransfer r = t;
                                reads = writes = 0;
                                scalar(&r, ref + offset, count * size);
                                unsigned expected_reads = reads, expected_writes = writes;
                                memcpy(expected_memory, words, sizeof(words));
                                memcpy(expected_addr, callback_addr, sizeof(expected_addr));
                                memcpy(expected_value, callback_value, sizeof(expected_value));
                                memcpy(expected_space, callback_space, sizeof(expected_space));
                                for (unsigned split = 0; split < 2; split++) {
                                    for (unsigned i = 0; i < 128; i++) words[i] = 0xfedcba98u ^ (i * 0x1234567u);
                                    for (unsigned i = 0; i < sizeof(out); i++) out[i] = (i * 37) ^ 0xa5;
                                    t.n = start;
                                    reads = writes = 0;
                                    if (split) {
                                        for (unsigned i = 0; i < count; i++) dma_run(&t, out + offset + i * size, size);
                                        dma_run(&t, out + offset + count * size, 0);
                                    } else dma_run(&t, out + offset, count * size);
                                    unsigned events = reads + writes;
                                    if (memcmp(out, ref, sizeof(out)) || memcmp(words, expected_memory, sizeof(words)) ||
                                        t.n != r.n || reads != expected_reads || writes != expected_writes ||
                                        memcmp(callback_addr, expected_addr, events * sizeof(*callback_addr)) ||
                                        memcmp(callback_value, expected_value, events * sizeof(*callback_value)) ||
                                        memcmp(callback_space, expected_space, events * sizeof(*callback_space))) {
                                        fprintf(stderr, "DMA sequential callback mismatch space=%u direction=%u size=%u start=%u count=%u\n",
                                                space, direction, size, start, count);
                                        return 1;
                                    }
                                }
                                cases++;
                            }
                        }
                    }
                }
            }
        }
    }
    /* Aliasing must retain the scalar read/write order in either direction,
     * including host addresses unaligned to DSP words and partial spans. */
    for (unsigned mode = 0; mode < 4; mode++) {
        for (unsigned size = 2; size <= 4; size += 2) {
            for (unsigned dst = 0; dst < 192; dst++) {
                uint32_t actual[128], expected[128];
                for (unsigned i = 0; i < 128; i++) actual[i] = expected[i] = 0xffa50000 | i;
                DmaTransfer t = { .mem = actual + 8, .item_size = size,
                    .item_mask = 0xffffff, .to_host = mode & 1, .n = 2,
                    .interleave = !!(mode & 2), .channels = 4, .blocks = 24 };
                DmaTransfer r = t;
                r.mem = expected + 8;
                scalar(&r, (uint8_t *)expected + dst, 32 * size);
                dma_run(&t, (uint8_t *)actual + dst, 32 * size);
                if (memcmp(actual, expected, sizeof(actual)) || t.n != r.n) {
                    fprintf(stderr, "DMA overlap mismatch dir=%u size=%u offset=%u\n", mode, size, dst);
                    return 1;
                }
                cases++;
            }
        }
    }
    printf("DMA: %u scalar differential cases agree\n", cases);
    return 0;
}
