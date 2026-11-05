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
static uint32_t read_word(void *opaque, int space, uint32_t addr)
{
    (void)opaque; (void)space;
    reads++;
    return words[addr];
}
static void write_word(void *opaque, int space, uint32_t addr, uint32_t value)
{
    (void)opaque; (void)space;
    writes++;
    words[addr] = value;
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
    /* Aliasing must retain the scalar read/write order in either direction,
     * including host addresses unaligned to DSP words and partial spans. */
    for (unsigned direction = 0; direction < 2; direction++) {
        for (unsigned size = 2; size <= 4; size += 2) {
            for (unsigned dst = 0; dst < 192; dst++) {
                uint32_t actual[128], expected[128];
                for (unsigned i = 0; i < 128; i++) actual[i] = expected[i] = 0xffa50000 | i;
                DmaTransfer t = { .mem = actual + 8, .item_size = size,
                    .item_mask = 0xffffff, .to_host = direction, .n = 2 };
                DmaTransfer r = t;
                r.mem = expected + 8;
                scalar(&r, (uint8_t *)expected + dst, 32 * size);
                dma_run(&t, (uint8_t *)actual + dst, 32 * size);
                if (memcmp(actual, expected, sizeof(actual)) || t.n != r.n) {
                    fprintf(stderr, "DMA overlap mismatch dir=%u size=%u offset=%u\n", direction, size, dst);
                    return 1;
                }
                cases++;
            }
        }
    }
    printf("DMA: %u scalar differential cases agree\n", cases);
    return 0;
}
