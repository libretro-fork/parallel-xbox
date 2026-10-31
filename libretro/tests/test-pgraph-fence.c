/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include "hw/xbox/nv2a/nv2a_regs.h"

typedef uint64_t hwaddr;
typedef struct {
    retro_atomic_int_t regs_[0x2000];
    unsigned long regs_dirty[0x2000 / 4 / (8 * sizeof(unsigned long))];
    retro_atomic_int_t pending_interrupts, enabled_interrupts;
} PGRAPHState;
typedef struct { PGRAPHState pgraph; } NV2AState;
static unsigned ctz32(uint32_t v) { return __builtin_ctz(v); }
static void bitmap_set(unsigned long *map, unsigned bit, unsigned count)
{
    assert(count == 1);
    map[bit / (8 * sizeof(*map))] |= 1ul << (bit % (8 * sizeof(*map)));
}
/* Unused RDI paths retain their production switch arms. */
static uint32_t pgraph_rdi_read(PGRAPHState *pg, unsigned a, unsigned b)
{ abort(); }
static void pgraph_reg_set_mask_atomic(PGRAPHState *pg, unsigned a,
                                       uint32_t b, uint32_t c) { abort(); }
#define PG_GET_MASK(r, mask) GET_MASK(pgraph_reg_r(pg, r), mask)
#define nv2a_reg_log_read(...) ((void)0)
/* PRODUCTION */

/* These probes are also compiled for AArch64 to verify the actual ordering
 * instructions; an x86 stress run alone cannot expose weak-memory failures. */
__attribute__((noinline)) void probe_publish(PGRAPHState *pg, uint32_t value)
{ pgraph_fence_publish(pg, value); }
__attribute__((noinline)) uint32_t probe_poll(NV2AState *d)
{ return pgraph_read(d, NV_PGRAPH_PATT_COLOR0, 4); }

enum { COUNT = 65536, LANES = 4 };
static NV2AState device;
static uint32_t payload[COUNT][LANES];
static uint32_t pattern(unsigned n, unsigned lane)
{ return (n * 0x9e3779b9u) ^ (lane * 0x85ebca6bu); }
static void producer(void *unused)
{
    for (unsigned n = 1; n <= COUNT; n++) {
        for (unsigned lane = 0; lane < LANES; lane++)
            payload[n - 1][lane] = pattern(n, lane);
        probe_publish(&device.pgraph, n);
    }
}
int main(void)
{
    const uint32_t values[] = { 0, 1, 0x80000000u, 0xffffffffu, 0xffffffffu, 0 };
    PGRAPHState *pg = &device.pgraph;
    for (unsigned n = 0; n < sizeof(values) / sizeof(*values); n++) {
        uint32_t previous = probe_poll(&device);
        memset(pg->regs_dirty, 0, sizeof(pg->regs_dirty));
        probe_publish(pg, values[n]);
        assert(probe_poll(&device) == values[n]);
        unsigned bit = NV_PGRAPH_PATT_COLOR0 / 4;
        unsigned long expected = previous != values[n]
                               ? 1ul << (bit % (8 * sizeof(unsigned long))) : 0;
        for (unsigned i = 0; i < sizeof(pg->regs_dirty) / sizeof(*pg->regs_dirty); i++)
            assert(pg->regs_dirty[i] == (i == bit / (8 * sizeof(unsigned long)) ? expected : 0));
    }
    /* Immutable payload slots avoid an acknowledgement that would itself
     * publish the data and hide a missing fence release/acquire. */
    for (unsigned round = 0; round < 8; round++) {
        memset(payload, 0, sizeof(payload));
        probe_publish(pg, 0);
        sthread_t *thread = sthread_create(producer, NULL);
        assert(thread);
        unsigned checked = 0;
        /* Bounded guest-style polling, not a production wait or a lock. */
        for (unsigned polls = 0; polls < 100000000 && checked != COUNT; polls++) {
            unsigned published = probe_poll(&device);
            assert(published >= checked && published <= COUNT);
            while (checked < published) {
                for (unsigned lane = 0; lane < LANES; lane++)
                    assert(payload[checked][lane] == pattern(checked + 1, lane));
                checked++;
            }
        }
        assert(checked == COUNT);
        sthread_join(thread);
    }
    /* Neighboring registers and IRQ reads still return their full words. */
    pgraph_reg_w(pg, NV_PGRAPH_PATT_COLOR0 + 4, 0xfedcba98u);
    assert(pgraph_read(&device, NV_PGRAPH_PATT_COLOR0 + 4, 4) == 0xfedcba98u);
    retro_atomic_store_relaxed_int(&pg->pending_interrupts, (int)0x80000000u);
    assert(pgraph_read(&device, NV_PGRAPH_INTR, 4) == 0x80000000u);
    puts("PGRAPH fence: 524288 publications, immutable payload visibility, dirty bits and full-width values passed");
}
