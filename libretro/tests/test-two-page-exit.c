/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint64_t target_ulong;
typedef uint64_t vaddr;
typedef int MemOp;
typedef uint64_t *TCGv_i64;
enum { MO_16, MO_32, MO_64, DISAS_NORETURN, DISAS_JUMP, DISAS_EOB_ONLY };
#define CF_PCREL 1u
#define CF_NO_GOTO_TB 2u
#define TARGET_PAGE_MASK (~4095ULL)
#define TARGET_PAGE_SIZE 4096
#define X86_MAX_INSN_LENGTH 15
#define CODE64(s) ((s)->code64)
#define CODE32(s) ((s)->code32)
typedef struct { unsigned cflags; } TB;
typedef struct { TB *tb; uint64_t pc_first; int is_jmp; } DisasContextBase;
typedef struct { DisasContextBase base; bool jmp_opt,cc_op_dirty,code64,code32;
 uint64_t pc,pc_save,cs_base; } DisasContext;
static uint64_t ip,temporary;
static TCGv_i64 cpu_eip=&ip;
static bool xemu_cpu_boost,known;
static unsigned direct,probe,exits,eob,slot;
static uint64_t probe_pc,probe_cs;
static uint32_t probe_flags;
static unsigned tb_cflags(TB *tb) { return tb->cflags; }
static bool translator_is_same_page(DisasContextBase *db,uint64_t pc) { return !((db->pc_first^pc)&TARGET_PAGE_MASK); }
/* PAGE_GUARD */
static bool gen_exit_tb_flags(DisasContext *s,uint32_t *f) { (void)s;*f=0x1234;return known; }
static void tcg_gen_addi_tl(TCGv_i64 a,TCGv_i64 b,uint64_t c) { *a=*b+c; }
static void tcg_gen_andi_tl(TCGv_i64 a,TCGv_i64 b,uint64_t c) { *a=*b&c; }
static void tcg_gen_movi_tl(TCGv_i64 a,uint64_t b) { *a=b; }
static void tcg_gen_extu_tl_i64(TCGv_i64 a,TCGv_i64 b) { *a=*b; }
static void tcg_gen_addi_i64(TCGv_i64 a,TCGv_i64 b,uint64_t c) { *a=*b+c; }
static void tcg_gen_ext32u_i64(TCGv_i64 a,TCGv_i64 b) { *a=(uint32_t)*b; }
static TCGv_i64 tcg_temp_new_i64(void) { temporary=0;return &temporary; }
static void tcg_gen_goto_tb(int n) { direct++;slot=n; }
static void tcg_gen_exit_tb(TB *tb,int n) { (void)tb;exits++;assert(slot==(unsigned)n); }
static void tcg_gen_lookup_and_goto_ptr_2page(TCGv_i64 pc,uint64_t cs,uint32_t flags,unsigned n) { probe++;probe_pc=*pc;probe_cs=cs;probe_flags=flags;assert(slot==n); }
static void gen_eob(DisasContext *s,int mode) { (void)s;assert(mode==DISAS_JUMP || mode==DISAS_EOB_ONLY);eob++; }
/* PRODUCTION */
int main(void) {
 unsigned cases=0;const int diffs[]={-4096,-16,-9,-8,0,1,4,32};
 const uint64_t bases[]={0,0x1000,0x80000000};
 for(unsigned boost=0;boost<2;boost++) for(unsigned pcrel=0;pcrel<2;pcrel++)
 for(unsigned k=0;k<2;k++) for(unsigned opt=0;opt<2;opt++)
 for(unsigned mode=0;mode<4;mode++) for(unsigned no=0;no<2;no++)
 for(unsigned bi=0;bi<3;bi++) for(unsigned di=0;di<8;di++)
 for(unsigned index=0;index<2;index++) {
  if(mode==2 && bi) continue; /* Long mode ignores the CS base. */
  TB tb={.cflags=(pcrel?CF_PCREL:0)|(no?CF_NO_GOTO_TB:0)};
  uint64_t current=bases[bi]+(mode==0?0xfffa:0x100ffa);
  uint64_t first=current&TARGET_PAGE_MASK;
  DisasContext s={.base={.tb=&tb,.pc_first=first},.pc=current,.pc_save=first,.cs_base=bases[bi],.jmp_opt=opt,.code32=mode==1 || mode==3,.code64=mode==2};
  uint64_t mask=(mode==0 || mode==3)?0xffff:mode==2?UINT64_MAX:0xffffffff;
  uint64_t eip=(s.pc+diffs[di]-s.cs_base)&mask;
  uint64_t pc=mode==2?s.pc+diffs[di]:(uint32_t)(eip+s.cs_base);
  bool link=opt && !no && k && !(pcrel && mode==3) && translator_is_same_page(&s.base,pc);
  /* PC-relative 16-bit displacement in 32-bit code is a separate guarded
   * path in production and must never emit the cache probe. */
  ip=s.pc_save-s.cs_base;xemu_cpu_boost=boost;known=k;
  direct=probe=exits=eob=0;probe_pc=probe_cs=0;probe_flags=0;
  gen_jmp_rel(&s,(mode==0 || mode==3)?MO_16:mode==1?MO_32:MO_64,diffs[di],index);
  assert(direct==(unsigned)link);
  if(link) {
   assert(ip==eip && !eob && s.base.is_jmp==DISAS_NORETURN);
   if(boost && (pc & 4095)>4081) { assert(probe==1 && !exits);assert(probe_pc==pc && probe_cs==s.cs_base && probe_flags==0x1234); }
   else assert(!probe && exits==1);
  } else assert(!probe && !exits && eob==1);
  cases++;
 }
 printf("two-page exit generation: %u mode/PC-relative/page/segment/class/slot cases passed\n",cases);
}
