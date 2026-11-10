/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define unlikely(x) (x)
#define CODE_GEN_HTABLE_SIZE 32768
typedef struct { uint64_t page1, generation; unsigned id; } TranslationBlock;
typedef struct { unsigned stores; } CPUJumpCache;
typedef struct { uint32_t cflags_next_tb; CPUJumpCache *tb_jmp_cache; } CPUState;
typedef struct { uint64_t pc; uint32_t flags,cflags; } TCGTBCPUState;
static struct {uint64_t flush_generation; unsigned htable,inv_htable;} tb_ctx;
static TranslationBlock blocks[64];
static CPUState *current_cpu;
static unsigned step,total,period,flushes,patches,locks,cleanups;
static bool lookup_hit,two_page,reuse,breakpoint;
#define CPU_FOREACH(c) for ((c)=current_cpu; (c); (c)=NULL)
static void trace_tb_flush(void) {}
static bool tcg_enabled(void) {return true;}
static bool runstate_is_running(void) {return true;}
static void tcg_flush_jmp_cache(CPUState *c) {cleanups++;}
static void qht_reset_size(unsigned *table,unsigned size) {assert(size==32768);cleanups++;}
static void tb_remove_all(void) {cleanups++;}
static void tcg_region_reset_all(void) {cleanups++;}
static void qemu_plugin_flush_cb(void) {flushes++;}
/* FLUSH */
static bool cpu_handle_exception(CPUState *c,int *ret) {*ret=7;return step>=total;}
static bool cpu_handle_interrupt(CPUState *c,TranslationBlock **last) {return step>=total;}
static TCGTBCPUState xemu_get_tb_cpu_state(CPUState *c) {return (TCGTBCPUState){.pc=step*16,.flags=3};}
static uint32_t curr_cflags(CPUState *c) {return 8;}
static bool check_for_breakpoints(CPUState *c,uint64_t pc,uint32_t *flags) {
    if (breakpoint && step==2) {step=total;return true;} return false;
}
static TranslationBlock *next_block(void) {
    TranslationBlock *t=&blocks[reuse?0:step];
    *t=(TranslationBlock){.page1=two_page?0x2000:UINT64_MAX,.generation=tb_ctx.flush_generation,.id=step};
    return t;
}
static TranslationBlock *tb_lookup(CPUState *c,TCGTBCPUState s) {return lookup_hit?next_block():NULL;}
static void mmap_lock(void) {assert(!locks);locks++;}
static void mmap_unlock(void) {assert(locks==1);locks--;}
static TranslationBlock *tb_gen_code(CPUState *c,TCGTBCPUState s) {
    assert(locks==1);
    if (period && step && step%period==0) tb_flush__exclusive_or_serial();
    return next_block();
}
static void tb_jmp_cache_store(CPUJumpCache *jc,uint64_t pc,unsigned flags,TranslationBlock *t) {jc->stores++;}
static uint64_t tb_page_addr1(TranslationBlock *t) {return t->page1;}
static void tb_add_jump(TranslationBlock *from,int slot,TranslationBlock *to) {
    /* Reusing the same address makes pointer and block flag checks unable
     * to identify the old block. Only the flush generation distinguishes it. */
    assert(!(period && step%period==0 && !lookup_hit));
    assert(from->generation==tb_ctx.flush_generation);
    assert(!two_page && slot==(int)((step-1)&1)); patches++;
}
static void cpu_loop_exec_tb(CPUState *c,TranslationBlock *t,uint64_t pc,TranslationBlock **last,int *exit) {
    assert(t->generation==tb_ctx.flush_generation && !locks);
    *last=t;*exit=step&1;step++;
}
/* LOOP */
int main(void) {
    unsigned cases=0;
    for (unsigned hit=0;hit<2;hit++) for(unsigned span=0;span<2;span++)
    for(unsigned alias=0;alias<2;alias++) for(unsigned bp=0;bp<2;bp++)
    for(unsigned stride=0;stride<9;stride++) for(unsigned initial=0;initial<3;initial++) {
        CPUJumpCache cache={0}; CPUState cpu={.cflags_next_tb=UINT32_MAX,.tb_jmp_cache=&cache};
        current_cpu=&cpu;step=0;total=32;period=stride;flushes=patches=locks=cleanups=0;
        lookup_hit=hit;two_page=span;reuse=alias;breakpoint=bp;
        tb_ctx.flush_generation=initial==0?0:initial==1?UINT64_MAX:UINT64_MAX-1;
        uint64_t before=tb_ctx.flush_generation;
        assert(cpu_exec_loop(&cpu)==7 && !locks);
        unsigned executed=bp?2:total;
        unsigned expected_flush=hit||!stride?0:(executed-1)/stride;
        unsigned expected_patches=span?0:executed-1-expected_flush;
        assert(flushes==expected_flush && cleanups==5*flushes);
        assert(tb_ctx.flush_generation==before+flushes);
        assert(patches==expected_patches && cache.stores==(hit?0:executed));
        cases++;
    }
    printf("PASS: %u production execution-loop/flush cases; address reuse, hits, spanning TBs, breakpoints and generation wrap\n",cases);
}
