/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#define CF_INVALID 0x4000u
#define CF_USE_ICOUNT 0x2000u
#define CF_MEMI_ONLY 0x1000u
#define CF_NOIRQ 0x10000u
#define CF_PCREL 0x20000u
#define CPU_LOG_EXEC 1
#define VADDR_PRIx "llx"
#define g_assert assert
#define qatomic_set(p,v) (*(p)=(v))
#define retro_atomic_load_relaxed_u16(p) (*(p))
#define retro_atomic_store_relaxed_u16(p,v) (*(p)=(v))
#define retro_atomic_store_relaxed_int(p,v) (*(p)=(v))
typedef uint64_t vaddr;
typedef uint64_t tb_page_addr_t;
typedef struct TranslationBlock {vaddr pc;uint64_t cs_base;uint32_t flags,cflags;tb_page_addr_t physical;unsigned jmp_lock;} TranslationBlock;
typedef struct CPUState CPUState;
typedef struct CPUClass {vaddr(*get_pc)(CPUState*);struct TCGOps {bool(*io_recompile_replay_branch)(CPUState*,TranslationBlock*);}*tcg_ops;} CPUClass;
struct CPUState {CPUClass*cc;vaddr pc;uint32_t cflags_next_tb;struct{union{struct{uint16_t low,high;}u16;}icount_decr;}neg;};
typedef struct {TranslationBlock*slot;uint32_t hash;} Table;
static struct {Table htable,inv_htable;}tb_ctx;
static unsigned pages_locked,page_removed,cache_removed,jumps_removed[2],unlinked,restored;
static jmp_buf escaped;
static vaddr get_pc(CPUState*c){return c->pc;}
static uint32_t tb_cflags(TranslationBlock*t){return t->cflags;}
static tb_page_addr_t tb_page_addr0(TranslationBlock*t){return t->physical;}
static uint32_t tb_hash_func(uint64_t phys,uint64_t pc,uint32_t flags,uint64_t cs,uint32_t cf){return phys^pc^flags^cs^cf;}
static void assert_memory_lock(void){}
static void qemu_spin_lock(unsigned*p){assert(!*p);*p=1;}
static void qemu_spin_unlock(unsigned*p){assert(*p);*p=0;}
static bool qht_remove(Table*t,TranslationBlock*b,uint32_t h){if(t->slot!=b)return false;assert(t->hash==h);t->slot=NULL;return true;}
static void qht_insert(Table*t,TranslationBlock*b,uint32_t h,void**old){*old=t->slot;t->slot=b;t->hash=h;}
static void tb_lock_pages(TranslationBlock*t){assert(!pages_locked);pages_locked=1;}
static void tb_unlock_pages(TranslationBlock*t){assert(pages_locked);pages_locked=0;}
static void tb_remove(TranslationBlock*t){assert(pages_locked);page_removed++;}
static void tb_jmp_cache_inval_tb(TranslationBlock*t){cache_removed++;}
static void tb_remove_from_jmp_list(TranslationBlock*t,unsigned n){assert(n<2);jumps_removed[n]++;}
static void tb_jmp_unlink(TranslationBlock*t){unlinked++;}
static TranslationBlock*tcg_tb_lookup(uintptr_t retaddr){assert(retaddr==42);return tb_ctx.htable.slot;}
static void cpu_restore_state_from_tb(CPUState*c,TranslationBlock*t,uintptr_t r){restored++;}
static void cpu_abort(CPUState*c,const char*f,...){abort();}
static uint32_t curr_cflags(CPUState*c){return CF_USE_ICOUNT;}
static bool qemu_loglevel_mask(int m){return false;}
static bool qemu_log_in_addr_range(vaddr a){return false;}
static void qemu_log(const char*f,...){abort();}
static void cpu_loop_exit_noexc(CPUState*c){longjmp(escaped,1);}
/* PRODUCTION */
static uint32_t original_hash(TranslationBlock*t){return tb_hash_func(t->physical,(t->cflags&CF_PCREL)?0:t->pc,t->flags,t->cs_base,t->cflags);}
static void setup(TranslationBlock*t)
{
 memset(&tb_ctx,0,sizeof(tb_ctx));tb_ctx.htable.slot=t;tb_ctx.htable.hash=original_hash(t);
 pages_locked=page_removed=cache_removed=jumps_removed[0]=jumps_removed[1]=unlinked=restored=0;
}
static void recompile(CPUState*cpu)
{
 if(!setjmp(escaped))cpu_io_recompile(cpu,42);
}
int main(void)
{
 struct TCGOps ops={0};CPUClass cc={.get_pc=get_pc,.tcg_ops=&ops};
 /* Actually execute production cpu_io_recompile: the old block's bytes
  * can be identical, but it must not be available to inv-cache lookup. */
 for(unsigned i=0;i<256;i++){
  static TranslationBlock tb;tb=(TranslationBlock){.pc=0x1000+32*i,.physical=0x2000+32*i,.cflags=CF_USE_ICOUNT|((i&1)?CF_PCREL:0),.flags=i,.cs_base=i&2};
  static CPUState cpu;cpu=(CPUState){.cc=&cc,.pc=tb.pc+4};setup(&tb);
  recompile(&cpu);
  assert(tb_io_insn_known(cpu.pc));assert(restored==1);
  assert(!tb_ctx.htable.slot&&!tb_ctx.inv_htable.slot);
  assert(tb.cflags&CF_INVALID);
  assert(page_removed==1&&!pages_locked&&cache_removed==1&&jumps_removed[0]==1&&jumps_removed[1]==1&&unlinked==1);
  assert(cpu.cflags_next_tb==(CF_USE_ICOUNT|CF_MEMI_ONLY|CF_NOIRQ|1));
 }
 /* Ordinary code invalidation remains eligible for byte-hash recycling. */
 for(unsigned i=0;i<256;i++){
  TranslationBlock tb={.pc=0x4000+32*i,.physical=0x8000+32*i,.cflags=(i&1)?CF_PCREL:0,.flags=i,.cs_base=i&4};setup(&tb);
  uint32_t h=tb_ctx.htable.hash;tb_phys_invalidate(&tb,-1);
  assert(!tb_ctx.htable.slot&&tb_ctx.inv_htable.slot==&tb&&tb_ctx.inv_htable.hash==h);
  assert(unlinked==1&&page_removed==1&&!pages_locked);
  /* Invalidating again cannot reinsert or unlink an already removed TB. */
  tb_phys_invalidate(&tb,-1);assert(unlinked==1);
 }
 /* One-shot code without a physical page has no page lock/list to remove. */
 TranslationBlock tb={.physical=(uint64_t)-1,.cflags=CF_USE_ICOUNT};setup(&tb);
 tb_phys_invalidate_recompile(&tb);assert(!page_removed&&!pages_locked&&!tb_ctx.inv_htable.slot&&unlinked==1);
 puts("PASS: learned I/O blocks cannot resurrect; ordinary blocks retain reuse, hash identity, page/jump cleanup and duplicate-invalidation behavior (513 cases)");
}
