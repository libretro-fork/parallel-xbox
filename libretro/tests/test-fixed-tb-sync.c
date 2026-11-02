/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define XBOX 1
#define CF_PCREL 0x20u
#define HF_CS64_MASK 0x40u
#define TB_EXIT_IDX1 1
#define assert(c) do {if(!(c)){fprintf(stderr,"tb sync: %d\n",__LINE__);_Exit(1);}}while(0)
typedef struct {uint64_t eip, untouched;} CPUX86State;
typedef struct {CPUX86State env;} CPUState;
typedef struct {uint64_t pc,cs_base;uint32_t flags,cflags;} TranslationBlock;
#define cpu_env(c) (&(c)->env)
static uint32_t tb_cflags(const TranslationBlock *tb){return tb->cflags;}
/* PRODUCTION */
static uint64_t seed=0x123456789abcdef0ULL;
static uint64_t random64(void){seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;}
int main(void){unsigned count=0;
 for(unsigned i=0;i<65536;i++)for(unsigned mode=0;mode<4;mode++)for(int exit=0;exit<4;exit++){
  CPUState cpu={{random64(),random64()}},expected=cpu;
  TranslationBlock tb={random64(),random64(),(mode&1)?HF_CS64_MASK:0,(mode&2)?CF_PCREL:0};
  if(exit>TB_EXIT_IDX1 && !(tb.cflags&CF_PCREL))expected.env.eip=(tb.flags&HF_CS64_MASK)?tb.pc:(uint32_t)(tb.pc-tb.cs_base);
  sync_exit(&cpu,&tb,&exit);assert(!memcmp(&cpu,&expected,sizeof(cpu)));count++;
 }
 printf("Fixed TB sync: %u full-state PC-relative/32-bit/64-bit/normal/request-exit cases passed\n",count);
}
