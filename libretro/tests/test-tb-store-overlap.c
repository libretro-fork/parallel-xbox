/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#define TARGET_PAGE_MASK (~UINT64_C(4095))
#define CF_COUNT_MASK 255u
#define CF_NOIRQ 256u
#define CF_PCREL 1024u
#define unlikely(x) (x)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define tcg_debug_assert assert
typedef uint64_t tb_page_addr_t;
typedef unsigned PageForEachNext;
typedef struct TranslationBlock { uint64_t page[2]; unsigned size, cflags, id; } TranslationBlock;
typedef struct { struct { struct { bool precise_smc; } *tcg_ops; } *cc; unsigned cflags_next_tb; } CPUState;
typedef struct { unsigned first_tb; } PageDesc;
struct page_collection { unsigned unused; };
static TranslationBlock blocks[3], *running;
static unsigned page_link[3], invalid, restored, unlocked, unprotected;
static unsigned flushes;
static CPUState *only_cpu;
#define CPU_FOREACH(c) for ((c)=only_cpu; (c); (c)=NULL)
static void tcg_flush_jmp_cache(CPUState *c) { flushes++; }
static PageDesc *active_page;
static jmp_buf exit_env;
static TranslationBlock *tcg_tb_lookup(uintptr_t ra) { return running; }
static uint64_t tb_page_addr0(TranslationBlock *t) { return t->page[0]; }
static uint64_t tb_page_addr1(TranslationBlock *t) { return t->page[1]; }
static unsigned tb_cflags(TranslationBlock *t) { return t->cflags; }
static unsigned curr_cflags(CPUState *c) { return 512; }
static void cpu_restore_state_from_tb(CPUState *c, TranslationBlock *t, uintptr_t ra) { restored++; }
static void tb_phys_invalidate__locked(TranslationBlock *t, bool *owed) { invalid |= 1u << t->id; active_page->first_tb--; *owed=true; }
static void tlb_unprotect_code(uint64_t start) { assert(flushes==1); unprotected++; }
static void page_collection_unlock(struct page_collection *p) { unlocked++; }
static void cpu_loop_exit_noexc(CPUState *c) { assert(flushes==1); longjmp(exit_env, 1); }
#define PAGE_FOR_EACH_TB(start,last,p,t,n) \
    for (unsigned i=0; i<3 && ((t=&blocks[i]), (n=page_link[i]), true); i++)
/* PRODUCTION */
static void run(CPUState *cpu, struct page_collection *pages, PageDesc *pd,
                uint64_t address, unsigned width, uintptr_t ra)
{
    if (!setjmp(exit_env))
        tb_invalidate_phys_page_range__locked(cpu, pages, pd, address, address + width - 1, ra);
}
int main(void)
{
    unsigned cases=0;
    struct { bool precise_smc; } ops = { true };
    /* Avoid incompatible anonymous struct assignments. */
    CPUState cpu = {0};
    typeof(*cpu.cc) cc = {0};
    typeof(*cc.tcg_ops) tcg_ops = {ops.precise_smc};
    cc.tcg_ops=&tcg_ops; cpu.cc=&cc; only_cpu=&cpu;
    struct page_collection pages={0};
    for (unsigned second=0; second<2; second++)
    for (unsigned writer=0; writer<6; writer++)
    for (unsigned single=0; single<2; single++)
    for (unsigned width=1; width<=8; width*=2)
    for (unsigned off=0; off<4096; off+=width) {
        uint64_t page = second ? 0x9000 : 0x1000;
        blocks[0]=(TranslationBlock){{0x1ff0,0x9000},32,single?1:8,0};
        blocks[1]=(TranslationBlock){{page+32,UINT64_MAX},16,8,1};
        blocks[2]=(TranslationBlock){{page+128,UINT64_MAX},64,8,2};
        page_link[0]=second; page_link[1]=page_link[2]=0;
        TranslationBlock external={{0x5000,UINT64_MAX},64,8,3};
        running=writer==0?&blocks[0]:writer==1?&blocks[1]:&external;
        tcg_ops.precise_smc=writer!=5;
        CPUState *context=writer==3?NULL:&cpu;
        uintptr_t ra=writer==4?0:1;
        bool precise=writer<2;
        unsigned expected=0;
        for(unsigned j=0;j<3;j++) {
            /* Independent physical-byte oracle, including a discontiguous
             * second page. No production range formula is repeated here. */
            bool hit=false;
            for(unsigned k=0;k<blocks[j].size;k++) {
                uint64_t address=blocks[j].page[0]+k;
                if ((address&TARGET_PAGE_MASK)!=(blocks[j].page[0]&TARGET_PAGE_MASK))
                    address=blocks[j].page[1]+(address&4095);
                if(address>=page+off && address<page+off+width) hit=true;
            }
            if(!precise||hit) expected|=1u<<j;
        }
        bool restart=precise && (expected&(1u<<running->id)) && running->cflags!=1;
        PageDesc pd={3}; active_page=&pd;
        invalid=restored=unlocked=unprotected=flushes=0;cpu.cflags_next_tb=0;
        run(context,&pages,&pd,page+off,width,ra);
        assert(invalid==expected);
        assert(flushes==(expected!=0));
        assert(restored==restart && unlocked==restart);
        assert(unprotected==(expected==7));
        assert(cpu.cflags_next_tb==(restart?(1|CF_NOIRQ|512):0));
        cases++;
    }
    printf("PASS: %u physical store cases (adjacent data, real SMC, two-page aliases, external/DMA, precise-SMC disabled)\n",cases);
    return 0;
}
