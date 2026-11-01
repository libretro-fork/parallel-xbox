/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(c) do{if(!(c)){fprintf(stderr,"TCG budget: %u: %s\n",__LINE__,#c);_Exit(1);}}while(0)
#define XBOX 1
#ifndef __x86_64__
#define __x86_64__ 1
#endif
#define TCG_TARGET_REG_BITS 64
#define TCG_CALL_RET_BY_REF 1
#define OPC_MOVZWL 2
#define OPC_MOVL_EvGv 4
#define OPC_MOVL_EvIz 6
#define P_DATA16 256
#define TCG_REG_R15 15
#define TCG_TARGET_LR_ICOUNT_REG TCG_REG_R15
#define TCG_REG_EAX 0
#define TCG_AREG0 5
#define TCG_TYPE_I32 32
#define TCG_TYPE_I64 64
typedef int TCGType;
typedef int TCGReg;
typedef intptr_t tcg_target_long;
typedef uint8_t tcg_insn_unit;
typedef struct {int out_kind;} TCGHelperInfo;
typedef struct {uint32_t head;struct{struct{struct{uint16_t low,high;}u16;}icount_decr;}neg;uint64_t tail[4];} CPUState;
typedef struct {uint32_t reg[16];uint8_t mem[512];unsigned events;int event[8];ptrdiff_t immediate;int delta;bool request;}TCGContext;
static uint16_t read16(TCGContext*s,ptrdiff_t off){uint16_t v;memcpy(&v,s->mem+128+off,2);return v;}
static void write16(TCGContext*s,ptrdiff_t off,uint16_t v){memcpy(s->mem+128+off,&v,2);}
static void event(TCGContext*s,int e){assert(s->events<8);s->event[s->events++]=e;}
static void tcg_out_modrm_offset(TCGContext*s,int op,int reg,int base,ptrdiff_t off){
 (void)base;
 if(op==OPC_MOVZWL){s->reg[reg]=read16(s,off);event(s,1);}
 else if(op==(OPC_MOVL_EvGv|P_DATA16)){write16(s,off,s->reg[reg]);event(s,2);}
 else {assert(op==(OPC_MOVL_EvIz|P_DATA16));s->immediate=off;}
}
static bool tcg_out_mov(TCGContext*s,int type,int dst,int src){assert(type==TCG_TYPE_I32);s->reg[dst]=s->reg[src];event(s,3);return true;}
static void tcg_out_ext16u(TCGContext*s,int dst,int src){s->reg[dst]=s->reg[src]&65535;event(s,4);}
static void tcg_out_movi(TCGContext*s,int type,int dst,intptr_t v){assert(type==TCG_TYPE_I32);s->reg[dst]=v;event(s,5);}
static void tcg_out16(TCGContext*s,intptr_t v){write16(s,s->immediate,v);event(s,6);}
#ifndef _WIN32
static inline void tcg_out_push(TCGContext*s,int reg){(void)s;(void)reg;assert(false);}
#endif
static void tcg_out_branch(TCGContext*s,int call,const tcg_insn_unit*dest){
 (void)dest;assert(call==1);event(s,7);
 ptrdiff_t low=(ptrdiff_t)offsetof(CPUState,neg.icount_decr.u16.low)-(ptrdiff_t)sizeof(CPUState);
 write16(s,low,read16(s,low)+s->delta);
 if(s->request)write16(s,low+2,65535);
 s->reg[0]=0xC0FFEE;
}
/* PRODUCTION */
int main(void){
 unsigned cases=0;ptrdiff_t low=LR_ICOUNT_OFFSET;
 for(int base=0;base<16;base++)for(ptrdiff_t off=-120;off<=120;off++)for(int width=32;width<=64;width+=32){
  TCGContext s={0};s.reg[15]=0x1234;write16(&s,off,0xABCD);
  tgen_ld16u(&s,width,2,base,off);assert(s.events==1);
  bool shadow=base==TCG_AREG0&&off==low;
  assert(s.reg[2]==(shadow?0x1234:0xABCD));assert(s.event[0]==(shadow?3:1));cases++;
  s.events=0;s.reg[3]=0xBABE5678;write16(&s,low+2,0xA5A5);
  tgen_st16_r(&s,width,3,base,off);assert(s.events==1);
  if(shadow){assert(s.reg[15]==0x5678);assert(read16(&s,low+2)==0xA5A5);assert(s.event[0]==4);}
  else {assert(read16(&s,off)==0x5678);assert(s.reg[15]==0x1234);assert(s.event[0]==2);}cases++;
 }
 static const intptr_t immediates[]={0,1,32767,32768,65535,65536,-1,0x12345678};
 for(unsigned i=0;i<sizeof(immediates)/sizeof(*immediates);i++)for(int base=0;base<16;base++)for(ptrdiff_t off=-120;off<=120;off++){
  TCGContext s={0};s.reg[15]=123;
  tgen_st16_i(&s,TCG_TYPE_I32,immediates[i],base,off);assert(s.events==1);
  if(base==TCG_AREG0&&off==low){assert(s.reg[15]==(immediates[i]&65535));assert(s.event[0]==5);}
  else {assert(read16(&s,off)==(immediates[i]&65535));assert(s.reg[15]==123);assert(s.event[0]==6);}cases++;
 }
 for(unsigned v=0;v<=65535;v++){
  TCGContext s={0};s.reg[3]=0xBEEF0000|v;write16(&s,low+2,0x9876);
  tgen_st16_r(&s,TCG_TYPE_I32,3,TCG_AREG0,low);assert(s.reg[15]==v);
  tcg_lr_budget_publish(&s);assert(read16(&s,low)==v);assert(read16(&s,low+2)==0x9876);cases++;
 }
 static const int deltas[]={-32,-1,0,1,32};TCGHelperInfo info={0};
 for(unsigned v=0;v<=65535;v+=17)for(unsigned d=0;d<sizeof(deltas)/sizeof(*deltas);d++)for(unsigned irq=0;irq<2;irq++){
  TCGContext s={.delta=deltas[d],.request=irq};s.reg[15]=v;write16(&s,low,0xDEAD);write16(&s,low+2,0x0123);
  tcg_out_call(&s,NULL,&info);
  assert(s.events==3&&s.event[0]==2&&s.event[1]==7&&s.event[2]==1);
  assert(s.reg[15]==(uint16_t)(v+deltas[d]));assert(s.reg[0]==0xC0FFEE);
  assert(read16(&s,low+2)==(irq?65535:0x0123));cases++;
 }
 printf("TCG budget register: %u field-exclusion/truncation/helper/request-half cases passed; all helper call sites synchronized\n",cases);
}
