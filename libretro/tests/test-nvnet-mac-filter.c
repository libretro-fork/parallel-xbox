/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/mcpx/nvnet/nvnet_regs.h"
typedef struct {uint32_t regs[256];} NvNetState;
static uint32_t get_reg(NvNetState*s,unsigned reg){assert(reg%4==0&&reg<1024);return s->regs[reg/4];}
static bool is_broadcast_ether_addr(const uint8_t*p){for(unsigned i=0;i<6;i++)if(p[i]!=255)return false;return true;}
static inline uint32_t ldl_le_p(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static inline uint16_t lduw_le_p(const uint8_t*p){return p[0]|(uint16_t)p[1]<<8;}
#ifndef _WIN32
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define cpu_to_le32(v) __builtin_bswap32(v)
#else
#define cpu_to_le32(v) (v)
#endif
#else
#define cpu_to_le32(v) (v)
#endif
#define MAC_ARG(p) ((uint8_t*)(p))[0],((uint8_t*)(p))[1],((uint8_t*)(p))[2],((uint8_t*)(p))[3],((uint8_t*)(p))[4],((uint8_t*)(p))[5]
typedef struct {unsigned count,kind[2];uint8_t bytes[2][6];} Trace;
static Trace actual;
static void record(unsigned kind,const uint8_t bytes[6]){assert(actual.count<2);actual.kind[actual.count]=kind;memcpy(actual.bytes[actual.count++],bytes,6);}
static void trace_nvnet_rx_filter_bcast_match(void){const uint8_t zero[6]={0};record(1,zero);}
#define TRACE(name,kind) static void name(uint8_t a,uint8_t b,uint8_t c,uint8_t d,uint8_t e,uint8_t f){const uint8_t bytes[]={a,b,c,d,e,f};record(kind,bytes);}
TRACE(trace_nvnet_rx_filter_mcast_match,2)
TRACE(trace_nvnet_rx_filter_mcast_mismatch,3)
TRACE(trace_nvnet_rx_filter_ucast_match,4)
TRACE(trace_nvnet_rx_filter_ucast_mismatch,5)
/* FILTER */
static uint8_t register_byte(NvNetState*s,unsigned lo,unsigned hi,unsigned i){return get_reg(s,i<4?lo:hi)>>(8*(i%4));}
static bool oracle(NvNetState*s,const uint8_t*p,unsigned size,Trace *trace){
 if(size<6)return false;
 bool broadcast=true;for(unsigned i=0;i<6;i++)broadcast&=p[i]==255;
 if(broadcast){trace->count=1;trace->kind[0]=1;return true;}
 if(!(get_reg(s,NVNET_PACKET_FILTER)&NVNET_PACKET_FILTER_MYADDR))return true;
 uint8_t target[6],masked[6];bool enabled=false,match=true;
 for(unsigned i=0;i<6;i++){
  target[i]=register_byte(s,NVNET_MULTICAST_ADDR_A,NVNET_MULTICAST_ADDR_B,i);enabled|=target[i]!=255;
  masked[i]=p[i]&register_byte(s,NVNET_MULTICAST_MASK_A,NVNET_MULTICAST_MASK_B,i);match&=masked[i]==target[i];
 }
 if(enabled){trace->kind[trace->count]=match?2:3;memcpy(trace->bytes[trace->count++],masked,6);if(match)return true;}
 match=true;for(unsigned i=0;i<6;i++)match&=p[i]==register_byte(s,NVNET_MAC_ADDR_A,NVNET_MAC_ADDR_B,i);
 trace->kind[trace->count]=match?4:5;memcpy(trace->bytes[trace->count++],p,6);return match;
}
static uint64_t state=UINT64_C(0xabc5679812);
static uint32_t random32(void){state^=state<<13;state^=state>>7;state^=state<<17;return state>>16;}
static void mac_registers(NvNetState*s,unsigned lo,unsigned hi,const uint8_t p[6]){
 s->regs[lo/4]=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
 s->regs[hi/4]=p[4]|(uint32_t)p[5]<<8|(random32()&0xffff0000u);
}
int main(void){
 for(unsigned trial=0;trial<200000;trial++){
  NvNetState s;for(unsigned i=0;i<256;i++)s.regs[i]=random32();
  unsigned style=trial%10,size=style==8?(trial/10)%6:style==9?1500:6,offset=trial%8;
  uint8_t *storage=malloc(size+offset?size+offset:1),*p=storage+offset;assert(storage);
  for(unsigned i=0;i<size;i++)p[i]=random32();
  s.regs[NVNET_PACKET_FILTER/4]|=NVNET_PACKET_FILTER_MYADDR;
  if(style==1)memset(p,255,6);
  if(style==2)mac_registers(&s,NVNET_MAC_ADDR_A,NVNET_MAC_ADDR_B,p);
  if(style==3||style==6){
   uint8_t target[6];for(unsigned i=0;i<6;i++)target[i]=p[i]&register_byte(&s,NVNET_MULTICAST_MASK_A,NVNET_MULTICAST_MASK_B,i);
   if(style==6)target[5]^=1;
   mac_registers(&s,NVNET_MULTICAST_ADDR_A,NVNET_MULTICAST_ADDR_B,target);
  }
  if(style==4){s.regs[NVNET_MULTICAST_ADDR_A/4]=UINT32_MAX;s.regs[NVNET_MULTICAST_ADDR_B/4]=random32()|0xffffu;}
  if(style==5){s.regs[NVNET_MULTICAST_ADDR_A/4]=s.regs[NVNET_MULTICAST_ADDR_B/4]=s.regs[NVNET_MULTICAST_MASK_A/4]=s.regs[NVNET_MULTICAST_MASK_B/4]=0;}
  if(style==7)s.regs[NVNET_PACKET_FILTER/4]&=~NVNET_PACKET_FILTER_MYADDR;
  NvNetState before=s;uint8_t *copy=malloc(size+1);assert(copy);memcpy(copy,p,size);
  actual=(Trace){0};Trace expected={0};bool want=oracle(&s,size?p:NULL,size,&expected);
  bool got=receive_filter(&s,size?p:NULL,size);
  assert(got==want&&!memcmp(&actual,&expected,sizeof(actual)));
  assert(!memcmp(&s,&before,sizeof(s))&&!memcmp(p,copy,size));free(copy);free(storage);
 }
 puts("PASS: 200000 MAC filter cases; byte-wise decisions/traces, all address classes, masks, unused high bits, short/unaligned packets and immutable input/registers");
}
