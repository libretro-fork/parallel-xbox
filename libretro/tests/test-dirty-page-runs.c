/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <retro_atomic.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define TARGET_PAGE_BITS 12
#define PAGE UINT64_C(4096)
#define TARGET_PAGE_ALIGN(x) (((x)+PAGE-1)&~(PAGE-1))
#define BITS_PER_LONG (sizeof(unsigned long)*CHAR_BIT)
#ifdef REAL_BLOCK_SIZE
#define DIRTY_MEMORY_BLOCK_SIZE (256u*1024u*8u)
#else
#define DIRTY_MEMORY_BLOCK_SIZE 128u
#endif
#define PAGES (4u*DIRTY_MEMORY_BLOCK_SIZE)
#define WORDS (PAGES/BITS_PER_LONG)
#define DIRTY_MEMORY_NUM 2
/* BITMAP ACCESS */
static unsigned long *inject_word;
static unsigned long inject_before,inject_after;
static unsigned long load_word(unsigned long *p)
{
 unsigned long value=bitmap_word_read(p);
 if(p==inject_word&&inject_before){bitmap_word_fetch_or(p,inject_before);inject_before=0;}
 return value;
}
static unsigned long clear_word(unsigned long *p,unsigned long mask)
{
 unsigned long value=bitmap_word_fetch_and(p,mask);
 if(p==inject_word&&inject_after){bitmap_word_fetch_or(p,inject_after);inject_after=0;}
 return value;
}
#undef bitmap_word_read
#undef bitmap_word_fetch_and
#define bitmap_word_read(p) load_word(p)
#define bitmap_word_fetch_and(p,m) clear_word(p,m)
typedef uint64_t hwaddr;
typedef uint64_t ram_addr_t;
typedef struct { unsigned long *blocks[4]; } DirtyMemoryBlocks;
typedef struct { unsigned dummy; } MemoryRegion;
typedef struct {MemoryRegion *mr;ram_addr_t offset,used_length;} RAMBlock;
static struct {retro_atomic_ptr_t dirty_memory[2];} ram_list;
static MemoryRegion region;
static RAMBlock ramblock;
static DirtyMemoryBlocks clients[2];
static unsigned long *words[2],*expected;
static unsigned guard,clear_calls,notify_clear_calls;
static hwaddr clear_offset,clear_size;
#define WITH_RCU_READ_LOCK_GUARD() for(unsigned _once=(guard=1);_once;_once=0,guard=0)
static RAMBlock *qemu_get_ram_block(ram_addr_t start){assert(start>=ramblock.offset);return &ramblock;}
static void memory_region_clear_dirty_bitmap(MemoryRegion *mr,hwaddr a,hwaddr n)
{assert(guard&&mr==&region);clear_calls++;clear_offset=a;clear_size=n;}
static void physical_memory_dirty_bits_cleared(hwaddr a,hwaddr n)
{(void)a;(void)n;assert(!guard);notify_clear_calls++;}
/* PRODUCTION */
/* BASELINE */
typedef struct {hwaddr offset,size;} Run;
static Run actual[1024],reference[1024];
static unsigned nr,nref;static bool record_runs=true;
static hwaddr notified_bytes,stream_hash;
static void notify(void *opaque,hwaddr offset,hwaddr size)
{
 (void)opaque;assert(guard&&size);if(record_runs){assert(nr<1024);actual[nr]=(Run){offset,size};}nr++;
 notified_bytes+=size;stream_hash=stream_hash*UINT64_C(1000003)+offset+size*3;
}
static bool bit(unsigned long *data,size_t p){return(data[p/BITS_PER_LONG]>>(p%BITS_PER_LONG))&1;}
static void set_bit(unsigned long *data,size_t p,bool set)
{unsigned long m=1UL<<(p%BITS_PER_LONG);if(set)data[p/BITS_PER_LONG]|=m;else data[p/BITS_PER_LONG]&=~m;}
static void reset_events(void){guard=clear_calls=notify_clear_calls=nr=nref=0;notified_bytes=stream_hash=0;}
static void check_case(hwaddr start,hwaddr length,bool callback)
{
 memcpy(expected,words[0],WORDS*sizeof(unsigned long));reset_events();
 bool dirty=false,in_run=false;hwaddr run=0;
 if(length) {
  size_t first=start/PAGE,last=TARGET_PAGE_ALIGN(start+length)/PAGE;
  for(size_t p=first;p<last;p++) {
   bool set=bit(expected,p);set_bit(expected,p,false);dirty|=set;
   if(set&&!in_run){run=p;in_run=true;}
   if(!set&&in_run){hwaddr a=MAX(run*PAGE,start),b=MIN(p*PAGE,start+length);reference[nref++]=(Run){a-start,b-a};in_run=false;}
  }
  if(in_run){hwaddr a=MAX(run*PAGE,start);reference[nref++]=(Run){a-start,start+length-a};}
 }
 bool got=physical_memory_test_and_clear_dirty_ranges(start,length,0,callback?notify:NULL,NULL);
 assert(got==dirty&&!guard);
 assert(!memcmp(expected,words[0],WORDS*sizeof(unsigned long)));
 for(size_t i=0;i<WORDS;i++)assert(words[1][i]==0xA5A5A5A5UL);
 assert(clear_calls==(length?1u:0u)&&notify_clear_calls==(dirty?1u:0u));
 if(length){assert(clear_offset==(start/PAGE)*PAGE-ramblock.offset);assert(clear_size==TARGET_PAGE_ALIGN(start+length)-(start/PAGE)*PAGE);}
 if(callback){assert(nr==nref);for(unsigned i=0;i<nr;i++){assert(actual[i].offset==reference[i].offset&&actual[i].size==reference[i].size);}}
 else assert(!nr);
}
static unsigned correctness(void)
{
 unsigned cases=0;
 size_t starts[]={3,4,BITS_PER_LONG-1,BITS_PER_LONG,BITS_PER_LONG+1,DIRTY_MEMORY_BLOCK_SIZE-1,DIRTY_MEMORY_BLOCK_SIZE,DIRTY_MEMORY_BLOCK_SIZE+1,2*DIRTY_MEMORY_BLOCK_SIZE-1};
 unsigned patterns=4096;
#ifdef REAL_BLOCK_SIZE
 patterns=64;
#endif
 for(unsigned g=0;g<sizeof(starts)/sizeof(starts[0]);g++)for(unsigned fraction=0;fraction<3;fraction++)for(unsigned p=0;p<patterns;p++) {
  memset(words[0],0,WORDS*sizeof(unsigned long));size_t first=starts[g];
  for(unsigned i=0;i<12;i++)set_bit(words[0],first+i,((p>>i)&1)!=0);
  set_bit(words[0],first-1,true);set_bit(words[0],first+20,true);
  hwaddr off=fraction==0?0:fraction==1?17:4095;
  check_case(first*PAGE+off,12*PAGE+37,(p&1)==0);cases++;
 }
 for(unsigned g=0;g<sizeof(starts)/sizeof(starts[0]);g++)for(unsigned pattern=0;pattern<4;pattern++) {
  memset(words[0],0,WORDS*sizeof(unsigned long));size_t start=starts[g];
  for(size_t p=start;p<start+2*BITS_PER_LONG+3;p++)set_bit(words[0],p,pattern==0|| (pattern==1&&p%2)|| (pattern==2&&p%BITS_PER_LONG==0));
  check_case(start*PAGE+3,(2*BITS_PER_LONG+2)*PAGE-5,true);cases++;
 }
 memset(words[0],0xff,WORDS*sizeof(unsigned long));check_case((PAGES-1)*PAGE+4095,1,true);cases++;
 check_case(3*PAGE+17,0,true);cases++;
 /* A write after the empty observation must remain for the next scan. */
 memset(words[0],0,WORDS*sizeof(unsigned long));reset_events();size_t p=64;inject_word=&words[0][p/BITS_PER_LONG];inject_before=1UL<<(p%BITS_PER_LONG);
 assert(!physical_memory_test_and_clear_dirty_ranges(p*PAGE,PAGE,0,notify,NULL));assert(bit(words[0],p)&&!nr);check_case(p*PAGE,PAGE,true);cases++;
 /* Fetch-and captures a write after its initial load; later writes survive. */
 memset(words[0],0,WORDS*sizeof(unsigned long));reset_events();set_bit(words[0],64,true);inject_word=&words[0][64/BITS_PER_LONG];inject_before=1UL<<(65%BITS_PER_LONG);inject_after=1UL<<(66%BITS_PER_LONG);
 assert(physical_memory_test_and_clear_dirty_ranges(64*PAGE,3*PAGE,0,notify,NULL));assert(nr==1&&actual[0].offset==0&&actual[0].size==2*PAGE);assert(!bit(words[0],64)&&!bit(words[0],65)&&bit(words[0],66));
 inject_word=NULL;check_case(64*PAGE,3*PAGE,true);cases++;
 return cases;
}
int main(void)
{
 for(unsigned c=0;c<2;c++){words[c]=calloc(WORDS,sizeof(unsigned long));assert(words[c]);for(unsigned b=0;b<4;b++)clients[c].blocks[b]=words[c]+b*(DIRTY_MEMORY_BLOCK_SIZE/BITS_PER_LONG);retro_atomic_store_relaxed_ptr(&ram_list.dirty_memory[c],&clients[c]);}
 expected=malloc(WORDS*sizeof(unsigned long));assert(expected);for(size_t i=0;i<WORDS;i++)words[1][i]=0xA5A5A5A5UL;
 ramblock=(RAMBlock){&region,3*PAGE,(PAGES-3)*PAGE};
 unsigned cases=correctness();printf("Dirty runs: %u cases, %zu-bit words, block=%u pages; exact clipped callbacks, outside/client preservation, clear ordering and late writes PASS\n",cases,BITS_PER_LONG,DIRTY_MEMORY_BLOCK_SIZE);
 /* BENCHMARK CALL */
 free(expected);free(words[0]);free(words[1]);return 0;
}
