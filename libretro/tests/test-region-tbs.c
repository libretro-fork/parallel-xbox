/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The production TB index of a code region (tcg/region.c: insert, remove,
 * lookup by host address, foreach, count, reset) against a reference, in
 * the order tb_gen_code drives it: new TBs upwards, the newest one dropped
 * when another thread linked the same TB first (and its code space taken
 * again), old invalidated TBs recycled in place, lookups anywhere, TB
 * flushes. test-region-tbs.py puts the production code in place of
 * PRODUCTION.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define g_assert(x) assert(x)
#define g_renew(t, p, n) ((t *)realloc((p), sizeof(t) * (n)))
#define likely(x) (x)
#define TRUE 1
#define FALSE 0
typedef int gboolean;
typedef void *gpointer;
typedef gboolean (*GTraverseFunc)(gpointer key, gpointer value, gpointer data);
typedef struct { int locked; } QemuMutex;
static void qemu_mutex_lock(QemuMutex *m) { assert(!m->locked); m->locked = 1; }
static void qemu_mutex_unlock(QemuMutex *m) { assert(m->locked); m->locked = 0; }

struct tb_tc {
    const void *ptr;
    size_t size;
};
typedef struct TranslationBlock {
    int jmp_lock;            /* destroyed: 0 */
    struct tb_tc tc;
} TranslationBlock;
static void qemu_spin_destroy(int *l) { *l = 0; }

#define BUF (1 << 20)
#define NTB 40000
static unsigned char buf[BUF];
static TranslationBlock tbs[NTB];
static struct { size_t n; } region = { 1 };
static void *region_trees;
static size_t tree_size;
static uint64_t seed = 42;

static uint64_t rnd(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}

struct tcg_region_tree;
static struct tcg_region_tree *tc_ptr_to_region_tree(const void *p)
{
    const unsigned char *c = p;
    return c >= buf && c <= buf + BUF ? region_trees : NULL;
}

/* PRODUCTION */

/* the reference: which TBs are in the index */
static bool live[NTB];
static unsigned ntb, visited, last_seen;

static gboolean visit(gpointer key, gpointer value, gpointer data)
{
    TranslationBlock *tb = value;
    unsigned i = tb - tbs;

    assert(key == &tb->tc && live[i]);
    assert(visited == 0 || i > last_seen);
    last_seen = i;
    visited++;
    return data != NULL && visited == *(unsigned *)data;
}

static void check(void)
{
    unsigned want = 0;

    for (unsigned i = 0; i < ntb; i++) {
        want += live[i];
    }
    assert(tcg_nb_tbs() == want);
    visited = 0;
    tcg_tb_foreach(visit, NULL);
    assert(visited == want);
    if (want > 1) {
        /* stopping early */
        unsigned stop = 1 + rnd() % (want - 1);
        visited = 0;
        tcg_tb_foreach(visit, &stop);
        assert(visited == stop);
    }
    for (unsigned k = 0; k < 64; k++) {
        size_t off = rnd() % (BUF + 1);
        const unsigned char *p = buf + off;
        TranslationBlock *expect = NULL;
        unsigned lo = 0, hi = ntb;

        /* TBs are made upwards: only the last one starting at or below
         * p can hold it */
        while (lo < hi) {
            unsigned mid = (lo + hi) / 2;
            if ((const unsigned char *)tbs[mid].tc.ptr <= p) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo && live[lo - 1] &&
            p < (const unsigned char *)tbs[lo - 1].tc.ptr + tbs[lo - 1].tc.size) {
            expect = &tbs[lo - 1];
        }
        assert(tcg_tb_lookup((uintptr_t)p) == expect);
    }
    assert(tcg_tb_lookup((uintptr_t)buf + BUF + 4096) == NULL);
}

int main(void)
{
    unsigned cases = 0, recycles = 0, drops = 0, flushes = 0;
    bool grown = false;

    tree_size = sizeof(struct tcg_region_tree);
    region_trees = calloc(1, tree_size);

    for (unsigned trial = 0; trial < 40; trial++) {
        size_t next = rnd() % 64;      /* the region's code pointer */

        for (unsigned step = 0; step < 40000; step++) {
            unsigned op = rnd() % 16;

            if (op < 9 && ntb < NTB) {
                /* a new TB, above the last; its code may run up to the
                 * next one's start, or leave a gap */
                size_t size = 1 + rnd() % 48, gap = rnd() % 3 ? 0 : rnd() % 16;
                if (next + size + gap > BUF) {
                    continue;
                }
                TranslationBlock *tb = &tbs[ntb];
                tb->jmp_lock = 1;
                tb->tc.ptr = buf + next + gap;
                tb->tc.size = size;
                tcg_tb_insert(tb);
                live[ntb++] = true;
                next += gap + size;
                if (rnd() % 8 == 0) {
                    /* someone else's TB won: dropped, its space reused */
                    tcg_tb_remove(tb);
                    assert(tb->jmp_lock == 0);
                    live[--ntb] = false;
                    next = (const unsigned char *)tb->tc.ptr - buf - gap;
                    drops++;
                }
            } else if (op < 13 && ntb) {
                /* an old TB recycled in its own place, then perhaps
                 * dropped there */
                unsigned i = rnd() % ntb;
                tbs[i].jmp_lock = 1;
                tcg_tb_insert(&tbs[i]);
                live[i] = true;
                recycles++;
                if (rnd() % 4 == 0) {
                    tcg_tb_remove(&tbs[i]);
                    assert(tbs[i].jmp_lock == 0);
                    live[i] = false;
                }
            } else if (op == 13 && rnd() % 1024 == 0) {
                /* a flush */
                tcg_region_tree_reset_all();
                for (unsigned i = 0; i < ntb; i++) {
                    assert(!live[i] || tbs[i].jmp_lock == 0);
                    live[i] = false;
                }
                ntb = 0;
                next = rnd() % 64;
                flushes++;
            }
            grown |= ((struct tcg_region_tree *)region_trees)->cap > 4096;
            if (step % 64 == 0) {
                check();
                cases++;
            }
        }
        tcg_region_tree_reset_all();
        memset(live, 0, sizeof(live));
        ntb = 0;
    }
    assert(recycles > 10000 && drops > 10000 && flushes > 20 && grown);
    printf("region TBs: %u checks, %u recycles, %u drops, %u flushes\n",
           cases, recycles, drops, flushes);
    return 0;
}
