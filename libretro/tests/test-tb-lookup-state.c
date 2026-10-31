/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#define unlikely(x) (x)
#define HELPER(x) helper_##x
#define IOPL_MASK (3u<<12)
#define TF_MASK (1u<<8)
#define RF_MASK (1u<<16)
#define VM_MASK (1u<<17)
#define AC_MASK (1u<<18)
#define HF_CS64_MASK (1u<<20)
#define R_CS 1
#define CF_NO_GOTO_TB (1u<<16)
#define CF_NO_GOTO_PTR (1u<<17)
#define CF_SINGLE_STEP (1u<<18)
#define CPU_LOG_TB_NOCHAIN 1
#define CPU_LOG_TB_CPU 2
#define CPU_LOG_EXEC 4
typedef uint64_t vaddr;
typedef struct {vaddr pc;uint32_t flags,cs_base,cflags;} TCGTBCPUState;
typedef struct {uint32_t hflags,eflags,cc_op;uint64_t eip;struct {uint32_t base;} segs[2];} CPUX86State;
typedef struct CPUState CPUState;
typedef struct {TCGTBCPUState (*get_tb_cpu_state)(CPUState *);} Ops;
typedef struct {Ops *tcg_ops;} Class;
struct CPUState {struct {bool can_do_io;} neg;uint32_t tcg_cflags;bool singlestep_enabled;Class *cc;CPUX86State env;};
typedef CPUState CPUArchState;
typedef struct {struct {const void *ptr;} tc;} TranslationBlock;
static bool xemu_cpu_boost;
static int one_insn_per_tb,loglevel;
static unsigned indirect_calls,lookups,logs,breakpoint_checks;
static bool hit,breakpoint,rate;
static TCGTBCPUState looked_up;
static TranslationBlock block;
static const void *tcg_code_gen_epilogue;
static struct {unsigned lookup_miss;} xemu_tbrate;
static jmp_buf exit_jump;
static CPUState *env_cpu(CPUArchState *env) {return env;}
static CPUX86State *cpu_env(CPUState *cpu) {return &cpu->env;}
static uint32_t x86_cc_op_cf_class(uint32_t op) {return op&3;}
static uint32_t tb_cf_flags(uint32_t klass) {return klass<<28;}
static int retro_atomic_load_relaxed_int(int *p) {return *p;}
static bool qemu_loglevel_mask(int mask) {return (loglevel&mask)!=0;}
static bool check_for_breakpoints(CPUState *cpu,vaddr pc,uint32_t *cflags) {
    assert(cpu->neg.can_do_io);breakpoint_checks++;return breakpoint;
}
static void cpu_loop_exit(CPUState *cpu) {longjmp(exit_jump,1);}
static TranslationBlock *tb_lookup(CPUState *cpu,TCGTBCPUState state) {
    assert(cpu->neg.can_do_io);lookups++;looked_up=state;return hit?&block:NULL;
}
static bool xemu_tbrate_enabled(void) {return rate;}
static void log_cpu_exec(vaddr pc,CPUState *cpu,TranslationBlock *tb) {assert(tb==&block);logs++;}
/* PRODUCTION */
static TCGTBCPUState indirect_hook(CPUState *cpu) {indirect_calls++;return x86_get_tb_cpu_state(cpu);}
static uint32_t random_word(void) {static uint32_t v=0x12abcd;v^=v<<13;v^=v>>17;v^=v<<5;return v;}
static void __attribute__((noinline)) check_breakpoint(CPUState *cpu) {
    breakpoint=true;lookups=0;
    if(!setjmp(exit_jump)) {helper_lookup_tb_ptr(cpu);assert(!"breakpoint did not exit");}
    assert(!lookups);
}
int main(void) {
    Ops ops={indirect_hook};Class klass={&ops};CPUState cpu={.cc=&klass};unsigned cases=0;
    static int target,epilogue;block.tc.ptr=&target;tcg_code_gen_epilogue=&epilogue;
    for(unsigned sample=0;sample<1000;sample++) for(unsigned mode=0;mode<128;mode++) {
        cpu.env.hflags=random_word();cpu.env.eflags=random_word();cpu.env.eip=((uint64_t)random_word()<<32)|random_word();
        cpu.env.segs[R_CS].base=random_word();cpu.env.cc_op=random_word();cpu.tcg_cflags=random_word();
        xemu_cpu_boost=mode&1;cpu.singlestep_enabled=mode&2;one_insn_per_tb=!!(mode&4);
        loglevel=((mode&8)?CPU_LOG_TB_NOCHAIN:0)|((mode&16)?CPU_LOG_EXEC:0);
        hit=mode&32;rate=mode&64;breakpoint=false;
        uint32_t expected_flags=cpu.env.hflags|(cpu.env.eflags&0x73100);
        if(xemu_cpu_boost) expected_flags|=(cpu.env.cc_op&3)<<28;
        uint32_t expected_base=(cpu.env.hflags&HF_CS64_MASK)?0:cpu.env.segs[R_CS].base;
        uint64_t expected_pc=(cpu.env.hflags&HF_CS64_MASK)?cpu.env.eip:(uint32_t)(expected_base+cpu.env.eip);
        uint32_t expected_cflags=cpu.tcg_cflags;
        if(cpu.singlestep_enabled) expected_cflags|=CF_NO_GOTO_TB|CF_NO_GOTO_PTR|CF_SINGLE_STEP|1;
        else if(one_insn_per_tb) expected_cflags|=CF_NO_GOTO_TB|1;
        else if(loglevel&CPU_LOG_TB_NOCHAIN) expected_cflags|=CF_NO_GOTO_TB;
        unsigned old_misses=xemu_tbrate.lookup_miss;lookups=logs=breakpoint_checks=indirect_calls=0;cpu.neg.can_do_io=false;
        assert(helper_lookup_tb_ptr(&cpu)==(hit?block.tc.ptr:tcg_code_gen_epilogue));
        assert(looked_up.pc==expected_pc && looked_up.cs_base==expected_base && looked_up.flags==expected_flags && looked_up.cflags==expected_cflags);
        assert(lookups==1 && breakpoint_checks==1 && logs==(unsigned)(hit && (loglevel&CPU_LOG_EXEC)));
        assert(xemu_tbrate.lookup_miss==old_misses+(unsigned)(!hit && rate));
#ifdef XBOX
        assert(!indirect_calls);
#else
        assert(indirect_calls==1);
#endif
        check_breakpoint(&cpu);cases++;
    }
    printf("TB lookup state: %u boost/debug/flags/PC/hit/miss/breakpoint cases passed\n",cases);
}
