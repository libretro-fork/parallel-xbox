/* Exact wide-integer HRTF oracle using ring indexing, not the production
 * linear-history or split-accumulator implementation.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef HRTF_TEST_HEADER
#define HRTF_TEST_HEADER "hw/xbox/mcpx/apu/vp/hrtf.h"
#endif
#include HRTF_TEST_HEADER
static uint64_t random_state=UINT64_C(0x194bacdef87);
static uint32_t random32(void){random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;return random_state>>16;}
static int32_t at(const HrtfFilter *f,const int32_t in[32][2],int index){
 assert(index>=-HRTF_BUFLEN&&index<32);
 return index>=0?in[index][0]:f->buf[(f->buf_pos+HRTF_BUFLEN+index)%HRTF_BUFLEN];
}
static void reference(HrtfFilter *f,const int32_t in[32][2],int32_t out[32][2],const int32_t target[2][31],int32_t itd){
 if(itd>42*512)itd=42*512;
 if(itd< -42*512)itd= -42*512;
 int32_t from=f->params_set?f->itd_cur:itd;
 for(int n=0;n<32;n++)for(int ch=0;ch<2;ch++){
  int delay=from*(32-n)+itd*n;if(ch)delay= -delay;if(delay<0)delay=0;
  int whole=delay/16384,frac=delay%16384;__int128 total=0;
  for(int k=0;k<31;k++){
   int32_t previous=f->params_set?f->hrir_coeff_cur[ch][k]:target[ch][k];
   int64_t tap=(int64_t)previous*(32-n)+(int64_t)target[ch][k]*n;
   int index=n-whole-k;
   int64_t sample=(int64_t)at(f,in,index)*(16384-frac);
   if(frac)sample+=(int64_t)at(f,in,index-1)*frac;
   total+=(__int128)tap*sample;
  }
  const __int128 divisor=(__int128)1<<30;
  total+=(__int128)1<<29;__int128 rounded=total/divisor;
  if(total<0&&total%divisor)rounded--;
  out[n][ch]=(int32_t)(uint32_t)rounded;
 }
 int32_t history[HRTF_BUFLEN];
 for(int i=0;i<HRTF_BUFLEN;i++)history[i]=at(f,in,32-HRTF_BUFLEN+i);
 memcpy(f->buf,history,sizeof(history));f->buf_pos=0;
 memcpy(f->hrir_coeff_cur,target,sizeof(f->hrir_coeff_cur));f->itd_cur=itd;f->params_set=true;
}
static int32_t sample(unsigned style){
 static const int32_t edges[]={0,1,-1,(1<<26)-1,-((1<<26)-1),1<<26,-(1<<26),INT32_MIN,INT32_MAX};
 if(style<sizeof(edges)/sizeof(*edges))return edges[style];
 if(style==9)return (int32_t)(random32()%0x1000000)-0x800000;
 return (int32_t)random32();
}
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void process(HrtfFilter *f,const int32_t in[32][2],int32_t out[32][2],const int32_t target[2][31],int32_t itd){hrtf_filter_process(f,in,out,target,itd);}
int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"--bench")){
  for(int mode=0;mode<3;mode++){
   HrtfFilter f;hrtf_filter_init(&f);int32_t in[32][2],out[32][2],target[2][31];
   for(int i=0;i<32;i++)in[i][0]=in[i][1]=sample(mode==2?10:9);
   for(int ch=0;ch<2;ch++)for(int k=0;k<31;k++)target[ch][k]=(int32_t)(random32()%3969)-1984;
   clock_t start=clock();
   for(int frame=0;frame<200000;frame++)process(&f,(const int32_t (*)[2])in,out,(const int32_t (*)[31])target,mode==0?0:12345+(frame&31));
   printf("mode%d=%.6f checksum=%d ",mode,(double)(clock()-start)/CLOCKS_PER_SEC,out[31][1]);
  }
  puts("");return 0;
 }
 unsigned frames=0;
 for(unsigned style=0;style<11;style++)for(unsigned ring=0;ring<HRTF_BUFLEN;ring++){
  HrtfFilter actual;hrtf_filter_init(&actual);actual.buf_pos=ring;actual.params_set=ring&1;actual.itd_cur=(int32_t)(random32()%(84*512+1))-42*512;
  for(int i=0;i<HRTF_BUFLEN;i++)actual.buf[i]=sample(style);
  for(int ch=0;ch<2;ch++)for(int k=0;k<31;k++)actual.hrir_coeff_cur[ch][k]=1984;
  HrtfFilter expected=actual;
  for(int frame=0;frame<8;frame++){
   int32_t in[32][2],got[32][2],want[32][2],target[2][31];
   for(int i=0;i<32;i++){in[i][0]=sample(style);in[i][1]=(int32_t)random32();}
   for(int ch=0;ch<2;ch++)for(int k=0;k<31;k++)target[ch][k]=frame?(int32_t)(random32()%3969)-1984:1984;
   int32_t itd=frame==0?0:frame==1?INT32_MIN:frame==2?INT32_MAX:(int32_t)(random32()%(84*512+1))-42*512;
   reference(&expected,(const int32_t (*)[2])in,want,(const int32_t (*)[31])target,itd);
   process(&actual,(const int32_t (*)[2])in,got,(const int32_t (*)[31])target,itd);
   assert(!memcmp(got,want,sizeof(got))&&!memcmp(&actual,&expected,sizeof(actual)));frames++;
  }
 }
 printf("PASS: %u HRTF frames against 128-bit oracle; every ring position, ramps, delay clamp, narrow/wide boundaries and INT32_MIN\n",frames);
}
