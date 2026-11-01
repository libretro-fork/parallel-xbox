/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "hw/xbox/nv2a/pgraph/m2mf-transfer.h"
static uint32_t rng=0x391974;
static uint32_t random_word(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static bool range_oracle(size_t bytes,uint32_t off,uint32_t pitch,uint32_t len,uint32_t rows,uint32_t stride)
{
 if(!len||!rows)return true;
 if(!stride)return false;
 __uint128_t end=(__uint128_t)off+(__uint128_t)(rows-1)*pitch+(__uint128_t)(len-1)*stride;
 return end<bytes;
}
static uint64_t progress(const PGRAPHM2MFTransfer*t){return (uint64_t)t->row*t->length+t->column;}
int main(void)
{
 uint8_t actual[512],expected[512],rebound[512];
 unsigned cases=0;
 for(unsigned i=0;i<100000;i++) {
  size_t bytes=(i&1)?SIZE_MAX:random_word();
  uint32_t off=random_word(),pitch=random_word(),len=random_word(),rows=random_word(),stride=random_word();
  assert(pgraph_m2mf_range(bytes,off,pitch,len,rows,stride)==range_oracle(bytes,off,pitch,len,rows,stride));
  cases++;
 }
 for(unsigned i=0;i<50000;i++) {
  PGRAPHM2MFTransfer t={.offset_in=random_word()%128,.offset_out=random_word()%128,
   .pitch_in=random_word()%49,.pitch_out=random_word()%49,.length=random_word()%18,
   .rows=random_word()%9,.stride_in=random_word()%5,.stride_out=random_word()%5};
  size_t in_bytes=256+random_word()%257,out_bytes=256+random_word()%257;
  bool valid=range_oracle(in_bytes,t.offset_in,t.pitch_in,t.length,t.rows,t.stride_in)&&
             range_oracle(out_bytes,t.offset_out,t.pitch_out,t.length,t.rows,t.stride_out);
  for(unsigned x=0;x<512;x++)actual[x]=expected[x]=(uint8_t)(x*37+i);
  uint8_t *ram=actual,*other=rebound;
  cases++;
  PGRAPHM2MFResult status=pgraph_m2mf_begin(&t,in_bytes,out_bytes);
  assert((status!=PGRAPH_M2MF_INVALID)==valid);
  if(!valid){assert(!memcmp(actual,expected,512));continue;}
  for(unsigned y=0;y<t.rows&&t.length;y++) {
   unsigned src=t.offset_in+y*t.pitch_in,dst=t.offset_out+y*t.pitch_out;
   if(t.stride_in==1&&t.stride_out==1)memmove(expected+dst,expected+src,t.length);
   else for(unsigned x=0;x<t.length;x++)expected[dst+x*t.stride_out]=expected[src+x*t.stride_in];
  }
  uint64_t total=(uint64_t)t.length*t.rows;
  if(status==PGRAPH_M2MF_YIELD){uint64_t before=progress(&t);assert(pgraph_m2mf_step(&t,ram,ram,0)==status);assert(progress(&t)==before);}
  unsigned calls=0;
  while(status==PGRAPH_M2MF_YIELD) {
   size_t budget=1+random_word()%23;
   uint64_t before=progress(&t),remaining=total-before;
   status=pgraph_m2mf_step(&t,ram,ram,budget);
   assert(progress(&t)-before==(remaining<budget?remaining:budget));
   assert((status==PGRAPH_M2MF_DONE)==(progress(&t)==total));
   /* Rebind all RAM and copy progress mid-transfer: no hidden pointers. */
   memcpy(other,ram,512);PGRAPHM2MFTransfer saved=t;
   uint8_t *swap=ram;ram=other;other=swap;t=saved;
   assert(++calls<=total+1);
  }
  assert(!memcmp(ram,expected,512));
  assert(pgraph_m2mf_step(&t,ram,ram,7)==PGRAPH_M2MF_DONE);
 }
 PGRAPHM2MFTransfer huge={.length=1,.rows=UINT32_MAX,.stride_in=1,.stride_out=1};
 assert(pgraph_m2mf_begin(&huge,1,1)==PGRAPH_M2MF_YIELD);
 assert(pgraph_m2mf_step(&huge,actual,actual,17)==PGRAPH_M2MF_YIELD);
 assert(huge.row==17&&huge.column==0);
 printf("PASS: %u M2MF range/progress/copy cases; huge repeated-row dispatch bounded to 17 bytes\n",cases);
 return 0;
}
