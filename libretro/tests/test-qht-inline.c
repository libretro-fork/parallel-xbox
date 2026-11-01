/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <retro_atomic.h>
#define HOST_LONG_BITS (__SIZEOF_POINTER__ * 8)
#define QEMU_ALIGNED(n) __attribute__((aligned(n)))
#define QEMU_ALWAYS_INLINE __attribute__((always_inline))
#define QEMU_BUILD_BUG_ON(c) _Static_assert(!(c), #c)
#define likely(v) __builtin_expect(!!(v),1)
#define unlikely(v) __builtin_expect(!!(v),0)
typedef int QemuSpin;
typedef struct QemuSeqLock QemuSeqLock;
/* PRODUCTION */
struct item {unsigned key;};
static struct qht table;
static struct qht_map maps[2];
static struct qht_bucket buckets[3];
static struct item items[3]={{1},{2},{1}};
static unsigned mutate, comparisons;
static bool compare(const void *obj, const void *key)
{
    comparisons++;
    if (mutate) {
        mutate=0;
        seqlock_write_begin(&buckets[0].sequence);
        QHT_SET(&buckets[0],0,7,&items[2]);
        seqlock_write_end(&buckets[0].sequence);
    }
    return ((const struct item *)obj)->key==*(const unsigned *)key;
}
static void reset(void)
{
    memset(buckets,0,sizeof(buckets)); memset(maps,0,sizeof(maps));
    for(unsigned i=0;i<3;i++) seqlock_init(&buckets[i].sequence);
    maps[0].buckets=buckets; maps[0].n_buckets=1;
    maps[1].buckets=&buckets[2]; maps[1].n_buckets=1;
    retro_atomic_store_relaxed_ptr(&table.map,&maps[0]);
    comparisons=mutate=0;
}
static void *writer(void *unused)
{
    (void)unused;
    for(unsigned i=0;i<100000;i++) {
        seqlock_write_begin(&buckets[0].sequence);
        QHT_SET(&buckets[0],0,7,&items[(i&1)?0:2]);
        seqlock_write_end(&buckets[0].sequence);
    }
    return NULL;
}
int main(void)
{
    unsigned key=1; pthread_t thread;
    for(unsigned generic=0;generic<2;generic++) {
#define LOOKUP(h) (generic?qht_lookup_custom(&table,&key,h,compare):qht_lookup_inline(&table,&key,h,compare))
        reset(); assert(!LOOKUP(0)); assert(!comparisons);
        QHT_SET(&buckets[0],0,7,&items[1]);
        QHT_SET(&buckets[0],1,7,NULL);
        QHT_SET(&buckets[1],0,7,&items[0]);
        retro_atomic_store_relaxed_ptr(&buckets[0].next,&buckets[1]);
        assert(LOOKUP(7)==&items[0]); assert(comparisons==2);
        key=99; assert(!LOOKUP(7)); key=1;
        assert(!LOOKUP(8));
        /* A comparator may fault/probe mappings; if a writer changed the
         * bucket while comparing, the stale successful result must retry. */
        QHT_SET(&buckets[0],0,7,&items[0]); mutate=1; comparisons=0;
        assert(LOOKUP(7)==&items[2]); assert(comparisons>=2);
        QHT_SET(&buckets[2],0,7,&items[0]);
        retro_atomic_store_release_ptr(&table.map,&maps[1]);
        assert(LOOKUP(7)==&items[0]);
        retro_atomic_store_release_ptr(&table.map,&maps[0]);
        assert(!pthread_create(&thread,NULL,writer,NULL));
        for(unsigned i=0;i<100000;i++) {
            void *p=LOOKUP(7); assert(p==&items[0] || p==&items[2]);
        }
        assert(!pthread_join(thread,NULL));
#undef LOOKUP
    }
    puts("QHT inline/generic: collisions, chains, nulls, misses, retry, map replacement and 200000 concurrent reads passed");
}
