/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define HF_INHIBIT_IRQ_MASK 1u
#define HF_RF_MASK 2u
#define HF_TF_MASK 4u
#define HF_CS64_MASK 8u
#define RF_MASK 0x10000u
#define TB_CF_ANY 7
static uint32_t tb_cf_flags(int c) { return ((uint32_t)(c&3)<<30)|((uint32_t)(c>>2)<<2); }
#define CF_NO_GOTO_PTR 1u
#define CF_NO_GOTO_TB 2u
#define TARGET_PAGE_MASK (~4095ULL)
#define TB_EXIT_IDXMAX 1
#define INDEX_op_goto_tb 2
#define tcg_debug_assert assert
#define CC_OP_DYNAMIC (-1)
typedef uint64_t vaddr;
#define INDEX_op_goto_ptr 1
#define TCG_TYPE_PTR 1
enum { DISAS_EOB_ONLY, DISAS_EOB_NEXT, DISAS_EOB_INHIBIT_IRQ,
       DISAS_EOB_RECHECK_TF, DISAS_JUMP, DISAS_JUMP_FAR, DISAS_EOB_STI, DISAS_NORETURN };
typedef uint64_t TCGv_i64;
typedef int TCGv_ptr;
typedef struct { uint32_t cflags; } FakeTB;
typedef struct { FakeTB *tb; uint64_t pc_first; int is_jmp; } DisasContextBase;
typedef struct { uint32_t flags; uint64_t cs_base,pc; bool jmp_opt;
 int cc_op,cf_entry_class; bool cc_op_entry,cf_entry_used; DisasContextBase base; } DisasContext;
static FakeTB tb;
static struct { FakeTB *gen_tb; } context={&tb}, *tcg_ctx=&context;
static unsigned direct;
static uint32_t last_fast_flags;
static uint32_t tb_cflags(FakeTB *p) { return p->cflags; }
static bool translator_is_same_page(DisasContextBase *db,vaddr pc) { return ((db->pc_first^pc)&TARGET_PAGE_MASK)==0; }
static int x86_cc_op_cf_class(int op) { return op%7; }
static bool xemu_cpu_boost, known_flags;
static uint32_t hflags, eflags;
static unsigned updates, resets, sets, exits, gotos, lookups, fast, steps, rechecks;
static const int tcg_env=0;
static const uint64_t cpu_eip=0x80010000;
static void gen_update_cc_op(DisasContext *s) { (void)s;updates++; }
static void gen_reset_hflag(DisasContext *s,uint32_t f) { (void)s;hflags&=~f;resets++; }
static void gen_set_hflag(DisasContext *s,uint32_t f) { (void)s;hflags|=f;sets++; }
static void gen_reset_eflags(DisasContext *s,uint32_t f) { (void)s;eflags&=~f; }
static void gen_helper_rechecking_single_step(int env) { (void)env;rechecks++; }
static void gen_helper_single_step(int env) { (void)env;steps++; }
/* FLAGS */
static TCGv_i64 tcg_temp_new_i64(void) { return 0; }
static void tcg_gen_extu_tl_i64(TCGv_i64 a,uint64_t b) { (void)a;(void)b; }
static void tcg_gen_addi_i64(TCGv_i64 a,TCGv_i64 b,uint64_t c) { (void)a;(void)b;(void)c; }
static void tcg_gen_ext32u_i64(TCGv_i64 a,TCGv_i64 b) { (void)a;(void)b; }
void tcg_gen_lookup_and_goto_ptr(void);
static void tcg_gen_lookup_and_goto_ptr_fast(TCGv_i64 pc,uint64_t base,uint32_t flags) { (void)pc;(void)base;last_fast_flags=flags;fast++;if(tb.cflags&CF_NO_GOTO_PTR) tcg_gen_lookup_and_goto_ptr(); }
static void tcg_gen_exit_tb(void *a,int b) { (void)a;(void)b;exits++; }
static void plugin_gen_disable_mem_helpers(void) { }
static TCGv_ptr tcg_temp_ebb_new_ptr(void) { return 0; }
static void gen_helper_lookup_tb_ptr(TCGv_ptr p,int env) { (void)p;(void)env;lookups++; }
static int tcgv_ptr_arg(TCGv_ptr p) { return p; }
static void tcg_gen_op1i(int op,int type,int arg) { (void)arg;if(op==INDEX_op_goto_ptr) { assert(type==TCG_TYPE_PTR);gotos++; } else { assert(op==INDEX_op_goto_tb && type==0);direct++; } }
static void tcg_temp_free_ptr(TCGv_ptr p) { (void)p; }
/* LOOKUP */
/* USE_GOTO_TB */
/* GOTO_TB */
/* EOB */
int main(void) {
 unsigned cases=0;
 for(unsigned flags=0;flags<16;flags++) for(unsigned boost=0;boost<2;boost++)
 for(unsigned mode=0;mode<DISAS_NORETURN;mode++) for(unsigned known=0;known<3;known++)
 for(unsigned debug=0;debug<2;debug++) for(unsigned same=0;same<2;same++) {
  DisasContext s={.flags=flags,.cs_base=0x1000,.pc=same?0x80010001:0x80011000,.jmp_opt=!debug,.cc_op=known==2?2:CC_OP_DYNAMIC,.cf_entry_class=known==1?2:TB_CF_ANY,.cc_op_entry=known!=2,.base={.tb=&tb,.pc_first=0x80010000}};xemu_cpu_boost=boost;known_flags=known!=0;
  tb.cflags=debug?(CF_NO_GOTO_PTR|CF_NO_GOTO_TB):0;hflags=flags;eflags=RF_MASK;
  updates=resets=sets=exits=gotos=lookups=fast=steps=rechecks=direct=0;last_fast_flags=0;
  assert(translator_use_goto_tb(&s.base,s.pc)==(!debug && same));
  gen_eob(&s,mode);
  assert(updates==1 && s.base.is_jmp==DISAS_NORETURN);
  bool inhibited=(flags&HF_INHIBIT_IRQ_MASK)!=0;
  assert(resets==(unsigned)inhibited);
  assert(sets==(unsigned)(!inhibited && (mode==DISAS_EOB_INHIBIT_IRQ || mode==DISAS_EOB_STI)));
  if(inhibited) assert(!(hflags&HF_INHIBIT_IRQ_MASK));
  else if(mode==DISAS_EOB_INHIBIT_IRQ || mode==DISAS_EOB_STI) assert(hflags&HF_INHIBIT_IRQ_MASK);
  if(flags&HF_RF_MASK) assert(!(eflags&RF_MASK));
  if(mode==DISAS_EOB_RECHECK_TF) { assert(rechecks==1 && exits==1 && !gotos && !steps); }
  else if(flags&HF_TF_MASK) { assert(steps==1 && !exits && !gotos && !lookups && !fast); }
  else {
   bool shadow=boost && mode==DISAS_EOB_STI && !inhibited && !(flags&HF_RF_MASK);
   bool jump=(mode==DISAS_JUMP || mode==DISAS_JUMP_FAR) && !inhibited;
   bool use_fast=shadow || (jump && boost && mode!=DISAS_JUMP_FAR && !(flags&HF_RF_MASK) && known_flags);
   assert(!direct);
   if(use_fast) { assert(fast==1 && !lookups && !gotos);assert(exits==(unsigned)debug);if(shadow) assert(last_fast_flags==(flags|HF_INHIBIT_IRQ_MASK|tb_cf_flags(TB_CF_ANY))); }
   else if(shadow || jump) {
    if(debug) assert(exits==1 && !lookups && !gotos);
    else assert(lookups==1 && gotos==1 && !exits);
   } else assert(exits==1 && !lookups && !gotos && !fast);
  }
  cases++;
 }
 printf("interrupt-shadow exit generation: %u mode/flags/boost/debug/page/carry cases passed\n",cases);
}
