/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_inline.h>
#include <retro_spsc.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while (0)


static unsigned tail_reads;
static size_t observed_acquire(retro_atomic_size_t *p)
{
    tail_reads++;
    return retro_atomic_load_acquire_size(p);
}
#undef retro_atomic_load_acquire_size
#define retro_atomic_load_acquire_size(p) observed_acquire(p)
/* OBSERVED RESERVE */
#undef retro_atomic_load_acquire_size

static uint32_t rng = 0x923dc781;
static uint32_t next(void) {rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;}
static void cases(bool mirrored)
{
    retro_spsc_t q;
    assert(mirrored ? retro_spsc_init_mirrored(&q, 128) : retro_spsc_init(&q, 128));
    uint8_t payload[256], expected[256], actual[256];
    uint8_t *snapshot = malloc(q.mask + 1);
    assert(snapshot);
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)next();
    for (unsigned trial = 0; trial < 100000; trial++) {
        size_t start = trial & 1 ? SIZE_MAX - next() % 512 : next();
        size_t occupied = next() % 129;
        size_t need = trial % 31 ? next() % 140 : SIZE_MAX;
        size_t stale = next() % (129 - occupied);
        for (unsigned observed = 0; observed < 2; observed++) {
            retro_atomic_size_init(&q.head, start);
            retro_atomic_size_init(&q.tail, start);
            q.cached_head = q.cached_tail = start;
            assert(retro_spsc_write(&q, payload, occupied) == occupied);
            q.cached_tail = start - stale;
            size_t head = start + occupied;
            size_t contiguous = q.mirror ? SIZE_MAX : q.mask + 1 - (head & q.mask);
            bool valid = need && need <= q.capacity && need <= contiguous;
            bool fits = valid && need <= q.capacity - occupied;
            unsigned expected_reads = valid && need > q.capacity - occupied - stale;
            memcpy(snapshot, q.buffer, q.mask + 1);
            memcpy(expected, payload, occupied);
            void *ptr = (void *)(uintptr_t)1;
            tail_reads = 0;
            bool reserved = observed ? observed_reserve(&q, &ptr, need) :
                                       retro_spsc_write_reserve(&q, &ptr, need);
            assert(reserved == fits);
            if (observed) assert(tail_reads == expected_reads);
            assert(retro_atomic_load_relaxed_size(&q.head) == head);
            if (!fits) {
                assert(ptr == NULL && !memcmp(snapshot, q.buffer, q.mask + 1));
            } else {
                assert(ptr == q.buffer + (head & q.mask));
                memcpy(ptr, payload, need);
                assert(retro_spsc_read_avail(&q) == occupied);
                /* Abandon a reservation without publishing its bytes. */
                bool abandon = (trial % 7) == 0;
                retro_spsc_write_end(&q, abandon ? 0 : need);
                if (!abandon) {
                    memcpy(expected + occupied, payload, need);
                    occupied += need;
                }
            }
            assert(retro_spsc_read(&q, actual, sizeof(actual)) == occupied);
            assert(!memcmp(actual, expected, occupied));
            /* Restore loop input before exercising the other implementation. */
            if (fits && trial % 7) occupied -= need;
        }
    }
    printf("200000 %s reservations: exact spans, rejection, abandonment, unpublished writes, stale-tail refresh and cursor rollover pass\n",
           mirrored && q.mirror ? "mirrored" : "plain");
    free(snapshot);
    retro_spsc_free(&q);
}
int main(void) {cases(false); cases(true); return 0;}
