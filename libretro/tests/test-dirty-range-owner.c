/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL dirty-range line %d: %s\n",__LINE__,#x); _Exit(1); } } while (0)
typedef uint64_t ram_addr_t;
/* CLIENTS */
#define TARGET_PAGE_BITS 12
#define TARGET_PAGE_ALIGN(x) (((x)+4095)&~UINT64_C(4095))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define WORD_BITS (sizeof(unsigned long)*CHAR_BIT)
#define BLOCK_WORDS (DIRTY_MEMORY_BLOCK_SIZE/WORD_BITS)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
typedef struct { unsigned long *blocks[3]; } DirtyMemoryBlocks;
static DirtyMemoryBlocks clients[DIRTY_MEMORY_NUM];
static struct { void *dirty_memory[DIRTY_MEMORY_NUM]; } ram_list;
static unsigned depth, locks, loads, order[DIRTY_MEMORY_NUM];
static void unlock(unsigned *held) { assert(*held && depth==1); depth--; }
static unsigned lock(void) { assert(!depth); depth++; locks++; return 1; }
#define RCU_READ_LOCK_GUARD() unsigned held __attribute__((cleanup(unlock)))=lock()
static void *retro_atomic_load_acquire_ptr(void **p) {
    assert(depth==1 && loads<DIRTY_MEMORY_NUM);
    order[loads++]=(unsigned)(p-ram_list.dirty_memory);
    return *p;
}
static unsigned long find_next_zero_bit(const unsigned long *p,unsigned long n,unsigned long at) {
    assert(depth==1 && at<=n && n<=DIRTY_MEMORY_BLOCK_SIZE);
    for (;at<n;at++) if (!(p[at/WORD_BITS] & (1ul<<(at%WORD_BITS)))) return at;
    return n;
}
/* FUNCTIONS */
static void set_page(unsigned c,ram_addr_t page,bool dirty) {
    unsigned long *w=&clients[c].blocks[page/DIRTY_MEMORY_BLOCK_SIZE][page%DIRTY_MEMORY_BLOCK_SIZE/WORD_BITS];
    unsigned long bit=1ul<<(page%WORD_BITS);
    *w=dirty?(*w|bit):(*w&~bit);
}
static uint8_t reference(ram_addr_t start,ram_addr_t length,unsigned mask) {
    uint8_t result=0;
    ram_addr_t end=TARGET_PAGE_ALIGN(start+length)>>12;
    for (unsigned c=0;c<DIRTY_MEMORY_NUM;c++) if (mask&(1u<<c)) {
        for (ram_addr_t p=start>>12;p<end;p++) {
            unsigned long w=clients[c].blocks[p/DIRTY_MEMORY_BLOCK_SIZE][p%DIRTY_MEMORY_BLOCK_SIZE/WORD_BITS];
            if (!(w&(1ul<<(p%WORD_BITS)))) { result|=1u<<c; break; }
        }
    }
    return result;
}
int main(void) {
    for (unsigned c=0;c<DIRTY_MEMORY_NUM;c++) {
        ram_list.dirty_memory[c]=&clients[c];
        for (unsigned b=0;b<3;b++) {
            clients[c].blocks[b]=malloc(BLOCK_WORDS*sizeof(unsigned long));
            assert(clients[c].blocks[b]);
            for (size_t w=0;w<BLOCK_WORDS;w++) clients[c].blocks[b][w]=~0ul;
        }
    }
    const ram_addr_t pages[]={0,1,WORD_BITS-1,WORD_BITS,DIRTY_MEMORY_BLOCK_SIZE-1,DIRTY_MEMORY_BLOCK_SIZE,2*DIRTY_MEMORY_BLOCK_SIZE-1};
    const unsigned offsets[]={0,1,4095};
    const ram_addr_t lengths[]={0,1,4095,4096,4097,(WORD_BITS+1)*4096};
    const unsigned sequence[]={DIRTY_MEMORY_NV2A,DIRTY_MEMORY_NV2A_TEX,DIRTY_MEMORY_NV2A_PVIDEO,DIRTY_MEMORY_VGA,DIRTY_MEMORY_CODE,DIRTY_MEMORY_MIGRATION};
    unsigned cases=0;
    for (unsigned p=0;p<ARRAY_SIZE(pages);p++) for (unsigned o=0;o<ARRAY_SIZE(offsets);o++)
    for (unsigned l=0;l<ARRAY_SIZE(lengths);l++) for (unsigned pattern=0;pattern<5;pattern++) {
        ram_addr_t start=(pages[p]<<12)+offsets[o],end=TARGET_PAGE_ALIGN(start+lengths[l])>>12;
        ram_addr_t cleared[DIRTY_MEMORY_NUM];
        for (unsigned c=0;c<DIRTY_MEMORY_NUM;c++) {
            /* Different consumers place holes at the first, middle, last,
             * or immediately outside the range. Pattern four has no holes. */
            unsigned where=(pattern+c)%4;
            cleared[c]=where==0?pages[p]:where==1?(pages[p]+end)/2:where==2?(end?end-1:0):end;
            if (pattern<4) set_page(c,cleared[c],false);
        }
        for (unsigned mask=0;mask<256;mask++) {
            uint8_t want=reference(start,lengths[l],mask);
            for (unsigned generation=0;generation<2;generation++) {
                DirtyMemoryBlocks moved[DIRTY_MEMORY_NUM];
                for (unsigned c=0;c<DIRTY_MEMORY_NUM;c++) { moved[c]=clients[c]; ram_list.dirty_memory[c]=generation?&moved[c]:&clients[c]; }
                locks=loads=0;
                assert(physical_memory_range_includes_clean(start,lengths[l],mask)==want);
                assert(reference(start,lengths[l],mask)==want);
                assert(!depth && locks==!!(mask&63));
                unsigned selected=0;
                for (unsigned c=0;c<DIRTY_MEMORY_NUM;c++) if (mask&(1u<<sequence[c])) assert(order[selected++]==sequence[c]);
                assert(loads==selected);
                cases++;
            }
        }
        for (unsigned c=0;c<DIRTY_MEMORY_NUM;c++) { ram_list.dirty_memory[c]=&clients[c]; if (pattern<4) set_page(c,cleared[c],true); }
    }
    for (unsigned c=0;c<DIRTY_MEMORY_NUM;c++) for (unsigned b=0;b<3;b++) free(clients[c].blocks[b]);
    printf("Dirty range: %u mask/boundary/hole/empty/generation cases PASS; one owner, ordered loads, zero owner for empty masks\n",cases);
    return 0;
}
