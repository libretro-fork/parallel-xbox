/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qemu/lru.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "lru-key-view-reference.h"

enum { COUNT = 32, POOL = 96, HEAD = offsetof(ShaderState, psh) & ~63u };
typedef struct Entry { LruNode node; ShaderState state; } Entry;
typedef struct Cache {
    Lru lru;
    Entry entries[COUNT];
    unsigned initialized, evicted, considered;
    size_t copied;
} Cache;
typedef struct View { const void *head, *tail; } View;
#define OWNER(p, t, field) ((t *)((char *)(p) - offsetof(t, field)))
static Entry *entry(LruNode *node) { return OWNER(node, Entry, node); }
static Cache *cache(Lru *lru) { return OWNER(lru, Cache, lru); }
static void initialized(Lru *lru) {
    cache(lru)->initialized++;
    cache(lru)->copied += sizeof(ShaderState);
}
static void init(Lru *lru, LruNode *node, const void *key) {
    memcpy(&entry(node)->state, key, sizeof(ShaderState));
    initialized(lru);
}
static bool compare(Lru *lru, LruNode *node, const void *key) {
    (void)lru;
    return memcmp(&entry(node)->state, key, sizeof(ShaderState)) != 0;
}
static void init_view(Lru *lru, LruNode *node, const void *key) {
    const View *view = key;
    memcpy(&entry(node)->state, view->head, HEAD);
    memcpy((uint8_t *)&entry(node)->state + HEAD, view->tail,
           sizeof(ShaderState) - HEAD);
    initialized(lru);
}
static bool compare_view(Lru *lru, LruNode *node, const void *key) {
    const View *view = key;
    (void)lru;
    return memcmp(&entry(node)->state, view->head, HEAD) ||
           memcmp((uint8_t *)&entry(node)->state + HEAD, view->tail,
                  sizeof(ShaderState) - HEAD);
}
static bool pre_evict(Lru *lru, LruNode *node) {
    cache(lru)->considered++;
    return (entry(node)->state.vsh.programmable.program_data[0][0] % 5) != 0;
}
static void post_evict(Lru *lru, LruNode *node) {
    cache(lru)->evicted++;
    memset(&entry(node)->state, 0xa7, sizeof(ShaderState));
}
static Cache *setup(void) {
    Cache *c = calloc(1, sizeof(*c));
    assert(c);
    lru_init(&c->lru);
    c->lru.init_node = init;
    c->lru.compare_nodes = compare;
    c->lru.pre_node_evict = pre_evict;
    c->lru.post_node_evict = post_evict;
    for (unsigned i = 0; i < COUNT; i++)
        lru_add_free(&c->lru, &c->entries[i].node);
    return c;
}
static ptrdiff_t index_of(Cache *c, LruNode *node) {
    return node ? entry(node) - c->entries : -1;
}
static void same(Cache *a, Cache *b) {
    assert(a->lru.num_free == b->lru.num_free);
    assert(a->lru.num_used == b->lru.num_used);
    assert(a->initialized == b->initialized && a->evicted == b->evicted);
    assert(a->considered == b->considered && a->copied == b->copied);
    LruNode *x = QTAILQ_FIRST(&a->lru.global);
    LruNode *y = QTAILQ_FIRST(&b->lru.global);
    while (x || y) {
        assert(x && y && index_of(a, x) == index_of(b, y));
        assert(x->hash == y->hash);
        assert(!memcmp(&entry(x)->state, &entry(y)->state, sizeof(ShaderState)));
        x = QTAILQ_NEXT(x, next_global);
        y = QTAILQ_NEXT(y, next_global);
    }
    for (unsigned bin = 0; bin < 4; bin++) {
        x = QTAILQ_FIRST(&a->lru.bins[bin]);
        y = QTAILQ_FIRST(&b->lru.bins[bin]);
        while (x || y) {
            assert(x && y && index_of(a, x) == index_of(b, y));
            x = QTAILQ_NEXT(x, next_bin);
            y = QTAILQ_NEXT(y, next_bin);
        }
    }
}
static uint32_t random_state = 0x123dc789;
static uint32_t random_value(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
int main(void) {
    Cache *reference = setup(), *current = setup();
    ShaderState *states = calloc(POOL, sizeof(*states));
    assert(states);
    for (unsigned i = 0; i < POOL; i++) {
        states[i].vsh.programmable.program_data[0][0] = i / 2;
        states[i].psh.final_inputs_0 = i;
    }
    const LruKeyOps ops = {init_view, compare_view};
    for (unsigned trial = 0; trial < 100000; trial++) {
        unsigned id = random_value() % POOL;
        /* Same digest deliberately covers unequal prefix and tail bytes. */
        uint64_t hash = (id / 2) % 4;
        uint8_t tail[sizeof(ShaderState) - HEAD];
        memcpy(tail, (uint8_t *)&states[id] + HEAD, sizeof(tail));
        View view = {&states[id], tail};
        LruNode *a = reference_lookup(&reference->lru, hash, &states[id]);
        LruNode *b;
        switch (trial % 3) {
        case 0: b = lru_lookup(&current->lru, hash, &states[id]); break;
        case 1: b = lru_lookup_with_ops(&current->lru, hash, &view, &ops); break;
        default: b = lru_lookup_with_ops(&current->lru, hash, &states[id], NULL); break;
        }
        memset(tail, 0xdc, sizeof(tail)); /* Borrow ends at return. */
        assert(index_of(reference, a) == index_of(current, b));
        assert(current->lru.init_node == init);
        assert(current->lru.compare_nodes == compare);
        same(reference, current);
    }
    assert(current->initialized > COUNT && current->evicted);
    printf("100000 mixed owned/view lookups: collisions, pinned evictions, exact owned bytes, copy counts and recency match\n");
    free(states);
    free(reference);
    free(current);
    /* An explicit alternate representation may intentionally have no
     * initializer, independently of the cache's ordinary initializer. */
    current = setup();
    const LruKeyOps no_init = {NULL, compare_view};
    LruNode *node = lru_lookup_with_ops(&current->lru, 7, NULL, &no_init);
    assert(node->hash == 7 && current->initialized == 0);
    assert(current->lru.num_used == 1 && current->lru.num_free == COUNT - 1);
    free(current);
    return 0;
}
