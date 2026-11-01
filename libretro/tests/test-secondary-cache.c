/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr,"secondary cache: %u: %s\n",__LINE__,#c); _Exit(1); } } while(0)
#define tcg_debug_assert assert
#define likely(c) (c)
#define CF_INVALID 0x80000000u
#define CF_PCREL 0x40000000u
#define unlikely(c) (c)
#define TB_JMP_CACHE_SIZE 256
#define XEMU_TB_CF_ANY 0xc0000004u
typedef uint64_t vaddr;
typedef struct TranslationBlock { uint64_t pc,cs_base; uint32_t flags,cflags; } TranslationBlock;
typedef struct { uint64_t pc,cs_base; uint32_t flags,cflags; } TCGTBCPUState;
typedef struct { TranslationBlock *tb; uint64_t pc; } Entry;
typedef struct { Entry array[256],classes[256]; } CPUJumpCache;
typedef struct { CPUJumpCache *tb_jmp_cache; } CPUState;
static int xemu_cpu_boost,global_lookups;
#define retro_atomic_load_relaxed_ptr(p) (*(p))
#define retro_atomic_store_relaxed_ptr(p,v) (*(p)=(v))
static unsigned tb_jmp_cache_hash_func(uint64_t pc) { return pc&255; }
static unsigned tb_jmp_cache_class_hash(uint64_t pc,uint32_t flags) { return (pc^((flags>>30)*8))&255; }
static uint32_t tb_cflags(const TranslationBlock *tb) { return tb->cflags; }
static TranslationBlock *tb_htable_lookup(CPUState *cpu,TCGTBCPUState s) { (void)cpu;(void)s;global_lookups++;return NULL; }
/* STORE */
/* FLUSH */
/* PRODUCTION */
int main(void) {
 unsigned cases=0;
 for(unsigned boost=0;boost<2;boost++)for(unsigned primary=0;primary<4;primary++)
 for(unsigned wrong=0;wrong<6;wrong++) {
  CPUJumpCache cache={0};CPUState cpu={&cache};
  TCGTBCPUState s={.pc=0x1234,.cs_base=0x1000,.flags=0x40000000,.cflags=123};
  TranslationBlock wanted={s.pc,s.cs_base,s.flags,s.cflags},other={s.pc+256,0,0,0};
  unsigned h=tb_jmp_cache_hash_func(s.pc),cls=tb_jmp_cache_class_hash(s.pc,s.flags);
  cache.classes[cls]=(Entry){&wanted,s.pc};
  if(primary==1)cache.array[h]=(Entry){&other,other.pc};
  if(primary==2){other.pc=s.pc;other.cflags=CF_INVALID;cache.array[h]=(Entry){&other,s.pc};}
  if(primary==3)cache.array[h]=(Entry){NULL,s.pc+256};
  switch(wrong) {
   case 1:cache.classes[cls].pc+=256;break;
   case 2:wanted.cs_base++;break;
   case 3:wanted.flags^=8;break;
   case 4:wanted.cflags|=CF_INVALID;break;
   case 5:cache.classes[cls].tb=NULL;break;
  }
  xemu_cpu_boost=boost;global_lookups=0;
  TranslationBlock *got=tb_lookup(&cpu,s);bool valid=boost && !wrong;
  assert(got==(valid?&wanted:NULL));assert(global_lookups==!valid);
  if(valid) {assert(cache.array[h].tb==&wanted && cache.array[h].pc==s.pc);assert(tb_lookup(&cpu,s)==&wanted);assert(!global_lookups);}
  cases++;
 }
 /* Populate variants through the actual store path, displace the primary,
  * then recover or reject the secondary identity and flush both arrays. */
 for(unsigned boost=0;boost<2;boost++)for(unsigned invalid=0;invalid<2;invalid++)
 for(unsigned relative=0;relative<2;relative++) {
  CPUJumpCache cache={0};CPUState cpu={&cache};
  uint32_t cflags=123|(relative?CF_PCREL:0);
  uint64_t pc=0x1234;
  TranslationBlock a={relative?0xdeadbeef:pc,0,0x40000000,cflags};
  TranslationBlock variant={pc,0,0x80000000,cflags},other={pc+256,0,0,123};
  xemu_cpu_boost=boost;global_lookups=0;
  tb_jmp_cache_store(&cache,pc,a.flags,&a);
  tb_jmp_cache_store(&cache,pc,variant.flags,&variant);
  tb_jmp_cache_store(&cache,other.pc,other.flags,&other);
  if(invalid)a.cflags|=CF_INVALID;
  TCGTBCPUState want={pc,a.cs_base,a.flags,cflags};
  bool retained=boost && !invalid;
  assert(tb_lookup(&cpu,want)==(retained?&a:NULL));
  assert(global_lookups==!retained);
  tcg_flush_jmp_cache(&cpu);
  for(unsigned i=0;i<256;i++)assert(!cache.array[i].tb && !cache.classes[i].tb);
  cases++;
 }
 CPUState unallocated={0};tcg_flush_jmp_cache(&unallocated);
 printf("secondary cache: %u primary collision/empty/invalidation/PC/mode/boost cases passed\n",cases);
}
