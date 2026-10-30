/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef uint64_t hwaddr;
typedef uint64_t VkDeviceSize;
/* ENTRY */
typedef struct {
    VertexCowCacheEntry vertex_cow_cache[512];
    unsigned vertex_cow_recent[4], vertex_cow_cache_rr;
    unsigned vertex_cow_tail[2];
    unsigned vertex_arena_epoch[2];
    unsigned slot;
} PGRAPHVkState;
/* PRODUCTION */

static uint32_t random_state = 17;
static uint32_t random_value(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void check_lookup(PGRAPHVkState *r, hwaddr lo, hwaddr end)
{
    /* A recent index may select a different containing snapshot; any
     * uninvalidated snapshot in its current arena generation is legal. */
    bool exists = false;
    for (unsigned i = 0; i < ARRAY_SIZE(r->vertex_cow_cache); i++) {
        const VertexCowCacheEntry *e = &r->vertex_cow_cache[i];
        if (e->valid && e->epoch == r->vertex_arena_epoch[e->slot] &&
            e->addr <= lo && end <= e->addr + e->size) {
            exists = true;
        }
    }
    VertexCowCacheEntry *e = vertex_cow_lookup(r, lo, end);
    assert((e != NULL) == exists);
    if (e) {
        assert(e->valid && e->addr <= lo && end <= e->addr + e->size);
        assert(e->epoch == r->vertex_arena_epoch[e->slot]);
    }
    for (unsigned i = 0; i < ARRAY_SIZE(r->vertex_cow_recent); i++) {
        assert(r->vertex_cow_recent[i] <= ARRAY_SIZE(r->vertex_cow_cache));
        if (!r->vertex_cow_recent[i]) continue;
        for (unsigned j = i + 1; j < ARRAY_SIZE(r->vertex_cow_recent); j++) {
            assert(r->vertex_cow_recent[i] != r->vertex_cow_recent[j]);
        }
    }
}

int main(void)
{
    PGRAPHVkState r = {0};
    check_lookup(&r, 100, 120);
    vertex_cow_store(&r, 100, 100, 256);
    VertexCowCacheEntry *e = vertex_cow_lookup(&r, 120, 180);
    assert(e && e->addr == 100 && e->arena_offset == 256);
    /* Touching ranges do not overlap; one byte inside invalidates it. */
    pgraph_vk_vertex_cow_cache_invalidate(&r, 200, 20);
    check_lookup(&r, 120, 180);
    pgraph_vk_vertex_cow_cache_invalidate(&r, 199, 1);
    assert(!vertex_cow_lookup(&r, 120, 180));
    /* Recycling an arena invalidates even the most recent index. */
    vertex_cow_store(&r, 100, 100, 512);
    r.vertex_arena_epoch[0]++;
    assert(!vertex_cow_lookup(&r, 120, 180));
    r.slot = 1;
    vertex_cow_store(&r, 100, 100, 768);
    e = vertex_cow_lookup(&r, 120, 180);
    assert(e && e->slot == 1 && e->arena_offset == 768);
    r.vertex_arena_epoch[0]++;
    assert(vertex_cow_lookup(&r, 120, 180) == e);
    r.vertex_arena_epoch[1]++;
    assert(!vertex_cow_lookup(&r, 120, 180));
    /* Fill and replace beyond all 512 entries, mixing subrange lookups,
     * invalidations and both arena generations. */
    for (unsigned i = 0; i < 40000; i++) {
        hwaddr addr = (random_value() % 1024) * 64;
        unsigned size = 64 + random_value() % 1024;
        switch (random_value() % 8) {
        case 0:
        case 1:
        case 2:
        case 3:
            r.slot = random_value() & 1;
            vertex_cow_store(&r, addr, size, i * 256);
            break;
        case 4:
            pgraph_vk_vertex_cow_cache_invalidate(&r, addr, size);
            break;
        case 5:
            r.vertex_arena_epoch[random_value() & 1]++;
            break;
        default:
            break;
        }
        check_lookup(&r, addr, addr + size);
        check_lookup(&r, addr + 1, addr + size - 1);
    }
    memset(r.vertex_cow_cache, 0, sizeof(r.vertex_cow_cache));
    check_lookup(&r, 100, 120);
    puts("vertex snapshot cache: invalidation, arena epochs and 80000 randomized lookups passed");
    return 0;
}
