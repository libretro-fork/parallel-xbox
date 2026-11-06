/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <retro_inline.h>
#include <retro_spsc.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while (0)

static unsigned head_reads;
static size_t observed_acquire(retro_atomic_size_t *p)
{
    head_reads++;
    return retro_atomic_load_acquire_size(p);
}
#undef retro_atomic_load_acquire_size
#define retro_atomic_load_acquire_size(p) observed_acquire(p)
/* OBSERVED BEGIN */
#undef retro_atomic_load_acquire_size

static uint32_t rng = 0x4398ad71;
static uint32_t next(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
}
static void copy_view(void *dst, const void *first, size_t first_bytes,
                      const void *second, size_t total)
{
    if (first_bytes) memcpy(dst, first, first_bytes);
    if (total > first_bytes)
        memcpy((uint8_t *)dst + first_bytes, second, total - first_bytes);
}
static void cases(bool mirrored)
{
    retro_spsc_t q;
    assert(mirrored ? retro_spsc_init_mirrored(&q, 128) : retro_spsc_init(&q, 128));
    uint8_t payload[128], actual[128];
    uint8_t *snapshot = malloc(q.mask + 1);
    assert(snapshot && q.capacity == sizeof(payload));
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)next();
    for (unsigned trial = 0; trial < 100000; trial++) {
        size_t start = trial & 1 ? SIZE_MAX - next() % 512 : next();
        size_t occupied = next() % 129;
        size_t need = trial % 31 ? next() % 140 : SIZE_MAX;
        size_t stale = next() % (occupied + 1);
        size_t total = occupied < need ? occupied : need;
        size_t contiguous = q.mirror ? SIZE_MAX : q.mask + 1 - (start & q.mask);
        size_t first_length = total < contiguous ? total : contiguous;
        for (unsigned observed = 0; observed < 2; observed++) {
            retro_atomic_size_init(&q.head, start);
            retro_atomic_size_init(&q.tail, start);
            q.cached_head = q.cached_tail = start;
            assert(retro_spsc_write(&q, payload, occupied) == occupied);
            q.cached_head = start + occupied - stale;
            memcpy(snapshot, q.buffer, q.mask + 1);
            const void *first = (void *)(uintptr_t)1, *second = first;
            size_t first_bytes = SIZE_MAX;
            head_reads = 0;
            size_t got = observed ? observed_begin(&q, &first, &first_bytes, &second, need) :
                                    retro_spsc_read_begin_pair(&q, &first, &first_bytes, &second, need);
            assert(got == total && first_bytes == first_length);
            if (observed) assert(head_reads == (unsigned)(need > occupied - stale));
            assert(first == (total ? q.buffer + (start & q.mask) : NULL));
            assert(second == (first_length < total ? q.buffer : NULL));
            assert(retro_atomic_load_relaxed_size(&q.tail) == start);
            assert(retro_atomic_load_relaxed_size(&q.head) == start + occupied);
            assert(!memcmp(snapshot, q.buffer, q.mask + 1));
            copy_view(actual, first, first_bytes, second, got);
            assert(!memcmp(actual, payload, total));
            /* The producer can fill free space, but cannot reuse our view. */
            assert(retro_spsc_write(&q, payload + occupied, q.capacity - occupied) == q.capacity - occupied);
            assert(!retro_spsc_write(&q, payload, 1));
            copy_view(actual, first, first_bytes, second, got);
            assert(!memcmp(actual, payload, total));
            size_t consumed = trial % 7 == 0 ? 0 : trial % 3 ? total : next() % (total + 1);
            retro_spsc_read_end(&q, consumed);
            assert(retro_atomic_load_relaxed_size(&q.tail) == start + consumed);
            assert(retro_spsc_read(&q, actual, sizeof(actual)) == q.capacity - consumed);
            assert(!memcmp(actual, payload + consumed, q.capacity - consumed));
        }
    }
    printf("200000 %s views: exact spans, partial availability/consumption, abandonment, stable bytes, stale-head refresh and rollover passed\n",
           q.mirror ? "mirrored" : "plain");
    free(snapshot);
    retro_spsc_free(&q);
}

enum { RECORD_BYTES = 64, RECORDS = 100000 };
static retro_spsc_t shared;
static void fill_record(uint8_t *p, unsigned serial)
{
    for (unsigned i = 0; i < RECORD_BYTES; i++) p[i] = (uint8_t)(serial * 19 + i * 31);
    memcpy(p, &serial, sizeof(serial));
}
static void *produce(void *arg)
{
    (void)arg;
    uint8_t record[RECORD_BYTES];
    for (unsigned i = 0; i < RECORDS; i++) {
        fill_record(record, i);
        while (retro_spsc_write_avail(&shared) < sizeof(record)) sched_yield();
        assert(retro_spsc_write(&shared, record, sizeof(record)) == sizeof(record));
    }
    return NULL;
}
static void *consume(void *arg)
{
    (void)arg;
    uint8_t actual[RECORD_BYTES], expected[RECORD_BYTES];
    for (unsigned i = 0; i < RECORDS; i++) {
        const void *first, *second;
        size_t n, first_bytes;
        while (!(n = retro_spsc_read_begin_pair(&shared, &first, &first_bytes, &second, sizeof(actual)))) sched_yield();
        assert(n == sizeof(actual));
        fill_record(expected, i);
        copy_view(actual, first, first_bytes, second, n);
        assert(!memcmp(actual, expected, n));
        if (!(i % 16)) {
            /* Let the writer attempt to refill while both spans stay borrowed. */
            sched_yield();
            copy_view(actual, first, first_bytes, second, n);
            assert(!memcmp(actual, expected, n));
        }
        retro_spsc_read_end(&shared, n);
    }
    return NULL;
}
static void concurrent(bool mirrored)
{
    assert(mirrored ? retro_spsc_init_mirrored(&shared, 1024) : retro_spsc_init(&shared, 1024));
    size_t start = SIZE_MAX - 19;
    retro_atomic_size_init(&shared.head, start);
    retro_atomic_size_init(&shared.tail, start);
    shared.cached_head = shared.cached_tail = start;
    pthread_t producer, consumer;
    assert(!pthread_create(&consumer, NULL, consume, NULL));
    assert(!pthread_create(&producer, NULL, produce, NULL));
    assert(!pthread_join(producer, NULL));
    assert(!pthread_join(consumer, NULL));
    assert(!retro_spsc_read_avail(&shared));
    printf("100000 %s concurrent records: exact bytes/order and borrowed lifetime passed\n", shared.mirror ? "mirrored" : "plain");
    retro_spsc_free(&shared);
}
int main(void)
{
    cases(false); cases(true); concurrent(false); concurrent(true); return 0;
}
