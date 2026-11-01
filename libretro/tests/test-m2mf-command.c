/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "hw/xbox/nv2a/pgraph/m2mf-command.h"
#define FIELD(method,member) {method,offsetof(PGRAPHM2MFCommand,member)}
static const struct {uint32_t method;size_t offset;} fields[]={
 FIELD(NV039_SET_OBJECT,object_instance),FIELD(NV039_SET_CONTEXT_DMA_NOTIFIES,dma_notify),
 FIELD(NV039_SET_CONTEXT_DMA_BUFFER_IN,dma_in),FIELD(NV039_SET_CONTEXT_DMA_BUFFER_OUT,dma_out),
 FIELD(NV039_OFFSET_IN,job.transfer.offset_in),FIELD(NV039_OFFSET_OUT,job.transfer.offset_out),
 FIELD(NV039_PITCH_IN,job.transfer.pitch_in),FIELD(NV039_PITCH_OUT,job.transfer.pitch_out),
 FIELD(NV039_LINE_LENGTH_IN,job.transfer.length),FIELD(NV039_LINE_COUNT,job.transfer.rows)};
static void setup(PGRAPHM2MFCommand*c,unsigned sub)
{
 const uint32_t commands[][2]={{0,0x100},{0x180,0x200},{0x184,0x300},{0x188,0x400},
 {0x30c,0},{0x310,4},{0x314,16},{0x318,16},{0x31c,16},{0x320,2},{0x324,0x101}};
 for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++)
  assert(pgraph_m2mf_decode(c,sub,commands[i][0],commands[i][1])==PGRAPH_M2MF_CONSUMED);
}
/* Keep the dispatch boundary opaque as it is when the owner advances a job
 * using a RAM mapping supplied by the backend, not a fixed local array. */
static __attribute__((noinline)) void advance(PGRAPHM2MFCommand *c,
                                             uint64_t token, uint8_t *ram,
                                             size_t budget)
{
 assert(pgraph_m2mf_job_cpu_step(&c->job,token,ram,ram,budget)!=PGRAPH_M2MF_INVALID);
}
int main(void)
{
 unsigned cases=0;
 const uint32_t values[]={0,1,255,256,0x80000000,UINT32_MAX};
 for(unsigned sub=0;sub<8;sub++)
 for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)
 for(unsigned v=0;v<sizeof(values)/sizeof(values[0]);v++) {
  PGRAPHM2MFCommand c={0};assert(pgraph_m2mf_decode(&c,sub,fields[i].method,values[v])==PGRAPH_M2MF_CONSUMED);
  uint32_t value;memcpy(&value,(uint8_t*)&c+fields[i].offset,4);assert(value==values[v]);cases++;
 }
 for(unsigned format=0;format<65536;format++) {
  PGRAPHM2MFCommand c={0};
  assert(pgraph_m2mf_decode(&c,0,NV039_FORMAT,0xabcd0000|format)==PGRAPH_M2MF_CONSUMED);
  assert(c.format==(0xabcd0000|format)&&c.job.transfer.stride_in==(format&255)&&c.job.transfer.stride_out==(format>>8));cases++;
 }
 for(unsigned sub=0;sub<8;sub++)
 for(unsigned budget=1;budget<=32;budget++) {
  PGRAPHM2MFCommand c={0};setup(&c,sub);
  assert(c.object_instance==0x100&&c.dma_notify==0x200&&c.dma_in==0x300&&c.dma_out==0x400);
  assert(pgraph_m2mf_decode(&c,sub,0x328,0)==PGRAPH_M2MF_START);
  /* A retry before mapping/start must not issue another launch. */
  assert(pgraph_m2mf_decode(&c,sub,0x328,0)==PGRAPH_M2MF_RETRY);
  uint64_t token=pgraph_m2mf_job_start(&c.job,&c.job.transfer,128,128);assert(token);
  uint8_t ram[128],expected[128];for(unsigned i=0;i<128;i++)ram[i]=expected[i]=i*37;
  memmove(expected+4,expected,16);memmove(expected+20,expected+16,16);
  assert(pgraph_m2mf_job_prepared(&c.job,token,false));
  while(c.job.phase==PGRAPH_M2MF_CPU_COPY) {
   PGRAPHM2MFCommand before=c;
   for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)assert(pgraph_m2mf_decode(&c,sub,fields[i].method,UINT32_MAX)==PGRAPH_M2MF_RETRY);
   assert(pgraph_m2mf_decode(&c,sub,0x328,0)==PGRAPH_M2MF_RETRY);
   assert(!memcmp(&before,&c,sizeof(c)));
   advance(&c,token,ram,budget);
  }
  assert(!memcmp(ram,expected,128));assert(c.job.phase==PGRAPH_M2MF_PUBLISH);
  assert(pgraph_m2mf_decode(&c,sub,0x328,0)==PGRAPH_M2MF_RETRY);
  assert(pgraph_m2mf_job_published(&c.job,token));
  assert(pgraph_m2mf_decode(&c,(sub+1)%8,0x328,0)==PGRAPH_M2MF_RETRY);
  assert(pgraph_m2mf_decode(&c,sub,0x328,1)==PGRAPH_M2MF_RETRY);
  assert(pgraph_m2mf_decode(&c,sub,0x328,0)==PGRAPH_M2MF_CONSUMED);
  assert(!c.pending&&c.job.phase==PGRAPH_M2MF_IDLE);
  /* A separate identical word after consumption is a new command. */
  assert(pgraph_m2mf_decode(&c,sub,0x328,0)==PGRAPH_M2MF_START);
  pgraph_m2mf_command_reset(&c);assert(!c.pending&&!c.object_instance&&c.job.ticket>token);
  assert(!pgraph_m2mf_job_gpu_retired(&c.job,token));cases++;
 }
 for(unsigned sub=0;sub<8;sub++) {
  PGRAPHM2MFCommand gpu={0};setup(&gpu,sub);
  assert(pgraph_m2mf_decode(&gpu,sub,NV039_BUFFER_NOTIFY,1)==PGRAPH_M2MF_START);
  uint64_t token=pgraph_m2mf_job_start(&gpu.job,&gpu.job.transfer,128,128);
  assert(pgraph_m2mf_job_prepared(&gpu.job,token,true));
  assert(pgraph_m2mf_decode(&gpu,sub,NV039_BUFFER_NOTIFY,1)==PGRAPH_M2MF_RETRY);
  assert(pgraph_m2mf_job_gpu_retired(&gpu.job,token));
  assert(pgraph_m2mf_decode(&gpu,sub,NV039_BUFFER_NOTIFY,1)==PGRAPH_M2MF_RETRY);
  assert(pgraph_m2mf_job_published(&gpu.job,token));
  assert(pgraph_m2mf_decode(&gpu,sub,NV039_BUFFER_NOTIFY,1)==PGRAPH_M2MF_CONSUMED);cases++;
 }
 PGRAPHM2MFCommand c={0};setup(&c,0);
 assert(pgraph_m2mf_decode(&c,0,NV039_FORMAT,0)==PGRAPH_M2MF_CONSUMED);
 assert(pgraph_m2mf_decode(&c,0,NV039_BUFFER_NOTIFY,0)==PGRAPH_M2MF_START);
 assert(pgraph_m2mf_job_start(&c.job,&c.job.transfer,128,128));
 assert(pgraph_m2mf_decode(&c,0,NV039_BUFFER_NOTIFY,0)==PGRAPH_M2MF_FAULT);
 assert(c.pending);assert(pgraph_m2mf_command_ack(&c));assert(!c.pending);
 for(unsigned method=1;method<0x400;method++) {
  bool known=method==0x100||method==0x180||method==0x184||method==0x188||
   (method>=0x30c&&method<=0x328&&!(method&3));
  if(known)continue;
  PGRAPHM2MFCommand before=c;assert(pgraph_m2mf_decode(&c,0,method,123)==PGRAPH_M2MF_UNHANDLED);assert(!memcmp(&c,&before,sizeof(c)));cases++;
 }
 assert(pgraph_m2mf_decode(&c,8,0,123)==PGRAPH_M2MF_UNHANDLED);
 printf("PASS: %u M2MF decode/ownership schedules; class remains unconnected\n",cases+2);
 return 0;
}
