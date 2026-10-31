/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise publication, per-producer FIFO and immediate node reclamation.
 * Build with -pthread and libretro-common/rthreads/rthreads.c.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <retro_mpsc.h>
#include <rthreads/rthreads.h>

enum { PRODUCERS = 8, ITEMS = 100000 };
typedef struct item {
    retro_mpsc_node_t node;
    unsigned producer, sequence, checksum;
} item;

static retro_mpsc_t queue;
static retro_atomic_int_t start;

static void publish(void *opaque)
{
    unsigned producer = *(unsigned *)opaque;
    unsigned i;
    while (!retro_atomic_load_acquire_int(&start)) {
    }
    for (i = 0; i < ITEMS; i++) {
        item *entry = (item *)malloc(sizeof(*entry));
        assert(entry);
        entry->producer = producer;
        entry->sequence = i;
        entry->checksum = i ^ (producer * 0x1234567U);
        retro_mpsc_push(&queue, &entry->node);
        /* Consumer may already have freed entry. */
    }
}

int main(void)
{
    sthread_t *threads[PRODUCERS];
    unsigned ids[PRODUCERS], next[PRODUCERS] = { 0 };
    unsigned i, consumed = 0;
    retro_mpsc_node_t *node;
    item first = { { NULL }, 0, 0, 0 };
    item second = { { NULL }, 0, 1, 1 };
    retro_mpsc_init(&queue);
    assert(retro_mpsc_empty(&queue));
    assert(retro_mpsc_take_all(&queue) == NULL);
    retro_mpsc_push(&queue, &first.node);
    retro_mpsc_push(&queue, &second.node);
    node = retro_mpsc_take_all(&queue);
    assert(node == &first.node && node->next == &second.node);
    assert(second.node.next == NULL && retro_mpsc_empty(&queue));
    retro_atomic_int_init(&start, 0);
    for (i = 0; i < PRODUCERS; i++) {
        ids[i] = i;
        threads[i] = sthread_create(publish, &ids[i]);
        assert(threads[i]);
    }
    retro_atomic_store_release_int(&start, 1);
    while (consumed < PRODUCERS * ITEMS) {
        node = retro_mpsc_take_all(&queue);
        while (node) {
            item *entry = (item *)((char *)node - offsetof(item, node));
            node = node->next;
            assert(entry->producer < PRODUCERS);
            assert(entry->sequence == next[entry->producer]++);
            assert(entry->checksum ==
                   (entry->sequence ^ (entry->producer * 0x1234567U)));
            free(entry);
            consumed++;
        }
    }
    for (i = 0; i < PRODUCERS; i++) {
        sthread_join(threads[i]);
        assert(next[i] == ITEMS);
    }
    assert(retro_mpsc_empty(&queue));
    assert(retro_mpsc_take_all(&queue) == NULL);
    puts("MPSC: 800000 nodes, FIFO and immediate reclamation passed");
    return 0;
}
