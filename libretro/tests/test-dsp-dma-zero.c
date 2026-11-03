/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "hw/xbox/mcpx/apu/dsp/dsp_dma.h"
#include "hw/xbox/mcpx/apu/dsp/dsp_dma_regs.h"
#include "hw/xbox/mcpx/apu/dsp/interp/dsp_cpu_regs.h"
#define MIN(a,b) ((a)<(b)?(a):(b))
static jmp_buf rejected;
static bool expect_rejection;
static unsigned cases, checks;
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"DMA zero: line %u: %s\n",__LINE__,#x); exit(1); } checks++; } while(0)
static void dma_assert(bool valid,const char *expression)
{
 if(valid)return;
 if(expect_rejection && strstr(expression,"Dsp dma offset out of range")) longjmp(rejected,1);
 fprintf(stderr,"DMA zero: production assertion: %s\n",expression);exit(1);
}
#undef assert
#define assert(x) dma_assert(!!(x),#x)
/* PRODUCTION */
typedef struct {
 DSPDMAState s;
 uint32_t memory[3][0x800], expected[3][0x800];
 uint8_t host[1024], gold[1024];
 unsigned reads,writes,maps,walks,bytes,descriptor_space,size,cursor;
 bool zero,direct,to_host;
} Machine;
static Machine m;
static uint32_t mem_read(void *opaque,int space,uint32_t address)
{
 REQUIRE(opaque==&m && space>=0 && space<3 && address<0x800);
 if(m.zero) REQUIRE(space==(int)m.descriptor_space && address>=0x100 && address<=0x106);
 m.reads++; return m.memory[space][address];
}
static void mem_write(void *opaque,int space,uint32_t address,uint32_t value)
{
 REQUIRE(opaque==&m && space>=0 && space<3 && address<0x800);
 if(m.zero) REQUIRE(space==(int)m.descriptor_space && address==0x104);
 m.writes++;m.memory[space][address]=value;
}
static uint32_t *mem_ptr(void *opaque,int space,uint32_t address,uint32_t count)
{
 REQUIRE(opaque==&m && !m.zero && space>=0 && space<3 && address+count<0x800);
 m.maps++; return m.direct && space!=DSP_SPACE_P ? &m.memory[space][address] : NULL;
}
static void span(unsigned address,size_t len,bool dir,dsp_dma_run_func fn,void *user)
{
 REQUIRE(address+len<=sizeof(m.host) && dir==m.to_host);
 m.walks++;
 while(len) {
  size_t n=MIN(len,(m.walks%3+1)*m.size);
  fn(user,m.host+address,n);m.bytes+=n;address+=n;len-=n;
 }
}
static void scratch(void *opaque,uint32_t address,size_t len,bool dir,dsp_dma_run_func fn,void *user)
{ REQUIRE(opaque==&m);span(address,len,dir,fn,user); }
static void fifo(void *opaque,unsigned index,size_t len,bool dir,dsp_dma_run_func fn,void *user)
{
 REQUIRE(opaque==&m && index<4 && dir);
 unsigned ring=16*m.size,base=384+index*128;
 while(len) {
  unsigned n=MIN(len,ring-m.cursor);
  span(base+m.cursor,n,dir,fn,user);len-=n;m.cursor=(m.cursor+n)%ring;
 }
}
static void init(unsigned dsp_offset,unsigned descriptor_space,bool direct,bool direction,unsigned size)
{
 memset(&m,0,sizeof(m));m.zero=dsp_offset>=0x4000&&dsp_offset<0x8000;
 m.descriptor_space=descriptor_space;m.direct=direct;m.to_host=direction;m.size=size;m.cursor=3*size;
 m.s.mem_opaque=&m;m.s.mem_read=mem_read;m.s.mem_write=mem_write;m.s.mem_ptr=mem_ptr;
 m.s.rw_opaque=&m;m.s.scratch_walk=scratch;m.s.fifo_walk=fifo;
 for(unsigned s=0;s<3;s++)for(unsigned i=0;i<0x800;i++)m.memory[s][i]=((i*0x10307u)^(s*0x37A581u))&0xFFFFFF;
 for(unsigned i=0;i<sizeof(m.host);i++)m.host[i]=(i*29+7)&255;
}
static void completion(void)
{
 REQUIRE(m.s.eol && m.s.next_block==NODE_POINTER_EOL);
 REQUIRE(dsp_dma_read(&m.s,DMA_CONTROL)&DMA_CONTROL_RUNNING);
 REQUIRE(dsp_dma_read(&m.s,DMA_CONTROL)&DMA_CONTROL_RUNNING);
 REQUIRE(!(dsp_dma_read(&m.s,DMA_CONTROL)&DMA_CONTROL_RUNNING));
 REQUIRE(m.s.control&DMA_CONTROL_STOPPED);
}
static void transfer(unsigned dsp_offset,unsigned descriptor_space,bool direct,unsigned format,unsigned buf,bool direction,unsigned count,bool interleave)
{
 unsigned size=format==1?2:4,channels=3,blocks=5,words=interleave?channels*blocks:count;
 init(dsp_offset,descriptor_space,direct,direction,size);
 unsigned raw_descriptor=0x100+(descriptor_space==1?0x1800:descriptor_space==2?0x2800:0);
 unsigned space=dsp_offset<0x1800?0:dsp_offset<0x2000?1:2;
 unsigned address=0x200,scratch_size=5*size,offset=4*size;
 uint32_t *d=m.memory[descriptor_space]+0x100;
 d[0]=NODE_POINTER_EOL;d[1]=(format<<10)|(buf<<5)|(direction?NODE_CONTROL_DIRECTION:0)|16|interleave;
 d[2]=interleave?(blocks<<4)|(channels-1):count;d[3]=dsp_offset;d[4]=offset;d[5]=128;d[6]=scratch_size-1;
 memcpy(m.expected,m.memory,sizeof(m.expected));memcpy(m.gold,m.host,sizeof(m.gold));
 for(unsigned n=0;n<words;n++) {
  unsigned index=interleave?(n%channels)*blocks+n/channels:n;
  unsigned host_address=buf<4?384+buf*128+(3*size+n*size)%(16*size):128+(buf==14?(offset+n*size)%scratch_size:offset+n*size);
  if(direction) {
   uint32_t value=m.zero?0:m.memory[space][address+index];
   if(size==2)value>>=8;
   for(unsigned b=0;b<size;b++)m.gold[host_address+b]=(value>>(8*b))&255;
  } else if(!m.zero) {
   uint32_t value=0;
   for(unsigned b=0;b<size;b++)value|=(uint32_t)m.host[host_address+b]<<(8*b);
   m.expected[space][address+index]=size==2?value<<8:value&0xFFFFFF;
  }
 }
 m.expected[descriptor_space][0x104]=buf==14?(offset+words*size)%scratch_size:offset;
 dsp_dma_write(&m.s,DMA_NEXT_BLOCK,raw_descriptor);
 dsp_dma_write(&m.s,DMA_CONTROL,DMA_CONTROL_ACTION_START);
 REQUIRE(m.bytes==words*size);
 if(buf<4)REQUIRE(m.cursor==(3*size+words*size)%(16*size));
 REQUIRE(!memcmp(m.host,m.gold,sizeof(m.host)));
 REQUIRE(!memcmp(m.memory,m.expected,sizeof(m.memory)));
 bool mapped=direct&&space!=DSP_SPACE_P;
 REQUIRE(m.maps==(m.zero?0:1));
 REQUIRE(m.reads==7+(direction&&!m.zero&&!mapped?words:0));
 REQUIRE(m.writes==1+(!direction&&!m.zero&&!mapped?words:0));
 completion();cases++;
}
static void invalid(unsigned address)
{
 init(address,0,false,true,4);
 uint32_t *d=m.memory[0]+0x100;
 d[0]=NODE_POINTER_EOL;d[1]=(2<<10)|(15<<5)|NODE_CONTROL_DIRECTION;
 d[2]=1;d[3]=address;d[4]=0;d[5]=128;d[6]=63;
 expect_rejection=true;
 if(!setjmp(rejected)) {
  dsp_dma_write(&m.s,DMA_NEXT_BLOCK,0x100);
  dsp_dma_write(&m.s,DMA_CONTROL,DMA_CONTROL_ACTION_START);
  REQUIRE(false);
 }
 expect_rejection=false;REQUIRE(!m.maps&&!m.walks&&!m.writes);cases++;
}
static void chain_and_freeze(void)
{
 init(0x4000,0,false,true,4);m.zero=false;
 uint32_t *a=m.memory[0]+0x100,*b=m.memory[0]+0x108;
 for(unsigned i=0;i<2;i++) {
  uint32_t *d=i?b:a;
  d[0]=i?NODE_POINTER_EOL:0x108;
  d[1]=(2<<10)|(15<<5)|NODE_CONTROL_DIRECTION|16;
  d[2]=4;d[3]=i?0x200:0x4000;d[4]=i?16:0;d[5]=128;d[6]=63;
 }
 memcpy(m.expected,m.memory,sizeof(m.expected));memcpy(m.gold,m.host,sizeof(m.gold));
 memset(m.gold+128,0,16);
 for(unsigned i=0;i<4;i++)for(unsigned byte=0;byte<4;byte++)m.gold[144+i*4+byte]=m.memory[0][0x200+i]>>(byte*8);
 dsp_dma_write(&m.s,DMA_NEXT_BLOCK,0x100);
 dsp_dma_write(&m.s,DMA_CONTROL,DMA_CONTROL_ACTION_FREEZE);
 dsp_dma_write(&m.s,DMA_CONTROL,DMA_CONTROL_ACTION_START);
 REQUIRE(!m.reads&&!m.maps&&!m.walks&&!m.s.eol);
 REQUIRE((m.s.control&(DMA_CONTROL_RUNNING|DMA_CONTROL_FROZEN))==(DMA_CONTROL_RUNNING|DMA_CONTROL_FROZEN));
 dsp_dma_write(&m.s,DMA_CONTROL,DMA_CONTROL_ACTION_UNFREEZE);
 REQUIRE(m.bytes==32 && m.reads==18 && m.maps==1 && m.writes==2);
 REQUIRE(!memcmp(m.host,m.gold,sizeof(m.host)));
 REQUIRE(!memcmp(m.memory,m.expected,sizeof(m.memory)));
 completion();cases++;
}
int main(void)
{
 static const unsigned offsets[]={0x4000,0x4001,0x7FFF,0x200,0x1A00,0x2A00};
 static const unsigned formats[]={1,2,6},counts[]={0,1,7,31},buffers[]={0,1,2,3,14,15};
 for(unsigned o=0;o<6;o++)for(unsigned f=0;f<3;f++)for(unsigned sp=0;sp<3;sp++)for(unsigned direct=0;direct<2;direct++) {
  for(unsigned b=0;b<6;b++)for(unsigned n=0;n<4;n++) {
   transfer(offsets[o],sp,direct,formats[f],buffers[b],true,counts[n],false);
   if(buffers[b]>=14)transfer(offsets[o],sp,direct,formats[f],buffers[b],false,counts[n],false);
  }
  for(unsigned b=0;b<6;b++)transfer(offsets[o],sp,direct,formats[f],buffers[b],true,0,true);
 }
 static const unsigned rejected_offsets[]={0x2000,0x27FF,0x3800,0x3FFF,0x8000,0xBFFF,0xC000,0xFFFFFF};
 for(unsigned i=0;i<8;i++)invalid(rejected_offsets[i]);
 chain_and_freeze();
 printf("DMA zero: %u transfers/range rejections; %u checks passed\n",cases,checks);
 return 0;
}
