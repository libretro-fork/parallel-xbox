/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define NV2A_MAX_BATCH_LENGTH 512u
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define GET_MASK(v,m) (((v)&(m))>>__builtin_ctz(m))
#define NV097_DRAW_ARRAYS_START_INDEX 0x00ffffffu
#define NV097_DRAW_ARRAYS_COUNT 0xff000000u
#define NV_KELVIN_PRIMITIVE 0x97u
#define retro_atomic_store_relaxed_int(p,v) (*(p)=(v))
typedef struct {
 unsigned inline_elements_length,inline_array_length,inline_buffer_length;
 uint32_t inline_elements_min,inline_elements_max,inline_elements[NV2A_MAX_BATCH_LENGTH+2];
 unsigned draw_arrays_length,draw_arrays_min_start,draw_arrays_max_count;
 int32_t draw_arrays_start[1250],draw_arrays_count[1250];bool draw_arrays_prevent_connect;
 int ctx_switch_subchannel,flush_pending;
} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
static NV2AState d;
static uint32_t reference[NV2A_MAX_BATCH_LENGTH],words[NV2A_MAX_BATCH_LENGTH+7];
static unsigned nref,flushes,reads,ntrace;
static uint32_t traced[NV2A_MAX_BATCH_LENGTH+7];
static uint32_t rng=0x164a923;
static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint32_t ldl_le_p(const void *ptr){const uint8_t *p=ptr;reads++;return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void pgraph_method_log(unsigned s,unsigned c,unsigned m,uint32_t v){(void)m;assert(s==2&&c==0x97&&ntrace<ARRAY_SIZE(traced));traced[ntrace++]=v;}
static void pgraph_check_within_begin_end_block(PGRAPHState *pg){assert(pg==&d.pgraph);}
static void flush_draw(NV2AState *state){assert(state==&d);flushes++;}
static struct {struct {void (*flush_draw)(NV2AState *);} ops;} renderer={{flush_draw}};
#define pgraph_renderer(pg) (&renderer)
static void check(void){
 PGRAPHState *pg=&d.pgraph;assert(pg->inline_elements_length==nref);
 assert(!memcmp(pg->inline_elements,reference,nref*4));
 uint32_t lo=UINT32_MAX,hi=0;for(unsigned i=0;i<nref;i++){if(reference[i]<lo)lo=reference[i];if(reference[i]>hi)hi=reference[i];}
 assert(pg->inline_elements_min==lo&&pg->inline_elements_max==hi);
 assert(pg->inline_elements[NV2A_MAX_BATCH_LENGTH]==0x12345678&&pg->inline_elements[NV2A_MAX_BATCH_LENGTH+1]==0x89abcdef);
}
static void pfb_update_zcomp(NV2AState *state){assert(state==&d);}
static void nv2a_unlock_fifo(NV2AState *state){assert(state==&d);check();}
void pgraph_reset_draw_arrays(PGRAPHState *);
void pgraph_reset_inline_buffers(PGRAPHState *);
/* PRODUCTION */
static void reset(void){memset(&d,0,sizeof(d));d.pgraph.inline_elements[NV2A_MAX_BATCH_LENGTH]=0x12345678;d.pgraph.inline_elements[NV2A_MAX_BATCH_LENGTH+1]=0x89abcdef;pgraph_reset_inline_buffers(&d.pgraph);nref=reads=ntrace=0;check();}
static void oracle(uint32_t value){assert(nref<ARRAY_SIZE(reference));reference[nref++]=value;}
static void packet(bool packed,bool inc,unsigned count){
 unsigned before=d.pgraph.inline_elements_length;size_t consumed=1;reads=ntrace=0;
 unsigned available=NV2A_MAX_BATCH_LENGTH-before;unsigned take=inc?1:MIN(count,available/(packed?2:1));
 if(packed)method_ARRAY_ELEMENT16(&d,&d.pgraph,2,0x1800,words[0],words,count,&consumed,inc);
 else method_ARRAY_ELEMENT32(&d,&d.pgraph,2,0x1808,words[0],words,count,&consumed,inc);
 assert(consumed==(inc?1:take));assert(reads==(inc||!take?0:take-1));assert(ntrace==(take?take-1:0));
 for(unsigned i=0;i<take;i++){if(i)assert(traced[i-1]==words[i]);if(packed){oracle(words[i]&65535);oracle(words[i]>>16);}else oracle(words[i]);}
 check();
}
static void draw(unsigned start,unsigned count){size_t used=1;uint32_t parameter=start|((count-1)<<24);method_DRAW_ARRAYS(&d,&d.pgraph,2,0x1810,parameter,NULL,1,&used,true);}
int main(void){
 reset();
 for(unsigned v=0;v<65536;v++){reset();words[0]=v|((65535-v)<<16);packet(true,true,0);}
 for(unsigned round=0;round<100000;round++){
  if(NV2A_MAX_BATCH_LENGTH-nref<260)reset();
  for(unsigned i=0;i<ARRAY_SIZE(words);i++)words[i]=rnd();
  unsigned op=rnd()%5,count=1+rnd()%16;
  if(op<4)packet(op<2,op&1,count);
  else {if(!nref){words[0]=rnd();packet(false,true,1);}unsigned start=rnd()&0xffff,count=1+rnd()%256;draw(start,count);for(unsigned i=0;i<count;i++)oracle(start+i);check();}
  if(round%37==0){d.pgraph.inline_elements_min=0xdeadbeef;d.pgraph.inline_elements_max=0;assert(!nv2a_post_load(&d,5));assert(d.pgraph.ctx_switch_subchannel==-1&&d.pgraph.flush_pending==1);check();}
 }
 reset();draw(10,3);assert(d.pgraph.draw_arrays_length==1);words[0]=5|(40u<<16);
 method_ARRAY_ELEMENT16(&d,&d.pgraph,2,0x1800,words[0],NULL,0,&(size_t){1},true);
 oracle(10);oracle(11);oracle(12);oracle(5);oracle(40);check();assert(!d.pgraph.draw_arrays_length);
 reset();flushes=0;draw(20,2);draw(50,3);words[0]=7;
 method_ARRAY_ELEMENT32(&d,&d.pgraph,2,0x1808,7,NULL,0,&(size_t){1},true);
 oracle(50);oracle(51);oracle(52);oracle(7);check();assert(flushes==1);
 reset();pgraph_append_inline_elements_range(&d.pgraph,UINT32_MAX-2,5);for(unsigned i=0;i<5;i++)oracle(UINT32_MAX-2+i);check();
 pgraph_append_inline_elements_range(&d.pgraph,0,0);check();
 reset();for(unsigned i=0;i<ARRAY_SIZE(words);i++)words[i]=rnd();packet(false,false,ARRAY_SIZE(words));assert(nref==NV2A_MAX_BATCH_LENGTH);
 reset();for(unsigned i=0;i<NV2A_MAX_BATCH_LENGTH-2;i++){words[0]=i;packet(false,true,1);}for(unsigned i=0;i<8;i++)words[i]=rnd();packet(true,false,8);assert(nref==NV2A_MAX_BATCH_LENGTH);
 reset();size_t used=99;reads=ntrace=0;method_ARRAY_ELEMENT32(&d,&d.pgraph,2,0x1808,123,NULL,0,&used,false);assert(!used&&!reads&&!ntrace);check();
 uint8_t unaligned[17]={0};for(unsigned i=0;i<4;i++){uint32_t value=0xface0000+i;for(unsigned j=0;j<4;j++)unaligned[1+4*i+j]=value>>(8*j);}
 used=1;reads=ntrace=0;method_ARRAY_ELEMENT32(&d,&d.pgraph,2,0x1808,0xface0000,(uint32_t *)(unaligned+1),4,&used,false);for(unsigned i=0;i<4;i++)oracle(0xface0000+i);check();assert(used==4&&reads==3&&ntrace==3);
 reset();draw(20,2);used=99;method_ARRAY_ELEMENT16(&d,&d.pgraph,2,0x1800,0,NULL,0,&used,false);assert(!used&&d.pgraph.draw_arrays_length==1&&!nref);check();
 reset();for(unsigned i=0;i<ARRAY_SIZE(words);i++)words[i]=rnd();packet(false,false,ARRAY_SIZE(words));used=99;method_ARRAY_ELEMENT16(&d,&d.pgraph,2,0x1800,0,NULL,0,&used,false);assert(!used);check();
 reset();words[0]=UINT32_MAX;packet(false,true,1);words[0]=0;packet(false,true,1);assert(!nv2a_post_load(&d,5));check();
 puts("inline index packets/bounds: 65536 complementary packed endpoint pairs, 100000 mixed operations, consumed/traced words, generated/wrapping ranges, partial capacity and legacy-load rebuild passed; UB traps enabled");return 0;
}
