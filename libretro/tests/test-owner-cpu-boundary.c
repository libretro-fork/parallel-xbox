/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr,"owner CPU: %u: %s\n",__LINE__,#c); _Exit(1); } } while(0)
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#define g_assert assert
#define unlikely(c) (c)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define EXCP_INTERRUPT 0x10000
#define EXCP_DEBUG 0x10002
#define EXCP_HALTED 0x10003
#define EXCP_HLT EXCP_HALTED
#define CF_NOIRQ 0x2000u
#define CF_USE_ICOUNT 0x10000u
#define CPU_INTERRUPT_DEBUG 1
#define CPU_INTERRUPT_HALT 2
#define CPU_INTERRUPT_RESET 4
#define CPU_INTERRUPT_HARD 8
#define CPU_INTERRUPT_EXITTB 16
#define CPU_INTERRUPT_SSTEP_MASK CPU_INTERRUPT_HARD
#define SSTEP_NOIRQ 2
#define LR_QUIET_INSNS 12345
#define retro_atomic_store_release_int(p,v) (*(p)=(v))
#define retro_atomic_store_release_u16(p,v) (*(p)=(v))
#define retro_atomic_load_relaxed_u16(p) (*(p))
#define retro_atomic_load_relaxed_int(p) (*(p))
#define retro_atomic_load_acquire_int(p) (*(p))
#define retro_atomic_thread_fence_seq_cst() ((void)0)
typedef struct CPUState CPUState;
typedef struct TranslationBlock {int unused;} TranslationBlock;
typedef struct {void (*do_interrupt)(CPUState *);void (*cpu_exec_reset)(CPUState *);bool (*cpu_exec_interrupt)(CPUState *,int);} TCGCPUOps;
typedef struct {const TCGCPUOps *tcg_ops;} CPUClass;
struct CPUState {
 bool stop,stopped,runnable;int thread_kicked,work,exception_index,singlestep_enabled;
 int interrupt_request,exit_request,cflags_next_tb,icount_extra,lr_poll_runs;
 bool halted;CPUClass *cc;
 struct {struct {struct {uint16_t low,high;} u16;} icount_decr;int lr_quiet;} neg;
};
static CPUState *current_cpu;
#define SET_current_cpu(cpu) (current_cpu=(cpu))
static struct {TranslationBlock *gen_tb;} context,*tcg_ctx=&context;
static bool held=true,pages_locked,use_icount=true;
static unsigned acquisitions,releases,pages_released,executions,processed,debugged,work_done,interrupts,reset;
static int mode,run_result,irq_seen;static bool irq_processed=true;
static int64_t last_budget;
static jmp_buf fault;
static bool bql_locked(void){return held;}
static void bql_lock_impl(const char *file,int line){(void)file;(void)line;assert(!held);held=true;acquisitions++;}
static void bql_lock(void){bql_lock_impl(__FILE__,__LINE__);}
static void bql_unlock(void){assert(held);held=false;releases++;}
typedef struct BQLLockAuto BQLLockAuto;
/* GUARDS */
static void auto_cleanup(BQLLockAuto **lock){if(*lock)bql_auto_unlock(*lock);}
#define BQL_LOCK_GUARD() BQLLockAuto *guard __attribute__((cleanup(auto_cleanup)))=bql_auto_lock(__FILE__,__LINE__)
static void tb_unlock_pages(TranslationBlock *tb){assert(tb==context.gen_tb && pages_locked);pages_locked=false;pages_released++;}
static void assert_no_pages_locked(void){assert(!pages_locked);}
static bool cpu_can_run(CPUState *cpu){return cpu->runnable&&!cpu->stopped;}
static bool cpu_thread_is_idle(CPUState *cpu){(void)cpu;return false;}
static bool cpu_work_list_empty(CPUState *cpu){return !cpu->work;}
static void process_queued_cpu_work(CPUState *cpu){assert(held&&current_cpu==cpu);cpu->work=0;cpu->runnable=true;work_done++;}
static bool icount_enabled(void){return use_icount;}
static int64_t icount_round(int64_t ns){return ns/2;}
static void icount_prepare_for_run(CPUState *cpu,int64_t budget){assert(cpu==current_cpu&&held);last_budget=budget;}
static void icount_process_data(CPUState *cpu){assert(cpu==current_cpu&&held);processed++;}
static void cpu_handle_guest_debug(CPUState *cpu){(void)cpu;assert(held&&!current_cpu);debugged++;}
static void cpu_handle_debug_exception(CPUState *cpu){(void)cpu;assert(held);debugged++;}
static bool cpu_test_interrupt(CPUState *cpu,int mask){return cpu->interrupt_request&mask;}
static void cpu_reset_interrupt(CPUState *cpu,int mask){cpu->interrupt_request&=~mask;}
static void handle_exception(CPUState *cpu){(void)cpu;assert(held);interrupts++;if(mode==4)longjmp(fault,1);}
static void handle_reset(CPUState *cpu){(void)cpu;assert(held);reset++;}
static bool handle_irq(CPUState *cpu,int mask){(void)cpu;assert(held);irq_seen=mask;if(mode==4)longjmp(fault,1);return irq_processed;}
static void cpu_exec_longjmp_cleanup(CPUState *cpu);
static void mmio_transaction(void){BQL_LOCK_GUARD();assert(held);if(mode==1)longjmp(fault,1);}
static int tcg_cpu_exec(CPUState *cpu){
 assert(held&&current_cpu==cpu);executions++;
 if(mode==1){TranslationBlock block;context.gen_tb=&block;pages_locked=true;
  if(!setjmp(fault)){mmio_transaction();assert(0);}cpu_exec_longjmp_cleanup(cpu);
  assert(held&&!context.gen_tb&&!pages_locked);
 }else{
  for(unsigned i=0;i<5000;i++)mmio_transaction();
  if(mode==2){bql_unlock();assert(!held);bql_lock();}
 }
 return run_result;
}
/* PRODUCTION */
typedef struct {uint32_t hflags2;} CPUX86State;
#define HF2_IGNNE_MASK 1
static int ferr_irq=1;
static unsigned ferr_count;
static void qemu_irq_raise(int irq){assert(held&&irq==1);ferr_count++;}
/* FERR */
int main(void){
 TCGCPUOps ops={handle_exception,handle_reset,handle_irq};CPUClass cc={&ops};static CPUState cpu;cpu.runnable=true;cpu.cc=&cc;
 volatile unsigned cases=0;
 for(unsigned clock=0;clock<2;clock++)for(unsigned run=0;run<100;run++){
  use_icount=clock;cpu.thread_kicked=1;mode=0;run_result=0;
  unsigned locks=acquisitions,unlocks=releases,before=processed;
  assert(lr_vcpu_run_slice(&cpu,100)==0 && held&&!current_cpu&&!cpu.thread_kicked);
  assert(acquisitions==locks&&releases==unlocks&&processed==before+clock);
  if(clock)assert(last_budget==50);
  mode=1;assert(!lr_vcpu_run_slice(&cpu,1)&&held&&!current_cpu);
  if(clock)assert(last_budget==1);
  assert(acquisitions==locks&&releases==unlocks);
  mode=2;assert(!lr_vcpu_run_slice(&cpu,2)&&held&&!current_cpu);
  assert(acquisitions==locks+1&&releases==unlocks+1);cases+=3;
 }
 unsigned before=executions;cpu.stop=true;assert(lr_vcpu_run_slice(&cpu,10)==EXCP_HALTED&&cpu.stopped&&!cpu.stop&&held&&executions==before);
 cpu.stopped=false;cpu.runnable=false;assert(lr_vcpu_run_slice(&cpu,10)==EXCP_HALTED&&executions==before);
 cpu.work=1;mode=0;assert(!lr_vcpu_run_slice(&cpu,10)&&work_done==1&&held&&!current_cpu);
 run_result=EXCP_DEBUG;before=debugged;assert(lr_vcpu_run_slice(&cpu,10)==EXCP_DEBUG&&debugged==before+1&&held);
 current_cpu=&cpu;
 for(unsigned step=0;step<2;step++){
  cpu.singlestep_enabled=step;cpu.exception_index=14;int ret=-1;before=interrupts;
  assert(cpu_handle_exception(&cpu,&ret)==(bool)step&&interrupts==before+1&&cpu.exception_index==-1&&held);
  if(step)assert(ret==EXCP_DEBUG);
 }
 cpu.singlestep_enabled=0;cpu.exception_index=-1;int ret=-1;assert(!cpu_handle_exception(&cpu,&ret));
 cpu.exception_index=EXCP_DEBUG;assert(cpu_handle_exception(&cpu,&ret)&&ret==EXCP_DEBUG&&cpu.exception_index==-1&&held);
 for(unsigned bit=0;bit<5;bit++){
  TranslationBlock last,*last_tb=&last;cpu.interrupt_request=1<<bit;cpu.exception_index=-1;cpu.cflags_next_tb=-1;cpu.neg.icount_decr.u16.low=1;cpu.exit_request=0;cpu.halted=false;mode=0;
  bool result=cpu_handle_interrupt(&cpu,&last_tb);assert(held);
  if(bit<3)assert(result);
  if(bit==0)assert(cpu.exception_index==EXCP_DEBUG);
  if(bit==1)assert(cpu.halted&&cpu.exception_index==EXCP_HLT);
  if(bit==2)assert(reset==1);
  if(bit==3)assert(!result&&!last_tb&&irq_seen==CPU_INTERRUPT_HARD&&cpu.neg.lr_quiet==LR_QUIET_INSNS&&!cpu.lr_poll_runs);
  if(bit==4)assert(!result&&!last_tb&&!(cpu.interrupt_request&CPU_INTERRUPT_EXITTB));
 }
 cpu.interrupt_request=CPU_INTERRUPT_HARD;cpu.exception_index=14;mode=4;
 if(!setjmp(fault)){cpu_handle_exception(&cpu,&ret);assert(0);}cpu_exec_longjmp_cleanup(&cpu);assert(held);
 TranslationBlock *last_tb=NULL;
 if(!setjmp(fault)){cpu_handle_interrupt(&cpu,&last_tb);assert(0);}cpu_exec_longjmp_cleanup(&cpu);assert(held);
 mode=0;cpu.cflags_next_tb=CF_NOIRQ;cpu.interrupt_request=CPU_INTERRUPT_DEBUG;assert(!cpu_handle_interrupt(&cpu,&last_tb)&&cpu.interrupt_request==CPU_INTERRUPT_DEBUG);
 CPUX86State env={0};unsigned locks=acquisitions,unlocks=releases;
 fpu_check_raise_ferr_irq(&env);assert(ferr_count==1&&acquisitions==locks&&releases==unlocks);
 bql_unlock();locks=acquisitions;unlocks=releases;fpu_check_raise_ferr_irq(&env);
 assert(!held&&ferr_count==2&&acquisitions==locks+1&&releases==unlocks+1);bql_lock();
 printf("Owner CPU boundary: %u slices, guest/IRQ faults, stop/work/debug, conditional FERR guards and explicit GPU-wait releases passed; no MMIO mutex transfers\n",cases);
}
