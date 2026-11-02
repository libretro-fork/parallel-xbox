/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(c) do { if (!(c)) { fprintf(stderr,"generated MMU: %u: %s\n",__LINE__,#c); _Exit(1); } } while(0)
#define QEMU_BUILD_BUG_ON(c) _Static_assert(!(c),#c)
#define CONFIG_SOFTMMU 1
#define TARGET_PAGE_BITS 12
#define TB_JMP_CACHE_SIZE_MAX 65536
#define TB_JMP_DIRTY_WORDS 32
#define TB_JMP_CACHE_BITS (xemu_cpu_boost?16:12)
#define TB_JMP_PAGE_BITS (TB_JMP_CACHE_BITS/2)
#define TB_JMP_ADDR_MASK ((1u<<TB_JMP_PAGE_BITS)-1)
#define TB_JMP_PAGE_MASK ((1u<<TB_JMP_CACHE_BITS)-(1u<<TB_JMP_PAGE_BITS))
#define XEMU_TB_CF_ANY 0xc0000004u
#define CF_COUNT_MASK 0xffu
#define CF_NO_GOTO_PTR (1u<<8)
#define CF_SINGLE_STEP (1u<<9)
#define CF_MEMI_ONLY (1u<<10)
#define CF_BP_PAGE (1u<<11)
#define CF_NOIRQ (1u<<12)
#define CF_INVALID (1u<<16)
typedef uint64_t vaddr;
typedef int64_t tb_page_addr_t;
typedef struct {uint32_t flags,cflags;uint64_t cs_base;int64_t page_addr[2];struct {void *ptr;} tc;} TranslationBlock;
typedef TranslationBlock *retro_atomic_ptr_t;
typedef uint32_t retro_atomic_int_t;
struct rcu_head {uintptr_t words[2];};
/* LAYOUT */
typedef struct {CPUJumpCache *tb_jmp_cache;struct {void *tqh_first;} breakpoints;} CPUState;
static bool xemu_cpu_boost;
static uint32_t tb_cflags(const TranslationBlock *tb){return tb->cflags;}
static struct {TranslationBlock *gen_tb;} context,*tcg_ctx=&context;
typedef unsigned TCGv_i64,TCGv_i32,TCGv_ptr;
typedef struct {int unused;} TCGLabel;
enum {TCG_COND_EQ,TCG_COND_NE};
static uint64_t values[64];static unsigned next;
static bool stopped,rejected,fast,fallback;
static TCGv_ptr tcg_env;
static unsigned temp(void){assert(next<64);return next++;}
#define tcg_temp_ebb_new_i64 temp
#define tcg_temp_ebb_new_i32 temp
#define tcg_temp_ebb_new_ptr temp
static TCGLabel *gen_new_label(void){static TCGLabel label;return &label;}
static void gen_set_label(TCGLabel *label){(void)label;stopped=!rejected;}
static uint64_t load(unsigned base,intptr_t offset,size_t size){uint64_t v=0;if(!stopped)memcpy(&v,(void *)(uintptr_t)(values[base]+offset),size);return v;}
#define LOAD(name,type) static void __attribute__((unused)) name(unsigned d,unsigned b,intptr_t o){if(!stopped)values[d]=load(b,o,sizeof(type));}
LOAD(tcg_gen_ld_ptr,uintptr_t)
LOAD(tcg_gen_ld_i64,uint64_t)
LOAD(tcg_gen_ld_i32,uint32_t)
LOAD(tcg_gen_ld32u_i64,uint32_t)
LOAD(tcg_gen_ld8u_i32,uint8_t)
#define BIN(name,op) static void name(unsigned d,unsigned a,unsigned b){if(!stopped)values[d]=values[a] op values[b];}
BIN(tcg_gen_xor_i64,^)
BIN(tcg_gen_or_i64,|)
BIN(tcg_gen_add_ptr,+)
#define IMM(name,op) static void name(unsigned d,unsigned a,uint64_t b){if(!stopped)values[d]=values[a] op b;}
IMM(tcg_gen_shri_i64,>>)
IMM(tcg_gen_shli_i64,<<)
IMM(tcg_gen_andi_i64,&)
static void tcg_gen_xori_i32(unsigned d,unsigned a,uint32_t b){if(!stopped)values[d]=(uint32_t)values[a]^b;}
static unsigned tcg_constant_i32(uint32_t v){unsigned d=temp();values[d]=v;return d;}
static void tcg_gen_movcond_i32(int cond,unsigned d,unsigned a,unsigned b,unsigned yes,unsigned no){assert(cond==TCG_COND_EQ);if(!stopped)values[d]=(uint32_t)values[(uint32_t)values[a]==(uint32_t)values[b]?yes:no];}
static void tcg_gen_trunc_i64_ptr(unsigned d,unsigned a){if(!stopped)values[d]=(uintptr_t)values[a];}
static void branch(int cond,uint64_t a,uint64_t b){if(!stopped && ((cond==TCG_COND_EQ)==(a==b)))stopped=rejected=true;}
#define BRIMM(name,type) static void name(int c,unsigned a,type b,TCGLabel *l){(void)l;branch(c,(type)values[a],b);}
BRIMM(tcg_gen_brcondi_ptr,uintptr_t)
BRIMM(tcg_gen_brcondi_i64,uint64_t)
BRIMM(tcg_gen_brcondi_i32,uint32_t)
static void tcg_gen_brcond_i64(int c,unsigned a,unsigned b,TCGLabel *l){(void)l;branch(c,values[a],values[b]);}
static void __attribute__((unused)) tcg_gen_brcond_i32(int c,unsigned a,unsigned b,TCGLabel *l){(void)l;branch(c,(uint32_t)values[a],(uint32_t)values[b]);}
static void tcg_gen_goto_ptr(unsigned code){if(!stopped){assert(values[code]==0x12340000);fast=stopped=true;}}
static void tcg_gen_exit_tb(TranslationBlock *tb,int slot){(void)tb;assert(slot==0||slot==1);if(!stopped)fallback=stopped=true;}
static void tcg_gen_lookup_and_goto_ptr(void){if(!stopped)fallback=stopped=true;}
/* PRODUCTION */
static unsigned hash_pc(uint64_t pc){uint64_t t=pc^(pc>>(12-TB_JMP_PAGE_BITS));return ((t>>(12-TB_JMP_PAGE_BITS))&TB_JMP_PAGE_MASK)|(t&TB_JMP_ADDR_MASK);}
static uint32_t cls(unsigned n){return ((n&3u)<<30)|((n>>2)<<2);}
int main(void){
 static CPUJumpCache cache;CPUState cpu={.tb_jmp_cache=&cache};unsigned cases=0;
 for(unsigned boost=0;boost<2;boost++)for(unsigned sample=0;sample<8;sample++)
 for(int direct=-1;direct<2;direct++)for(unsigned klass=0;klass<8;klass++)
 for(unsigned generation=0;generation<4;generation++)for(unsigned bad=0;bad<14;bad++){
  xemu_cpu_boost=boost;uint64_t pc=sample==7?UINT32_MAX:0x80001000u+sample*0x4171u;
  uint32_t flags=cls(klass)|0x100000u;uint8_t epoch=(uint8_t[]){0,1,127,255}[generation];
  TranslationBlock origin={0},target={.flags=flags,.cs_base=0x12345678,.page_addr={10,20},.tc.ptr=(void *)0x12340000};
  if(bad==11)pc+=UINT64_C(1)<<32;
  unsigned h=hash_pc(pc);
#if HAS_CACHE_EPOCH
  cache.mmu_epoch=epoch;cache.array[h].mmu_epoch=epoch;
#else
  (void)epoch;
#endif
  cache.array[h].pc=(uint32_t)pc;cache.array[h].tb=&target;cpu.breakpoints.tqh_first=NULL;
  switch(bad){
   case 1:cache.array[h].tb=NULL;break;
   case 2:cache.array[h].pc^=1;break;
   case 3:target.cs_base^=1;break;
   case 4:target.flags^=1u<<18;break;
   case 5:target.cflags|=CF_INVALID;break;
   case 6:
#if HAS_CACHE_EPOCH
    cache.array[h].mmu_epoch=(uint8_t)(epoch+1);
#else
    cache.array[h].pc^=0x1000;
#endif
    break;
   case 7:cpu.breakpoints.tqh_first=&cpu;break;
   case 8:target.page_addr[1]=-1;break;
   case 9:target.flags|=XEMU_TB_CF_ANY;break;
   case 10:target.flags=(flags&~XEMU_TB_CF_ANY)|cls((klass+1)%7);break;
   case 12:origin.cflags=CF_NO_GOTO_PTR;break;
   case 13:origin.cflags=1;break;
  }
  next=1;tcg_env=0;values[tcg_env]=(uintptr_t)&cpu+sizeof(cpu);unsigned input=temp();values[input]=pc;
  stopped=rejected=fast=fallback=false;tcg_ctx->gen_tb=&origin;
  gen_lookup_and_goto_ptr(input,0x12345678,flags,direct);
  bool expected=bad==0||bad==9||(bad==8&&direct<0);
  assert(fast==expected && fallback==!expected);cases++;
 }
 printf("Generated TB cache: %u PC/class/mode/epoch/breakpoint/two-page/disabled-path decisions passed\n",cases);
}
