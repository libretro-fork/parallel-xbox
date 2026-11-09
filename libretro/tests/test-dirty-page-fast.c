/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <assert.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL dirty-page line %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
typedef uint64_t ram_addr_t;
/* CLIENTS */
#define TARGET_PAGE_BITS 12
#define TARGET_PAGE_ALIGN(x) (((x)+4095)&~UINT64_C(4095))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define WORD_BITS (sizeof(unsigned long)*CHAR_BIT)
#define BLOCK_WORDS (DIRTY_MEMORY_BLOCK_SIZE/WORD_BITS)
typedef struct {unsigned long *blocks[3];} DirtyMemoryBlocks;
static DirtyMemoryBlocks clients[DIRTY_MEMORY_NUM];
static struct {void *dirty_memory[DIRTY_MEMORY_NUM];} ram_list;
static unsigned depth,locks,loads,bit_reads,range_scans;
static void unlock(unsigned *held){assert(*held&&depth==1);depth--;}
static unsigned lock(void){assert(depth==0);depth++;locks++;return 1;}
#define RCU_READ_LOCK_GUARD() unsigned held __attribute__((cleanup(unlock)))=lock()
#define WITH_RCU_READ_LOCK_GUARD() for(unsigned held __attribute__((cleanup(unlock)))=lock(),once=1;once;once=0)
static void *retro_atomic_load_acquire_ptr(void **p){assert(depth==1);loads++;return *p;}
static bool test_bit(unsigned long bit,const unsigned long *words){assert(depth==1);bit_reads++;return !!(words[bit/WORD_BITS]&(1ul<<(bit%WORD_BITS)));}
/* FUNCTIONS */
int main(void){
 for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++){ram_list.dirty_memory[c]=&clients[c];for(unsigned b=0;b<3;b++){clients[c].blocks[b]=malloc(BLOCK_WORDS*sizeof(unsigned long));assert(clients[c].blocks[b]);for(size_t w=0;w<BLOCK_WORDS;w++)clients[c].blocks[b][w]=(unsigned long)(UINT64_C(0xa128c37e91702d65)^(w*UINT64_C(0x917821))^c^b);}}
 const ram_addr_t pages[]={0,1,WORD_BITS-1,WORD_BITS,WORD_BITS+1,DIRTY_MEMORY_BLOCK_SIZE-1,DIRTY_MEMORY_BLOCK_SIZE,DIRTY_MEMORY_BLOCK_SIZE+1,2*DIRTY_MEMORY_BLOCK_SIZE-1,2*DIRTY_MEMORY_BLOCK_SIZE,3*DIRTY_MEMORY_BLOCK_SIZE-1};
 const unsigned offsets[]={0,1,4095};unsigned cases=0;
 for(unsigned p=0;p<ARRAY_SIZE(pages);p++)for(unsigned o=0;o<ARRAY_SIZE(offsets);o++)for(unsigned mask=0;mask<64;mask++){
  ram_addr_t page=pages[p];unsigned block=page/DIRTY_MEMORY_BLOCK_SIZE;unsigned long bit=page%DIRTY_MEMORY_BLOCK_SIZE;
  for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++){unsigned long *word=&clients[c].blocks[block][bit/WORD_BITS],flag=1ul<<(bit%WORD_BITS);*word=(*word&~flag)|((mask&(1u<<c))?flag:0);}
  for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++){
   locks=loads=bit_reads=range_scans=0;
   assert(physical_memory_get_dirty_flag((page<<12)+offsets[o],c)==!!(mask&(1u<<c)));
   assert(!depth&&locks==1&&loads==1&&bit_reads==1&&range_scans==0);
  }
  locks=loads=bit_reads=range_scans=0;bool clean=physical_memory_is_clean((page<<12)+offsets[o]);
  assert(clean==(mask!=63));assert(!depth);assert(locks==1);assert(loads>=1&&loads<=6&&bit_reads==loads);assert(range_scans==0);
  /* Fresh metadata generation borrows the same owned immutable bitmaps;
   * queries cannot retain a prior DirtyMemoryBlocks view across returns. */
  DirtyMemoryBlocks moved[DIRTY_MEMORY_NUM];for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++){moved[c]=clients[c];ram_list.dirty_memory[c]=&moved[c];}
  assert(physical_memory_is_clean((page<<12)+offsets[o])==clean&&!depth);
  for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++){
   locks=loads=bit_reads=range_scans=0;
   assert(physical_memory_get_dirty_flag((page<<12)+offsets[o],c)==!!(mask&(1u<<c)));
   assert(!depth&&locks==1&&loads==1&&bit_reads==1&&range_scans==0);
  }
  for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++)ram_list.dirty_memory[c]=&clients[c];cases+=2;
 }
 for(unsigned c=0;c<DIRTY_MEMORY_NUM;c++)for(unsigned b=0;b<3;b++)free(clients[c].blocks[b]);
 printf("Dirty page: %u mask/word/block/offset/metadata-generation cases plus 25344 single-client queries PASS; one RCU lend, no range scans\n",cases);return 0;}
