/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_spsc.h>

/* Failed negative controls must not invoke a Windows crash/JIT dialog. */
#undef assert
#define assert(expr) do { if (!(expr)) { \
    fprintf(stderr, "SPSC pair: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    _Exit(1); \
} } while (0)

/* The production queue TU redirects memcpy here in the checked build.
 * Inspect publication during every constituent copy, including wrap copies. */
static retro_spsc_t *observed;
static size_t unpublished_head;
static unsigned copies;
void *observed_memcpy(void *dst, const void *src, size_t n)
{
    if (observed) {
        assert(retro_atomic_load_relaxed_size(&observed->head) == unpublished_head);
        copies++;
    }
    return memcpy(dst, src, n);
}

static uint32_t seed = 0x981237ab;
static uint32_t next(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static void cases(bool mirrored)
{
    retro_spsc_t q;
    assert(mirrored ? retro_spsc_init_mirrored(&q, 128) : retro_spsc_init(&q, 128));
    uint8_t first[256], second[256], actual[256], expected[256];
    uint8_t *snapshot = malloc(q.mask + 1);
    assert(snapshot);
    for (size_t i = 0; i < sizeof(first); i++) {
        first[i] = (uint8_t)next();
        second[i] = (uint8_t)next();
    }
    for (unsigned trial = 0; trial < 100000; trial++) {
        size_t occupied = next() % 129;
        size_t na = next() % 140, nb = next() % 140;
        size_t start = trial % 2 ? (size_t)-1 - next() % 512 : next();
        retro_atomic_size_init(&q.head, start);
        retro_atomic_size_init(&q.tail, start);
        q.cached_head = q.cached_tail = start;
        assert(retro_spsc_write(&q, first, occupied) == occupied);
        memcpy(expected, first, occupied);
        memcpy(snapshot, q.buffer, q.mask + 1);
        size_t head = retro_atomic_load_relaxed_size(&q.head);
        unpublished_head = head;
        observed = &q;
        copies = 0;
        size_t written = retro_spsc_write_pair(&q, na ? first : NULL, na,
                                               nb ? second : NULL, nb);
        observed = NULL;
        bool fits = na + nb <= q.capacity - occupied;
        assert(written == (fits ? na + nb : 0));
        assert(retro_atomic_load_relaxed_size(&q.head) == head + written);
#ifdef OBSERVE_COPIES
        if (written)
            assert(copies >= (unsigned)(na != 0) + (unsigned)(nb != 0));
        else
            assert(copies == 0);
#endif
        if (!written) {
            assert(!memcmp(snapshot, q.buffer, q.mask + 1));
        } else {
            memcpy(expected + occupied, first, na);
            memcpy(expected + occupied + na, second, nb);
        }
        assert(retro_spsc_read(&q, actual, sizeof(actual)) == occupied + written);
        assert(!memcmp(actual, expected, occupied + written));
        assert(retro_spsc_read_avail(&q) == 0);
        if (!(trial % 100)) {
            /* Reject overflow before touching even deliberately NULL sources. */
            assert(!retro_spsc_write_pair(&q, NULL, SIZE_MAX, NULL, 2));
            assert(!retro_spsc_write_pair(&q, NULL, 1, NULL, SIZE_MAX));
            assert(!retro_spsc_write_pair(&q, NULL, 0, NULL, 0));
        }
    }
    printf("100000 %s queue cases: byte order, full-record rejection, wrap, cursor rollover and empty/overflow spans pass\n",
           mirrored && q.mirror ? "mirrored" : "plain");
    free(snapshot);
    retro_spsc_free(&q);
}

enum { RECORD_BYTES = 2620, PREFIX_BYTES = 2304, RECORDS = 100000 };
typedef struct Transfer { retro_spsc_t queue; } Transfer;
static void make_record(uint8_t *record, unsigned sequence)
{
    for (unsigned i = 0; i < RECORD_BYTES; i++)
        record[i] = (uint8_t)(sequence * 17 + (sequence >> 8) + i * 31);
    memcpy(record, &sequence, sizeof(sequence));
}
static void *producer(void *arg)
{
    Transfer *t = arg;
    uint8_t head[PREFIX_BYTES], tail[RECORD_BYTES - PREFIX_BYTES];
    uint8_t record[RECORD_BYTES];
    for (unsigned i = 0; i < RECORDS; i++) {
        make_record(record, i);
        memcpy(head, record, sizeof(head));
        memcpy(tail, record + sizeof(head), sizeof(tail));
        while (!retro_spsc_write_pair(&t->queue, head, sizeof(head), tail, sizeof(tail)))
            sched_yield();
        memset(head, 0xda, sizeof(head));
        memset(tail, 0xdb, sizeof(tail));
    }
    return NULL;
}
static void *consumer(void *arg)
{
    Transfer *t = arg;
    uint8_t record[RECORD_BYTES], expected[RECORD_BYTES];
    for (unsigned i = 0; i < RECORDS; i++) {
        size_t n;
        while (!(n = retro_spsc_read(&t->queue, record, sizeof(record))))
            sched_yield();
        assert(n == sizeof(record));
        make_record(expected, i);
        assert(!memcmp(record, expected, sizeof(record)));
    }
    return NULL;
}
static void concurrent(bool mirrored)
{
    Transfer t;
    assert(mirrored ? retro_spsc_init_mirrored(&t.queue, 16384) : retro_spsc_init(&t.queue, 16384));
    /* Both streams cross SIZE_MAX as well as the physical wrap. */
    size_t start = SIZE_MAX - 4096;
    retro_atomic_size_init(&t.queue.head, start);
    retro_atomic_size_init(&t.queue.tail, start);
    t.queue.cached_head = t.queue.cached_tail = start;
    pthread_t writer, reader;
    assert(!pthread_create(&writer, NULL, producer, &t));
    assert(!pthread_create(&reader, NULL, consumer, &t));
    assert(!pthread_join(writer, NULL));
    assert(!pthread_join(reader, NULL));
    assert(retro_spsc_read_avail(&t.queue) == 0);
    printf("100000 concurrent %s 2620-byte records: no partial records, stale borrowed bytes or reordering\n",
           mirrored && t.queue.mirror ? "mirrored" : "plain");
    retro_spsc_free(&t.queue);
}
int main(void)
{
    cases(false);
    cases(true);
    concurrent(false);
    concurrent(true);
    return 0;
}
