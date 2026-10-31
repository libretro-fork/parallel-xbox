/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_SOFTMMU 1
#define TARGET_PAGE_BITS 12
#define XEMU_TB_CF_ANY ((3u<<30)|4u)
#define TB_JMP_CACHE_BITS (xemu_cpu_boost ? 16 : 12)
#define TB_JMP_CACHE_SIZE (1<<TB_JMP_CACHE_BITS)
#define TB_JMP_PAGE_BITS (TB_JMP_CACHE_BITS/2)
#define TB_JMP_PAGE_SIZE (1<<TB_JMP_PAGE_BITS)
#define TB_JMP_ADDR_MASK (TB_JMP_PAGE_SIZE-1)
#define TB_JMP_PAGE_MASK (TB_JMP_CACHE_SIZE-TB_JMP_PAGE_SIZE)
#define CF_INVALID (1u<<16)
#define CF_PCREL (1u<<17)
#define likely(x) (x)
#define unlikely(x) (x)
#define tcg_debug_assert assert
#define retro_atomic_load_relaxed_ptr(p) (*(p))
#define retro_atomic_store_relaxed_ptr(p,v) (*(p)=(v))
#define CPU_FOREACH(cpu) for(cpu=cpus;cpu<cpus+2;cpu++)
typedef uint64_t vaddr;
typedef struct {vaddr pc,cs_base;uint32_t flags,cflags;} TranslationBlock;
typedef struct {struct {TranslationBlock *tb;vaddr pc;} array[65536],classes[65536];} CPUJumpCache;
typedef struct {CPUJumpCache *tb_jmp_cache;} CPUState;
typedef struct {vaddr pc,cs_base;uint32_t flags,cflags;} TCGTBCPUState;
static CPUJumpCache caches[2];
static CPUState cpus[2]={{&caches[0]},{&caches[1]}};
static bool xemu_cpu_boost;
static unsigned table_lookups;
static TranslationBlock *table_answer;
static uint32_t tb_cflags(TranslationBlock *tb){return tb->cflags;}
static TranslationBlock *tb_htable_lookup(CPUState *cpu,TCGTBCPUState s){table_lookups++;return table_answer;}
/* PRODUCTION */
static uint32_t flags(unsigned cls){return ((cls&3u)<<30)|(cls&4u);}
static unsigned seed=0x71a2c981;
static unsigned random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
 unsigned cases=0;
 for(unsigned boost=0;boost<2;boost++){
  xemu_cpu_boost=boost;
  for(unsigned sample=0;sample<1500;sample++){
   vaddr pc=((uint64_t)random32()<<32)|random32();
   unsigned primary=tb_jmp_cache_hash_func(pc),seen[7];uint32_t mode=random32()&~XEMU_TB_CF_ANY;
   TranslationBlock variants[7];
   tcg_flush_jmp_cache(&cpus[0]);tcg_flush_jmp_cache(&cpus[1]);
   for(unsigned cls=0;cls<7;cls++){
    unsigned h=tb_jmp_cache_class_hash(pc,mode|flags(cls));seen[cls]=h;
    assert(h<(unsigned)TB_JMP_CACHE_SIZE);
    assert((h&~TB_JMP_ADDR_MASK)==tb_jmp_cache_hash_page(pc&~4095ull));
    for(unsigned other=0;other<cls;other++)assert(h!=seen[other]);
    variants[cls]=(TranslationBlock){pc,0,mode|flags(cls),0};
    tb_jmp_cache_store(&caches[0],pc,variants[cls].flags,&variants[cls]);
    assert(caches[0].array[primary].tb==&variants[cls]);
    if(boost && cls)assert(caches[0].classes[h].tb==&variants[cls]);
    else if(boost)assert(!caches[0].classes[h].tb);
    else assert(!caches[0].classes[h].tb);
   }
   table_answer=NULL;
   for(unsigned cls=0;cls<7;cls++){
    TCGTBCPUState s={pc,0,mode|flags(cls),0};table_lookups=0;
    assert(tb_lookup(&cpus[0],s)==((boost||cls==6)?&variants[cls]:NULL));
    if(boost){assert(!table_lookups);assert(caches[0].array[primary].tb==&variants[cls]);}
    /* Force the secondary-only path to check every identity guard. */
    if(boost){
     caches[0].array[primary].tb=&variants[(cls+1)%7];TranslationBlock *tb=&variants[cls];
     tb->cflags|=CF_INVALID;assert(!tb_lookup(&cpus[0],s));tb->cflags=0;
     s.cs_base=4;assert(!tb_lookup(&cpus[0],s));s.cs_base=0;
     s.flags^=1u<<18;assert(!tb_lookup(&cpus[0],s));s.flags^=1u<<18;
     s.cflags=1;assert(!tb_lookup(&cpus[0],s));s.cflags=0;
     caches[0].classes[seen[cls]].pc^=4096;assert(!tb_lookup(&cpus[0],s));
     caches[0].classes[seen[cls]].pc=pc;
    }
    cases++;
   }
   for(unsigned cpu=0;cpu<2;cpu++){
    tb_jmp_cache_store(&caches[cpu],pc,variants[2].flags,&variants[2]);
    tb_jmp_cache_store(&caches[cpu],pc,variants[3].flags,&variants[3]);
   }
   tb_jmp_cache_inval_tb(&variants[3]);
   for(unsigned cpu=0;cpu<2;cpu++){
    assert(!caches[cpu].array[primary].tb);
    if(boost)assert(!caches[cpu].classes[seen[3]].tb);
   }
   /* Independent code stays solely in the original primary cache. */
   tcg_flush_jmp_cache(&cpus[0]);TranslationBlock any={pc,0,mode|XEMU_TB_CF_ANY,0};
   table_answer=&any;assert(tb_lookup(&cpus[0],(TCGTBCPUState){pc,0,mode,0})==&any);
   assert(caches[0].array[primary].tb==&any);
   for(unsigned cls=0;cls<7;cls++)assert(!caches[0].classes[seen[cls]].tb);
   /* PC-relative code invalidation and page remaps clear/reject both banks. */
   table_answer=NULL;variants[0].cflags=variants[1].cflags=CF_PCREL;
   tb_jmp_cache_store(&caches[0],pc,mode,&variants[0]);
   tb_jmp_cache_store(&caches[0],pc,mode|flags(1),&variants[1]);
   variants[0].cflags|=CF_INVALID;tb_jmp_cache_inval_tb(&variants[0]);
   assert(!tb_lookup(&cpus[0],(TCGTBCPUState){pc,0,mode,CF_PCREL}));
   tb_jmp_cache_clear_page(&cpus[0],pc&~4095ull);
   assert(!caches[0].array[primary].tb);
   for(unsigned cls=0;cls<7;cls++)assert(!caches[0].classes[seen[cls]].tb);
   cases++;
  }
 }
 printf("Secondary class cache: %u store/lookup/promotion/identity/invalidation/flush scenarios passed\n",cases);
}
