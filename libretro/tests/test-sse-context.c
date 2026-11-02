/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "fpu/softfloat.h"
#include <setjmp.h>
typedef struct {float_status sse_status;uint32_t xemu_host_mxcsr;unsigned cookie;} CPUX86State;
typedef struct CPUState {CPUX86State env;bool halted;} CPUState;
#include "target/i386/tcg/sse-context.h"
void g_assertion_message_expr(const char *d,const char *f,int l,const char *fn,const char *e) {fprintf(stderr,"Assertion failed %s:%d %s: %s\n",f,l,fn,e);abort();}
static CPUState *current_cpu;
#define SET_current_cpu(cpu) (current_cpu=(cpu))
#define EXCP_HALTED 99
static int xemu_cpu_boost=1;
static unsigned phase,work;
static CPUX86State *cpu_env(CPUState *cpu){return &cpu->env;}
static bool cpu_handle_halt(CPUState *cpu){return cpu->halted;}
#define RCU_READ_LOCK_GUARD() assert(current_cpu==cpu)
static void cpu_exec_enter(CPUState *cpu) {
 assert(phase==0);phase=1;
 assert((__builtin_ia32_stmxcsr()&~0x3fu)==0x1f80);
 assert(cpu->env.xemu_host_mxcsr==0x1f80);
}
static void cpu_exec_exit(CPUState *cpu){assert(phase==2);phase=3;}
static int cpu_exec_setjmp(CPUState *cpu);
/* PRODUCTION */
static int cpu_exec_setjmp(CPUState *cpu) {
 jmp_buf destination;
 if(!setjmp(destination)) {
  assert(phase==1);assert(sse_native_ok(&cpu->env));
  /* Halfway sum: nearest must retain 1.0 even if caller rounded upward. */
  union {uint32_t u;float f;} a={.u=0x3f800000},b={.u=0x33800000};
  __asm__ volatile("addss %1,%0":"+x"(a.f):"x"(b.f):"memory");
  assert(a.u==0x3f800000);
  if(work&1) {
   uint32_t saved=xemu_sse_context_enter(&cpu->env);
   __builtin_ia32_ldmxcsr(0x7f80);
   xemu_sse_context_exit(&cpu->env,saved);
   assert((__builtin_ia32_stmxcsr()&~0x3fu)==0x1f80);
   assert(sse_native_ok(&cpu->env));
  }
  /* Simulate gen_flcr changing rounding, and guest reset clearing env. */
  __builtin_ia32_ldmxcsr(0x5f80);cpu->env.xemu_host_mxcsr=0x5f80;
  if(work&2)memset(&cpu->env,0,sizeof(cpu->env));
  if(work&4)longjmp(destination,1);
 }
 phase=2;return 42;
}
static uint32_t random_word(void) {static uint32_t n=0x123abc;n^=n<<13;n^=n>>17;n^=n<<5;return n;}
int main(void) {
 CPUState cpu={0};unsigned cases=0;
 for(unsigned i=0;i<4096;i++)for(work=0;work<8;work++) {
  /* Include all rounding modes, flags, DAZ/FTZ and unmasked exceptions. */
  uint32_t host=random_word()&0xffffu;
  cpu.env.xemu_host_mxcsr=0x1f80;cpu.env.cookie=0xabcdef;
  __builtin_ia32_ldmxcsr(host);phase=0;cpu.halted=false;
  assert(cpu_exec(&cpu)==42 && phase==3);
  assert(__builtin_ia32_stmxcsr()==host);
  assert(cpu.env.xemu_host_mxcsr==0);
  assert(get_float_exception_flags(&cpu.env.sse_status)==0); /* no blanket host-flag harvest */
  assert(cpu.env.cookie==((work&2)?0u:0xabcdefu));
  /* Halted calls never acquire or modify floating-point context. */
  cpu.halted=true;cpu.env.xemu_host_mxcsr=0x1234;phase=0;
  assert(cpu_exec(&cpu)==EXCP_HALTED && phase==0);
  assert(__builtin_ia32_stmxcsr()==host && cpu.env.xemu_host_mxcsr==0x1234);
  cases+=2;
 }
 __builtin_ia32_ldmxcsr(0x1f80);
 printf("SSE context: %u normal/halt/nesting/reset/longjmp/stale-cache cases passed\n",cases);
}
