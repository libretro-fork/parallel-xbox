/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr,"quiet store: %u: %s\n",__LINE__,#c); _Exit(1); } } while(0)
#define LR_QUIET_INSNS 8192
#define TCG_OPF_BB_END 1
#define TCG_OPF_COND_BRANCH 2
#define TCG_OPF_SIDE_EFFECTS 4
#define TCG_OPF_CALL_CLOBBER 8
#define QTAILQ_NEXT(op, field) ((op)->next)
typedef struct TCGOp { struct TCGOp *next; unsigned opc; bool removed; } TCGOp;
typedef struct { void *lr_quiet_label; bool lr_quiet_store, lr_quiet_enabled; TCGOp *lr_quiet_first,*lr_quiet_last; } TCGContext;
typedef struct { struct { int lr_quiet; } neg; } CPUState;
static const struct { unsigned flags; } tcg_op_defs[]={{0},{1},{2},{4},{8},{15}};
static TCGContext ctx,*tcg_ctx=&ctx;
static int tcg_env, removed, resets;
static int tcg_constant_i32(int v) { return v; }
static void tcg_gen_st_i32(int v,int env,size_t off) { (void)env;(void)off;assert(v==LR_QUIET_INSNS);resets++; }
static void tcg_op_remove(TCGContext *c,TCGOp *op) { assert(c==&ctx);assert(!op->removed);op->removed=true;removed++; }
/* PRODUCTION */
int main(void) {
 unsigned cases=0;
 for (unsigned length=0;length<=16;length++) {
  for (unsigned hazard=0;hazard<=5;hazard++) {
   for (unsigned pos=0;pos<(length?length:1);pos++) {
    TCGOp ops[20]={0};
    for(unsigned i=0;i+1<4+length;i++)ops[i].next=&ops[i+1];
    if(length)ops[4+pos].opc=hazard;
    ctx=(TCGContext){.lr_quiet_enabled=true,.lr_quiet_label=&ctx,.lr_quiet_first=&ops[0],.lr_quiet_last=&ops[3]};
    removed=resets=0;lr_quiet_store_emitted();
    bool safe=!length || !hazard;
    assert(removed==(safe?4:0));assert((ctx.lr_quiet_label==NULL)==safe);
    assert(ctx.lr_quiet_store && resets==1);
    for(unsigned i=4;i<4+length;i++)assert(!ops[i].removed);
    lr_quiet_store_emitted();assert(resets==1);cases++;
   }
  }
 }
 ctx=(TCGContext){.lr_quiet_enabled=true};removed=resets=0;
 lr_quiet_store_emitted();assert(!removed && resets==1);
 lr_quiet_store_emitted();assert(resets==1);
 ctx=(TCGContext){0};removed=resets=0;lr_quiet_store_emitted();assert(!removed && !resets);
 printf("quiet store prefix: %u straight-line/branch/helper/access cases and disabled path passed\n",cases);
}
