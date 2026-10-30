/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The production page collection (accel/tcg/tb-maint.c) against a
 * reference: the pages of a range and of every TB on them, collected and
 * locked in ascending order with contended trylocks, then unlocked.
 * test-page-collection.py puts the production code in place of
 * PRODUCTION.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TARGET_PAGE_BITS 12
#define NPAGES 96
#define NTBS 160
#define g_assert(x) assert(x)
#define g_new(t, n) ((t *)malloc(sizeof(t) * (n)))
#define g_renew(t, p, n) ((t *)realloc((p), sizeof(t) * (n)))
#define g_free(p) free(p)
#define assert_page_locked(pd) assert((pd)->lock == 1)

typedef uint64_t tb_page_addr_t;
typedef struct PageDesc {
    int lock;
    uintptr_t first_tb;
} PageDesc;
typedef struct TranslationBlock {
    tb_page_addr_t page_addr[2];
    uintptr_t page_next[2];
} TranslationBlock;

static PageDesc pds[NPAGES];
static bool present[NPAGES];
static TranslationBlock tbs[NTBS];
static unsigned busy_left, busy_hits, allocs;
static uint64_t seed = 42;

static uint64_t rnd(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 7;
    seed ^= seed << 17;
    return seed;
}

static PageDesc *page_find(tb_page_addr_t index)
{
    return index < NPAGES && present[index] ? &pds[index] : NULL;
}
static void page_lock(PageDesc *pd)
{
    assert(pd->lock == 0);
    pd->lock = 1;
}
/* false on success; busy now and then, as another thread's lock would be */
static bool page_trylock(PageDesc *pd)
{
    assert(pd->lock == 0);
    if (busy_left && rnd() % 3 == 0) {
        busy_left--;
        busy_hits++;
        return true;
    }
    pd->lock = 1;
    return false;
}
static void page_unlock(PageDesc *pd)
{
    assert(pd->lock == 1);
    pd->lock = 0;
}
static void assert_no_pages_locked(void)
{
    for (unsigned i = 0; i < NPAGES; i++) {
        assert(pds[i].lock == 0);
    }
}
static tb_page_addr_t tb_page_addr0(const TranslationBlock *tb)
{
    return tb->page_addr[0];
}
static tb_page_addr_t tb_page_addr1(const TranslationBlock *tb)
{
    return tb->page_addr[1];
}

/* PRODUCTION */

/* pages and TBs at random: a TB on one page or two, its pages present */
static void build(void)
{
    memset(pds, 0, sizeof(pds));
    memset(tbs, 0, sizeof(tbs));
    for (unsigned i = 0; i < NPAGES; i++) {
        present[i] = rnd() % 4 != 0;
    }
    unsigned ntbs = rnd() % NTBS;
    for (unsigned t = 0; t < ntbs; t++) {
        TranslationBlock *tb = &tbs[t];
        unsigned p0 = rnd() % NPAGES, p1;

        present[p0] = true;
        tb->page_addr[0] = (tb_page_addr_t)p0 << TARGET_PAGE_BITS;
        tb->page_addr[1] = -1;
        if (rnd() % 3 == 0) {
            /* the second page: the next one, or (rarely) a far one */
            p1 = rnd() % 4 ? (p0 + 1) % NPAGES : rnd() % NPAGES;
            if (p1 != p0) {
                present[p1] = true;
                tb->page_addr[1] = (tb_page_addr_t)p1 << TARGET_PAGE_BITS;
            }
        }
        for (unsigned n = 0; n < 2; n++) {
            if (tb->page_addr[n] == (tb_page_addr_t)-1) {
                continue;
            }
            PageDesc *pd = &pds[tb->page_addr[n] >> TARGET_PAGE_BITS];
            tb->page_next[n] = pd->first_tb;
            pd->first_tb = (uintptr_t)tb | n;
        }
    }
}

static void check(tb_page_addr_t first, tb_page_addr_t last)
{
    bool want[NPAGES] = { 0 };
    unsigned nwant = 0;

    for (tb_page_addr_t i = first; i <= last; i++) {
        PageDesc *pd = page_find(i);
        TranslationBlock *tb;
        uintptr_t n;

        if (!pd) {
            continue;
        }
        want[i] = true;
        PAGE_FOR_EACH_TB(unused, unused, pd, tb, n) {
            want[tb->page_addr[0] >> TARGET_PAGE_BITS] = true;
            if (tb->page_addr[1] != (tb_page_addr_t)-1) {
                want[tb->page_addr[1] >> TARGET_PAGE_BITS] = true;
            }
        }
    }
    for (unsigned i = 0; i < NPAGES; i++) {
        nwant += want[i];
    }

    struct page_collection set;
    busy_left = rnd() % 8;
    page_collection_lock(&set, first << TARGET_PAGE_BITS,
                         (last << TARGET_PAGE_BITS) | (rnd() & 4095));
    assert(set.n == nwant);
    assert(set.n <= set.cap);
    assert((set.entries == set.inl) == (set.cap == PAGE_COLLECTION_INLINE));
    allocs += set.entries != set.inl;
    for (unsigned k = 0; k < set.n; k++) {
        assert(set.entries[k].locked);
        assert(set.entries[k].pd == &pds[set.entries[k].index]);
        assert(k == 0 || set.entries[k - 1].index < set.entries[k].index);
        assert(want[set.entries[k].index]);
    }
    for (unsigned i = 0; i < NPAGES; i++) {
        assert(pds[i].lock == (int)want[i]);
    }
    page_collection_unlock(&set);
    assert_no_pages_locked();
}

int main(void)
{
    unsigned cases = 0;

    for (unsigned trial = 0; trial < 20000; trial++) {
        build();
        for (unsigned r = 0; r < 8; r++) {
            tb_page_addr_t first = rnd() % NPAGES, last;

            /* one page (a code write), a few, or a large DMA range */
            last = r < 4 ? first
                 : r < 6 ? first + rnd() % 4
                 : first + rnd() % (NPAGES - first);
            if (last >= NPAGES) {
                last = NPAGES - 1;
            }
            check(first, last);
            cases++;
        }
    }
    assert(busy_hits > 1000 && allocs > 1000);
    printf("page collection: %u cases, %u busy retries, %u grown arrays\n",
           cases, busy_hits, allocs);
    return 0;
}
