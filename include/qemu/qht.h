/*
 * Copyright (C) 2016, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#ifndef QEMU_QHT_H
#define QEMU_QHT_H

#include "qemu/seqlock.h"
#include "qemu/thread.h"
#include "qemu/qdist.h"
#include <retro_atomic.h>

typedef bool (*qht_cmp_func_t)(const void *a, const void *b);

struct qht {
    retro_atomic_ptr_t map;     /* struct qht_map * */
    qht_cmp_func_t cmp;
    QemuMutex lock; /* serializes setters of ht->map */
    unsigned int mode;
};

/**
 * struct qht_stats - Statistics of a QHT
 * @head_buckets: number of head buckets
 * @used_head_buckets: number of non-empty head buckets
 * @entries: total number of entries
 * @chain: frequency distribution representing the number of buckets in each
 *         chain, excluding empty chains.
 * @occupancy: frequency distribution representing chain occupancy rate.
 *             Valid range: from 0.0 (empty) to 1.0 (full occupancy).
 *
 * An entry is a pointer-hash pair.
 * Each bucket can host several entries.
 * Chains are chains of buckets, whose first link is always a head bucket.
 */
struct qht_stats {
    size_t head_buckets;
    size_t used_head_buckets;
    size_t entries;
    struct qdist chain;
    struct qdist occupancy;
};

typedef bool (*qht_lookup_func_t)(const void *obj, const void *userp);
typedef void (*qht_iter_func_t)(void *p, uint32_t h, void *up);
typedef bool (*qht_iter_bool_func_t)(void *p, uint32_t h, void *up);

#define QHT_MODE_AUTO_RESIZE 0x1 /* auto-resize when heavily loaded */

/**
 * qht_init - Initialize a QHT
 * @ht: QHT to be initialized
 * @cmp: default comparison function. Cannot be NULL.
 * @n_elems: number of entries the hash table should be optimized for.
 * @mode: bitmask with OR'ed QHT_MODE_*
 */
void qht_init(struct qht *ht, qht_cmp_func_t cmp, size_t n_elems,
              unsigned int mode);

/**
 * qht_insert - Insert a pointer into the hash table
 * @ht: QHT to insert to
 * @p: pointer to be inserted
 * @hash: hash corresponding to @p
 * @existing: address where the pointer to an existing entry can be copied to
 *
 * Attempting to insert a NULL @p is a bug.
 * Inserting the same pointer @p with different @hash values is a bug.
 *
 * In case of successful operation, a release barrier is implied before the pointer is
 * inserted into the hash table.
 *
 * Returns true on success.
 * Returns false if there is an existing entry in the table that is equivalent
 * (i.e. ht->cmp matches and the hash is the same) to @p-@h. If @existing
 * is !NULL, a pointer to this existing entry is copied to it.
 */
bool qht_insert(struct qht *ht, void *p, uint32_t hash, void **existing);

/**
 * qht_lookup_custom - Look up a pointer using a custom comparison function.
 * @ht: QHT to be looked up
 * @userp: pointer to pass to @func
 * @hash: hash of the pointer to be looked up
 * @func: function to compare existing pointers against @userp
 *
 * Needs to be called under an RCU read-critical section.
 *
 * The pointer is read before @func is called, and what it points to after.
 *
 * The user-provided @func compares pointers in QHT against @userp.
 * If the function returns true, a match has been found.
 *
 * Returns the corresponding pointer when a match is found.
 * Returns NULL otherwise.
 */
void *qht_lookup_custom(const struct qht *ht, const void *userp, uint32_t hash,
                        qht_lookup_func_t func);

/**
 * qht_remove - remove a pointer from the hash table
 * @ht: QHT to remove from
 * @p: pointer to be removed
 * @hash: hash corresponding to @p
 *
 * Attempting to remove a NULL @p is a bug.
 *
 * Just-removed @p pointers cannot be immediately freed; they need to remain
 * valid until the end of the RCU grace period in which qht_remove() is called.
 * This guarantees that concurrent lookups will always compare against valid
 * data.
 *
 * Returns true on success.
 * Returns false if the @p-@hash pair was not found.
 */
bool qht_remove(struct qht *ht, const void *p, uint32_t hash);

/**
 * qht_reset_size - reset and resize a QHT
 * @ht: QHT to be reset and resized
 * @n_elems: number of entries the resized hash table should be optimized for.
 *
 * Returns true if the resize was necessary and therefore performed.
 * Returns false otherwise.
 *
 * If concurrent readers may exist, the objects pointed to by the hash table
 * must remain valid for the existing RCU grace period -- see qht_remove().
 * See also: qht_reset(), qht_resize().
 */
bool qht_reset_size(struct qht *ht, size_t n_elems);

/**
 * qht_iter - Iterate over a QHT
 * @ht: QHT to be iterated over
 * @func: function to be called for each entry in QHT
 * @userp: additional pointer to be passed to @func
 *
 * Each time it is called, user-provided @func is passed a pointer-hash pair,
 * plus @userp.
 *
 * Note: @ht cannot be accessed from @func
 * See also: qht_iter_remove()
 */
void qht_iter(struct qht *ht, qht_iter_func_t func, void *userp);


#endif /* QEMU_QHT_H */
