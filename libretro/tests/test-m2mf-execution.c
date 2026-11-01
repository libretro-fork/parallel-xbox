/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <retro_atomic.h>
#include "hw/xbox/nv2a/pgraph/m2mf-execution.h"
#define MiB (1024*1024)
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef uint64_t hwaddr;
/* CPU TYPES */
typedef struct {PGRAPHCpuAccess cpu;} PGRAPHState;
typedef struct {hwaddr bytes;} MemoryRegion;
typedef struct NV2AState {MemoryRegion *vram;uint8_t *vram_ptr;PGRAPHState pgraph;}NV2AState;
typedef struct {uint8_t *data;hwaddr address,bytes;unsigned dma_class,dma_target;}NVDMAView;
static NVDMAView maps[3];
_Alignas(64) static uint8_t first[16384],second[16384],expected[16384];
static uint8_t dirty[262144];
_Alignas(64) static uint8_t big[262144];
static unsigned map_calls,dirty_calls,clock_calls,status_calls;
static NV2AState *current;
static uint64_t memory_region_size(MemoryRegion*m){return m->bytes;}
static bool nv_dma_map_checked(NV2AState*d,hwaddr instance,NVDMAView*view)
{
 unsigned i=instance==0x100?0:instance==0x200?1:instance==0x300||!instance?2:3;
 map_calls++;if(i==3)return false;*view=maps[i];view->data=d->vram_ptr+view->address;return true;
}
static void memory_region_set_dirty(MemoryRegion*m,hwaddr address,hwaddr bytes)
{assert(address<=m->bytes&&bytes<=m->bytes-address);memset(dirty+address,1,bytes);dirty_calls++;}
static uint64_t ptimer_snapshot(NV2AState*d){(void)d;clock_calls++;return UINT64_C(0x123456789abcdef0);}
static void stl_le_p(void*p,uint32_t value){uint8_t*b=p;for(unsigned i=0;i<4;i++)b[i]=value>>(8*i);}
static uint32_t get(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void status_store(retro_atomic_int_t*p,int value)
{
 assert((uint8_t*)p==current->vram_ptr+8192+0x1c&&value==0);
 assert(get(current->vram_ptr+8192+0x10)==0x9abcdef0);
 assert(get(current->vram_ptr+8192+0x14)==0x12345678);
 for(unsigned y=0;y<4;y++)assert(!memcmp(current->vram_ptr+4096+4+y*32,expected+4096+4+y*32,16));
 for(unsigned i=0;i<16;i++)assert(dirty[8192+0x10+i]);
 status_calls++;retro_atomic_store_release_int(p,value);
}
/* OWNER ACCESS */
#undef retro_atomic_store_release_int
#define retro_atomic_store_release_int status_store
/* COORDINATOR */
#undef retro_atomic_store_release_int
static void setup(NV2AState*d,PGRAPHM2MFCommand*c)
{
 memset(&d->pgraph,0,sizeof(d->pgraph));
 d->pgraph.cpu.dl_addr=123;d->pgraph.cpu.dl_len=456;
 retro_atomic_store_relaxed_int(&d->pgraph.cpu.dl_pending,1);
 for(unsigned i=0;i<sizeof(first);i++)first[i]=expected[i]=(uint8_t)(i*37+3);
 memset(dirty,0,sizeof(dirty));d->vram_ptr=first;
 maps[0]=(NVDMAView){.address=0,.bytes=128,.dma_class=2};
 maps[1]=(NVDMAView){.address=4096,.bytes=128,.dma_class=3};
 maps[2]=(NVDMAView){.address=8192,.bytes=32,.dma_class=3};
 *c=(PGRAPHM2MFCommand){.dma_in=0x100,.dma_out=0x200,.dma_notify=0x300};
 c->job.transfer=(PGRAPHM2MFTransfer){.offset_out=4,.pitch_in=32,.pitch_out=32,.length=16,.rows=4,.stride_in=1,.stride_out=1};
 assert(pgraph_m2mf_decode(c,0,NV039_BUFFER_NOTIFY,0)==PGRAPH_M2MF_START);
 for(unsigned y=0;y<4;y++)memmove(expected+4096+4+y*32,expected+y*32,16);
 map_calls=dirty_calls=clock_calls=status_calls=0;current=d;
}
int main(void)
{
 static NV2AState d;MemoryRegion vr={16384};d.vram=&vr;unsigned cases=0;
 for(unsigned budget=1;budget<=64;budget++) {
  PGRAPHM2MFCommand c;PGRAPHM2MFExecution e;setup(&d,&c);
  /* Explicit notifier binding at instance zero must not be dropped. */
  if(budget&1)c.dma_notify=0;
  assert(pgraph_m2mf_execution_start(&d,&c,&e,true)==PGRAPH_M2MF_YIELD&&map_calls==3);
  uint64_t token=e.ticket;
  assert(pgraph_m2mf_execution_publish(&d,&c,&e)==PGRAPH_M2MF_INVALID);
  assert(pgraph_m2mf_job_prepared(&c.job,token,false));
  retro_atomic_store_relaxed_int(&d.pgraph.cpu.ahead[0],1);
  assert(pgraph_m2mf_execution_cpu_step(&d,&c,&e,budget)==PGRAPH_M2MF_YIELD&&!c.job.transfer.column&&!dirty_calls);
  retro_atomic_store_relaxed_int(&d.pgraph.cpu.ahead[0],2);
  assert(pgraph_m2mf_execution_cpu_step(&d,&c,&e,budget)==PGRAPH_M2MF_YIELD&&!c.job.transfer.column&&!dirty_calls);
  assert(d.pgraph.cpu.dl_addr==123&&d.pgraph.cpu.dl_len==456&&retro_atomic_load_relaxed_int(&d.pgraph.cpu.dl_pending)==1);
  retro_atomic_store_relaxed_int(&d.pgraph.cpu.ahead[0],0);
  /* Descriptor edits after launch must not retarget the pending DMA. */
  maps[0].address=12288;maps[1].address=12352;maps[2].address=12480;
  while(c.job.phase==PGRAPH_M2MF_CPU_COPY) {
   uint64_t before=(uint64_t)c.job.transfer.row*16+c.job.transfer.column;
   pgraph_m2mf_execution_cpu_step(&d,&c,&e,budget);
   uint64_t after=(uint64_t)c.job.transfer.row*16+c.job.transfer.column;assert(after>before&&after-before<=budget);
   uint8_t *other=d.vram_ptr==first?second:first;memcpy(other,d.vram_ptr,16384);d.vram_ptr=other;
  }
  for(unsigned y=0;y<4;y++)for(unsigned x=0;x<16;x++)assert(dirty[4096+4+y*32+x]);
  assert(map_calls==3&&c.job.phase==PGRAPH_M2MF_PUBLISH);
  retro_atomic_store_relaxed_int(&d.pgraph.cpu.ahead[0],4);
  assert(pgraph_m2mf_execution_publish(&d,&c,&e)==PGRAPH_M2MF_YIELD&&!clock_calls&&!status_calls);
  retro_atomic_store_relaxed_int(&d.pgraph.cpu.ahead[0],0);
  assert(pgraph_m2mf_execution_publish(&d,&c,&e)==PGRAPH_M2MF_DONE&&clock_calls==1&&status_calls==1);
  assert(pgraph_m2mf_execution_publish(&d,&c,&e)==PGRAPH_M2MF_INVALID&&status_calls==1);
  assert(pgraph_m2mf_decode(&c,0,NV039_BUFFER_NOTIFY,0)==PGRAPH_M2MF_CONSUMED);cases++;
 }
 for(unsigned si=1;si<=4;si++)for(unsigned so=1;so<=4;so++)for(unsigned budget=1;budget<=17;budget++) {
  PGRAPHM2MFCommand c;PGRAPHM2MFExecution e;setup(&d,&c);
  maps[0].bytes=maps[1].bytes=320;maps[1].address=0;
  c.job.transfer.pitch_in=c.job.transfer.pitch_out=64;
  c.job.transfer.stride_in=si;c.job.transfer.stride_out=so;
  memcpy(expected,first,16384);
  for(unsigned y=0;y<4;y++) {
   if(si==1&&so==1)memmove(expected+4+y*64,expected+y*64,16);
   else for(unsigned x=0;x<16;x++)expected[4+y*64+x*so]=expected[y*64+x*si];
  }
  assert(pgraph_m2mf_execution_start(&d,&c,&e,false)==PGRAPH_M2MF_YIELD);
  assert(pgraph_m2mf_job_prepared(&c.job,e.ticket,false));
  while(c.job.phase==PGRAPH_M2MF_CPU_COPY)assert(pgraph_m2mf_execution_cpu_step(&d,&c,&e,budget)!=PGRAPH_M2MF_INVALID);
  assert(!memcmp(d.vram_ptr,expected,16384));
  for(unsigned y=0;y<4;y++)for(unsigned x=0;x<16;x++)assert(dirty[4+y*64+x*so]);
  assert(pgraph_m2mf_execution_publish(&d,&c,&e)==PGRAPH_M2MF_DONE&&!clock_calls&&!status_calls);cases++;
 }
 PGRAPHM2MFCommand c;PGRAPHM2MFExecution e;setup(&d,&c);maps[2].bytes=31;
 assert(pgraph_m2mf_execution_start(&d,&c,&e,true)==PGRAPH_M2MF_INVALID&&c.job.phase==PGRAPH_M2MF_FAILED&&!dirty_calls);
 setup(&d,&c);maps[2].address++;
 assert(pgraph_m2mf_execution_start(&d,&c,&e,true)==PGRAPH_M2MF_INVALID&&!dirty_calls);
 setup(&d,&c);c.job.transfer.length=0;
 assert(pgraph_m2mf_execution_start(&d,&c,&e,false)==PGRAPH_M2MF_DONE&&!map_calls);
 assert(pgraph_m2mf_execution_publish(&d,&c,&e)==PGRAPH_M2MF_DONE&&!clock_calls&&!status_calls);cases+=3;
 setup(&d,&c);maps[0].dma_target=1;
 assert(pgraph_m2mf_execution_start(&d,&c,&e,false)==PGRAPH_M2MF_YIELD);
 assert(pgraph_m2mf_job_prepared(&c.job,e.ticket,false));
 assert(pgraph_m2mf_execution_cpu_step(&d,&c,&e,16)==PGRAPH_M2MF_INVALID&&!dirty_calls);
 setup(&d,&c);maps[0].dma_class=3;
 assert(pgraph_m2mf_execution_start(&d,&c,&e,false)==PGRAPH_M2MF_YIELD);
 assert(pgraph_m2mf_job_prepared(&c.job,e.ticket,false));
 assert(pgraph_m2mf_execution_cpu_step(&d,&c,&e,16)==PGRAPH_M2MF_INVALID&&!dirty_calls);
 setup(&d,&c);c.job.transfer.length=1;c.job.transfer.rows=UINT32_MAX;c.job.transfer.pitch_in=c.job.transfer.pitch_out=0;
 assert(pgraph_m2mf_execution_start(&d,&c,&e,false)==PGRAPH_M2MF_YIELD);
 assert(pgraph_m2mf_job_prepared(&c.job,e.ticket,false));
 assert(pgraph_m2mf_execution_cpu_step(&d,&c,&e,SIZE_MAX)==PGRAPH_M2MF_YIELD);
 assert(c.job.transfer.row==256&&c.job.transfer.column==0&&dirty_calls==256);
 pgraph_m2mf_command_reset(&c);
 assert(pgraph_m2mf_execution_cpu_step(&d,&c,&e,1)==PGRAPH_M2MF_INVALID);cases+=3;
 setup(&d,&c);vr.bytes=sizeof(big);d.vram_ptr=big;
 memset(big,0,sizeof(big));for(unsigned i=0;i<70000;i++)big[i]=(uint8_t)(i*37+1);
 maps[0].bytes=70000;maps[1].address=131072;maps[1].bytes=70000;
 c.job.transfer=(PGRAPHM2MFTransfer){.length=70000,.rows=1,.stride_in=1,.stride_out=1};
 assert(pgraph_m2mf_execution_start(&d,&c,&e,false)==PGRAPH_M2MF_YIELD);
 assert(pgraph_m2mf_job_prepared(&c.job,e.ticket,false));
 assert(pgraph_m2mf_execution_cpu_step(&d,&c,&e,SIZE_MAX)==PGRAPH_M2MF_YIELD);
 assert(c.job.transfer.column==PGRAPH_M2MF_CPU_MAX_BYTES&&dirty_calls==1);
 assert(!memcmp(big,big+131072,PGRAPH_M2MF_CPU_MAX_BYTES));
 assert(!big[131072+PGRAPH_M2MF_CPU_MAX_BYTES]);
 assert(pgraph_m2mf_execution_cpu_step(&d,&c,&e,SIZE_MAX)==PGRAPH_M2MF_DONE);
 assert(!memcmp(big,big+131072,70000));cases++;
 vr.bytes=16384;setup(&d,&c);
 assert(pgraph_m2mf_execution_start(&d,&c,&e,true)==PGRAPH_M2MF_YIELD);
 assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_BLOCKED&&!dirty_calls&&!clock_calls);
 assert(pgraph_m2mf_job_prepared(&c.job,e.ticket,false));
 retro_atomic_store_relaxed_int(&d.pgraph.cpu.ahead[0],1);
 assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_BLOCKED&&!dirty_calls);
 retro_atomic_store_relaxed_int(&d.pgraph.cpu.ahead[0],0);
 assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_FINISHED&&clock_calls==1&&status_calls==1);
 unsigned previous_dirty=dirty_calls;
 assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_FINISHED&&clock_calls==1&&dirty_calls==previous_dirty);
 assert(pgraph_m2mf_decode(&c,0,NV039_BUFFER_NOTIFY,0)==PGRAPH_M2MF_CONSUMED);
 assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_ERROR);cases++;
 setup(&d,&c);
 assert(pgraph_m2mf_execution_start(&d,&c,&e,true)==PGRAPH_M2MF_YIELD);
 assert(pgraph_m2mf_job_prepared(&c.job,e.ticket,true));
 for(unsigned i=0;i<100;i++)assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_BLOCKED&&!dirty_calls&&!clock_calls);
 /* Controlled backend completes the data before its retirement event. */
 for(unsigned y=0;y<4;y++)memcpy(d.vram_ptr+4096+4+y*32,expected+4096+4+y*32,16);
 assert(pgraph_m2mf_job_gpu_retired(&c.job,e.ticket));
 assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_FINISHED&&status_calls==1);cases++;
 setup(&d,&c);vr.bytes=sizeof(big);d.vram_ptr=big;
 maps[0].bytes=70000;maps[1].address=131072;maps[1].bytes=70000;
 c.job.transfer=(PGRAPHM2MFTransfer){.length=70000,.rows=1,.stride_in=1,.stride_out=1};
 assert(pgraph_m2mf_execution_start(&d,&c,&e,false)==PGRAPH_M2MF_YIELD);
 assert(pgraph_m2mf_job_prepared(&c.job,e.ticket,false));
 assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_RUNNABLE&&c.job.transfer.column==65536);
 retro_atomic_store_relaxed_int(&d.pgraph.cpu.ahead[0],1<<16);
 assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_BLOCKED&&c.job.transfer.column==65536);
 retro_atomic_store_relaxed_int(&d.pgraph.cpu.ahead[0],0);
 assert(pgraph_m2mf_execution_progress(&d,&c,&e)==PGRAPH_M2MF_FINISHED&&!clock_calls&&!status_calls);cases++;
 printf("PASS: %u M2MF coordinator schedules (latched DMA, bounded copies, visibility, dirty tracking and status publication)\n",cases);
 return 0;
}
