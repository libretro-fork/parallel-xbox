/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(c) do{if(!(c)){fprintf(stderr,"early wait: %u: %s\n",__LINE__,#c);_Exit(1);}}while(0)
#define CF_USE_ICOUNT 1u
#define CF_NOIRQ 2u
#define TARGET_PAGE_SIZE 4096u
#define TARGET_PAGE_MASK (~(uint64_t)4095u)
typedef struct { unsigned cflags,icount; } TranslationBlock;
static unsigned tb_cflags(const TranslationBlock *tb){return tb->cflags;}
typedef struct { const uint8_t *host_addr[2]; uint64_t pc_first; TranslationBlock *tb; } DisasContextBase;
typedef struct CPUState { struct { struct { struct {uint16_t low,high;}u16;}icount_decr; int lr_quiet;}neg; bool proven; } CPUState;
typedef CPUState CPUX86State;
static CPUState *env_cpu(CPUX86State *env){return env;}
static bool xemu_libretro_memory_poll(CPUState *cpu){return cpu->proven;}
static uint16_t retro_atomic_load_relaxed_u16(const uint16_t *p){return *p;}
static void retro_atomic_store_relaxed_u16(uint16_t *p,uint16_t v){*p=v;}
/* PRODUCTION */
int main(void)
{
    unsigned cases=0;
    uint8_t code[]={0x8B,0x0A,0x8B,0xFE,0x2B,0xF9,0x3B,0xC7,0x72,0xF6};
    TranslationBlock tb={.cflags=CF_USE_ICOUNT,.icount=5};
    DisasContextBase db={.host_addr={code,NULL},.pc_first=0x80001000,.tb=&tb};
    assert(xemu_ram_wait_candidate(&db));
    for(unsigned b=0;b<sizeof(code);b++)for(unsigned v=0;v<256;v++){
        unsigned saved=code[b];code[b]=v;
        assert(xemu_ram_wait_candidate(&db)==(v==saved));code[b]=saved;cases++;
    }
    for(unsigned flags=0;flags<8;flags++)for(unsigned offset=0;offset<TARGET_PAGE_SIZE;offset++){
        tb.cflags=flags;db.pc_first=offset;
        assert(xemu_ram_wait_candidate(&db)==((flags&3)==1 && offset<=4096-sizeof(code)));cases++;
    }
    db.host_addr[0]=NULL;assert(!xemu_ram_wait_candidate(&db));
    static const unsigned counts[]={1,2,5,32,64,128,512};
    for(unsigned c=0;c<sizeof(counts)/sizeof(*counts);c++)for(unsigned before=counts[c];before<=65535;before++){
        tb.icount=counts[c];
        for(unsigned proven=0;proven<2;proven++)for(unsigned request=0;request<2;request++){
            CPUState cpu={.neg={.icount_decr={.u16={before-counts[c],request?65535:0}},.lr_quiet=17},.proven=proven};
            assert(helper_xemu_memory_wait(&cpu,&tb)==proven);
            assert(cpu.neg.icount_decr.u16.low==(proven?before:before-counts[c]));
            assert(cpu.neg.icount_decr.u16.high==(request?65535:0));
            assert(cpu.neg.lr_quiet==(proven?-1:17));cases++;
        }
    }
    printf("early memory wait: %u nomination/prepaid-budget/request-half cases passed\n",cases);
}
