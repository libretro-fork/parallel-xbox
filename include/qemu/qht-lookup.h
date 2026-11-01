/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 */
/* Internal QHT reader layout. Only include when a hot caller needs its
 * comparator specialized in the scan. Call under an RCU read-critical
 * section, just like qht_lookup_custom(). Writers and reclamation remain
 * in qht.c; contention uses its out-of-line retry path. */
#ifndef QEMU_QHT_LOOKUP_H
#define QEMU_QHT_LOOKUP_H
#include "qemu/qht.h"
#include "qemu/rcu.h"

/*
 * We want to avoid false sharing of cache lines. Most systems have 64-byte
 * cache lines so we go with it for simplicity.
 *
 * Note that systems with smaller cache lines will be fine (the struct is
 * almost 64-bytes); systems with larger cache lines might suffer from
 * some false sharing.
 */
#define QHT_BUCKET_ALIGN 64

/* define these to keep sizeof(qht_bucket) within QHT_BUCKET_ALIGN */
#if HOST_LONG_BITS == 32
#define QHT_BUCKET_ENTRIES 6
#else /* 64-bit */
#define QHT_BUCKET_ENTRIES 4
#endif

/*
 * Note: reading partially-updated pointers in @pointers could lead to
 * segfaults. We thus access them as retro_atomic; this guarantees that the
 * compiler makes all those accesses atomic and does not refetch them.
 *
 * If both ht->lock and b->lock are grabbed, ht->lock should always
 * be grabbed first.
 */
struct qht_bucket {
    QemuSpin lock;
    QemuSeqLock sequence;
    retro_atomic_int_t hashes[QHT_BUCKET_ENTRIES];      /* uint32_t */
    retro_atomic_ptr_t pointers[QHT_BUCKET_ENTRIES];
    retro_atomic_ptr_t next;                            /* struct qht_bucket * */
} QEMU_ALIGNED(QHT_BUCKET_ALIGN);

/* Every access goes through these, relaxed: the readers' order comes from
 * the bucket's seqlock (and RCU for the chain and the map) */
#define QHT_HASH(b, i)        ((uint32_t)retro_atomic_load_relaxed_int(&(b)->hashes[i]))
#define QHT_PTR(b, i)         retro_atomic_load_relaxed_ptr(&(b)->pointers[i])
#define QHT_NEXT(b) \
    ((struct qht_bucket *)retro_atomic_load_relaxed_ptr(&(b)->next))
#define QHT_SET(b, i, h, p) do {                                \
        retro_atomic_store_relaxed_int(&(b)->hashes[i], (h));   \
        retro_atomic_store_relaxed_ptr(&(b)->pointers[i], (p)); \
    } while (0)
#define QHT_MAP(ht) \
    ((struct qht_map *)retro_atomic_load_relaxed_ptr(&(ht)->map))

QEMU_BUILD_BUG_ON(sizeof(struct qht_bucket) > QHT_BUCKET_ALIGN);

/**
 * struct qht_map - structure to track an array of buckets
 * @rcu: used by RCU. Keep it as the top field in the struct to help valgrind
 *       find the whole struct.
 * @buckets: array of head buckets. It is constant once the map is created.
 * @n_buckets: number of head buckets. It is constant once the map is created.
 * @n_added_buckets: number of added (i.e. "non-head") buckets
 * @n_added_buckets_threshold: threshold to trigger an upward resize once the
 *                             number of added buckets surpasses it.
 *
 * Buckets are tracked in what we call a "map", i.e. this structure.
 */
struct qht_map {
    struct rcu_head rcu;
    struct qht_bucket *buckets;
    size_t n_buckets;
    retro_atomic_size_t n_added_buckets;
    size_t n_added_buckets_threshold;
};

static inline
struct qht_bucket *qht_map_to_bucket(const struct qht_map *map, uint32_t hash)
{
    return &map->buckets[hash & (map->n_buckets - 1)];
}

void *qht_lookup__slowpath(const struct qht_bucket *b, qht_lookup_func_t func,
                           const void *userp, uint32_t hash);

static inline QEMU_ALWAYS_INLINE
void *qht_do_lookup(const struct qht_bucket *head, qht_lookup_func_t func,
                    const void *userp, uint32_t hash)
{
    const struct qht_bucket *b = head;
    int i;

    do {
        for (i = 0; i < QHT_BUCKET_ENTRIES; i++) {
            if (QHT_HASH(b, i) == hash) {
                /* The pointer is dereferenced before seqlock_read_retry:
                 * it is loaded atomically, and its address dependency
                 * orders what is read through it.
                 */
                void *p = QHT_PTR(b, i);

                if (likely(p) && likely(func(p, userp))) {
                    return p;
                }
            }
        }
        b = QHT_NEXT(b);
    } while (b);

    return NULL;
}

static inline QEMU_ALWAYS_INLINE
void *qht_lookup_inline(const struct qht *ht, const void *userp, uint32_t hash,
                        qht_lookup_func_t func)
{
    const struct qht_bucket *b;
    const struct qht_map *map;
    unsigned int version;
    void *ret;

    map = QHT_MAP(ht);
    b = qht_map_to_bucket(map, hash);

    version = seqlock_read_begin(&b->sequence);
    ret = qht_do_lookup(b, func, userp, hash);
    if (likely(!seqlock_read_retry(&b->sequence, version))) {
        return ret;
    }
    /*
     * Removing the do/while from the fastpath gives a 4% perf. increase when
     * running a 100%-lookup microbenchmark.
     */
    return qht_lookup__slowpath(b, func, userp, hash);
}


#endif
