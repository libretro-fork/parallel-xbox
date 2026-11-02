/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(c) do { if (!(c)) {fprintf(stderr,"fixed x86: %u: %s\n",__LINE__,#c);_Exit(1);} } while(0)
#define g_assert assert
#define XBOX 1
#define CC_C 1u
#define CC_P 4u
#define CC_A 16u
#define CC_Z 64u
#define CC_S 128u
#define DF_MASK 1024u
#define CC_O 2048u
#define CC_MASK (CC_C|CC_P|CC_A|CC_Z|CC_S|CC_O)
#define CC_OP_EFLAGS 1
#define EXCP_HALTED 0x10003
typedef struct {uint32_t eflags,cc_src,cc_src2,cc_dst,cc_op;int32_t df;} CPUX86State;
/* No CPUClass or generic enter/exit callback slots exist in this fixture. */
typedef struct {CPUX86State env;bool halted;} CPUState;
#define cpu_env(cpu) (&(cpu)->env)
#define CC_SRC env->cc_src
#define CC_OP env->cc_op
static bool tcg_enabled(void){return true;}
static uint32_t cpu_cc_compute_all(CPUX86State *env){return (env->cc_op==CC_OP_EFLAGS?env->cc_src:(env->cc_src^env->cc_dst^env->cc_src2))&CC_MASK;}
static CPUState *current_cpu;
#define SET_current_cpu(cpu) (current_cpu=(cpu))
#define RCU_READ_LOCK_GUARD() ((void)0)
static bool bql_locked(void){return true;}
static bool cpu_handle_halt(CPUState *cpu){return cpu->halted;}
static uint32_t operand_a,operand_b,operation;
static CPUX86State expected_entry;
static void program(CPUX86State *env){env->cc_src=operand_a;env->cc_src2=operand_b;env->cc_dst=operand_a+operand_b;env->cc_op=operation;env->eflags^=0x10000;}
static int cpu_exec_setjmp(CPUState *cpu){assert(!memcmp(&expected_entry,&cpu->env,sizeof(expected_entry)));program(&cpu->env);return 71;}
/* PRODUCTION */
/* Frozen pre-change protocol: compare the full state, not just flags. */
static void legacy_enter(CPUX86State *env){env->cc_src=env->eflags&CC_MASK;env->df=(env->eflags&DF_MASK)?-1:1;env->cc_op=CC_OP_EFLAGS;env->eflags&=~(CC_MASK|DF_MASK);}
static void legacy_exit(CPUX86State *env){env->eflags|=cpu_cc_compute_all(env)|(env->df<0?DF_MASK:0);}
static uint32_t seed=0x1234abcd;
static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){unsigned cases=0;
 for(unsigned i=0;i<65536;i++)for(unsigned op=1;op<9;op++){
  CPUState cpu={.env={random32(),random32(),random32(),random32(),99,77}};
  CPUX86State reference=cpu.env;operand_a=random32();operand_b=random32();operation=op;
  legacy_enter(&reference);expected_entry=reference;program(&reference);legacy_exit(&reference);
  assert(cpu_exec(&cpu)==71&&current_cpu==&cpu&&!memcmp(&reference,&cpu.env,sizeof(reference)));
  assert(cpu.env.eflags==cpu_compute_eflags(&cpu.env));cases++;
 }
 CPUState halted={.env={0xabcdef01,7,8,9,10,-1},.halted=true};CPUX86State before=halted.env;
 assert(cpu_exec(&halted)==EXCP_HALTED&&current_cpu==&halted&&!memcmp(&before,&halted.env,sizeof(before)));
 printf("Fixed x86 entry: %u complete boundary states match frozen legacy behavior; halted state unchanged; no generic callback interface\n",cases);
}
