/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "qemu/queue.h"
#define assert(c) do { if (!(c)) { fprintf(stderr,"watchpoint ownership: %u: %s\n",__LINE__,#c); _Exit(1); } } while(0)
#define g_assert assert
#define MAX(a,b) ((a)>(b)?(a):(b))
#define BP_MEM_READ 1
#define BP_MEM_WRITE 2
#define BP_MEM_ACCESS 3
#define BP_CPU 4
#define BP_STOP_BEFORE_ACCESS 8
#define BP_HIT_SHIFT 8
#define BP_WATCHPOINT_HIT (BP_MEM_ACCESS << BP_HIT_SHIFT)
#define CPU_INTERRUPT_DEBUG 1
#define EXCP_DEBUG 2
#define CF_NOIRQ 0x100

typedef uint64_t vaddr;
typedef uint32_t MemTxAttrs;
typedef struct CPUWatchpoint {
    vaddr vaddr,len,hitaddr;
    int flags;
    MemTxAttrs hitattrs;
    QTAILQ_ENTRY(CPUWatchpoint) entry;
} CPUWatchpoint;
typedef struct CPUState CPUState;
typedef struct {
    vaddr (*adjust_watchpoint_address)(CPUState *,vaddr,vaddr);
    bool (*debug_check_watchpoint)(CPUState *,CPUWatchpoint *);
} Ops;
typedef struct {const Ops *tcg_ops;} CPUClass;
struct CPUState {
    CPUWatchpoint *watchpoint_hit;
    CPUClass *cc;
    QTAILQ_HEAD(,CPUWatchpoint) watchpoints;
    int exception_index,cflags_next_tb,interrupt_request;
};
static bool held;
static unsigned acquisitions,releases,interrupts,restores;
static bool reject;
static jmp_buf unwind;
static bool bql_locked(void){return held;}
static void bql_lock_impl(const char *file,int line){(void)file;(void)line;assert(!held);held=true;acquisitions++;}
static void bql_lock(void){bql_lock_impl(__FILE__,__LINE__);}
static void bql_unlock(void){assert(held);held=false;releases++;}
typedef struct BQLLockAuto BQLLockAuto;
/* GUARDS */
static void auto_cleanup(BQLLockAuto **lock){if(*lock)bql_auto_unlock(*lock);}
#define BQL_LOCK_GUARD() BQLLockAuto *guard __attribute__((cleanup(auto_cleanup)))=bql_auto_lock(__FILE__,__LINE__)
static bool tcg_enabled(void){return true;}
static void cpu_interrupt(CPUState *cpu,int mask){assert(held);assert(mask==CPU_INTERRUPT_DEBUG);cpu->interrupt_request|=mask;interrupts++;}
static void tb_check_watchpoint(CPUState *cpu,uintptr_t ra){assert(cpu->watchpoint_hit);assert(ra==0x1234);restores++;}
static unsigned curr_cflags(CPUState *cpu){(void)cpu;return 0x400;}
static void cpu_loop_exit(CPUState *cpu){(void)cpu;longjmp(unwind,1);}
static void cpu_loop_exit_noexc(CPUState *cpu){(void)cpu;longjmp(unwind,2);}
static bool check(CPUState *cpu,CPUWatchpoint *wp){(void)cpu;(void)wp;return !reject;}
/* MATCHER */
/* PRODUCTION */
int main(void)
{
    Ops ops={NULL,check};CPUClass cc={&ops};
    volatile unsigned cases=0;
    static CPUState cpu;
    static CPUWatchpoint wp;
    for(volatile unsigned owner=0;owner<2;owner++)for(volatile unsigned access=1;access<=2;access++)for(volatile unsigned stop=0;stop<2;stop++){
        cpu=(CPUState){.cc=&cc,.exception_index=-1};
        wp=(CPUWatchpoint){.vaddr=0x1000,.len=4,.flags=access|BP_CPU|(stop?BP_STOP_BEFORE_ACCESS:0)};
        QTAILQ_INIT(&cpu.watchpoints);QTAILQ_INSERT_TAIL(&cpu.watchpoints,&wp,entry);
        held=owner;reject=false;
        unsigned locks=acquisitions,unlocks=releases,irq=interrupts,saved=restores;
        int reason=setjmp(unwind);
        if(!reason){cpu_check_watchpoint(&cpu,0x1001,2,0x55,access,0x1234);assert(false);}
        assert(reason==(stop?1:2));assert(cpu.watchpoint_hit==&wp);
        assert(wp.hitaddr==0x1001&&wp.hitattrs==0x55&&(wp.flags&BP_WATCHPOINT_HIT));
        assert(restores==saved+1&&held==(bool)owner);
        assert(acquisitions==locks&&releases==unlocks&&interrupts==irq);
        if(stop)assert(cpu.exception_index==EXCP_DEBUG);
        else assert(cpu.cflags_next_tb==(1|CF_NOIRQ|0x400));
        /* Re-entry must publish DEBUG under BQL, retaining an outer owner
         * and balancing its own acquisition when called without one. */
        cpu_check_watchpoint(&cpu,0x2000,1,0x66,access,0x5678);
        assert(cpu.interrupt_request==CPU_INTERRUPT_DEBUG&&interrupts==irq+1);
        assert(acquisitions==locks+!owner&&releases==unlocks+!owner);
        assert(held==(bool)owner&&restores==saved+1);
        assert(wp.hitaddr==0x1001&&wp.hitattrs==0x55);
        cpu.watchpoint_hit=NULL;reject=true;
        cpu_check_watchpoint(&cpu,0x1001,1,0x55,access,0x1234);
        assert(!cpu.watchpoint_hit&&!(wp.flags&BP_WATCHPOINT_HIT));
        reject=false;
        cpu_check_watchpoint(&cpu,0x2000,1,0x55,access,0x1234);
        assert(!cpu.watchpoint_hit&&held==(bool)owner&&restores==saved+1);
        assert(acquisitions==locks+!owner&&releases==unlocks+!owner);
        cases++;
    }
    /* Exercise the explicit locking helper in the oracle as well. */
    held=false;bql_lock();bql_unlock();
    {BQL_LOCK_GUARD();assert(held);}
    assert(!held);
    printf("Watchpoint ownership: %u read/write before/after-access paths, DEBUG re-entry, rejection and BQL balance passed\n",cases);
    return 0;
}
