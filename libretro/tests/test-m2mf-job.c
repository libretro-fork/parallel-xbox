/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "hw/xbox/nv2a/pgraph/m2mf-job.h"
_Static_assert(sizeof(PGRAPHM2MFJob)<=64,"keep owner transfer state compact");
static const PGRAPHM2MFTransfer layout={.offset_in=0,.offset_out=4,.pitch_in=16,.pitch_out=16,.length=16,.rows=3,.stride_in=1,.stride_out=1};
static void init_ram(uint8_t *p){for(unsigned i=0;i<128;i++)p[i]=i*37;}
static void reference(uint8_t *p){for(unsigned i=0;i<3;i++)memmove(p+4+i*16,p+i*16,16);}
int main(void)
{
 unsigned cases=0;
 for(unsigned budget=1;budget<=64;budget++)
 for(unsigned delay=0;delay<8;delay++) {
  PGRAPHM2MFJob job={0};uint8_t ram[128],expected[128];init_ram(ram);memcpy(expected,ram,128);reference(expected);
  uint64_t token=pgraph_m2mf_job_start(&job,&layout,128,128);assert(token);
  assert(job.phase==PGRAPH_M2MF_PREPARE);
  for(unsigned i=0;i<delay;i++) {
   PGRAPHM2MFJob before=job;
   assert(!pgraph_m2mf_job_start(&job,&layout,128,128));
   assert(!pgraph_m2mf_job_gpu_retired(&job,token));
   assert(!pgraph_m2mf_job_published(&job,token));
   assert(pgraph_m2mf_job_cpu_step(&job,token,ram,ram,budget)==PGRAPH_M2MF_INVALID);
   assert(!memcmp(&job,&before,sizeof(job)));
  }
  assert(pgraph_m2mf_job_prepared(&job,token,false));
  while(job.phase==PGRAPH_M2MF_CPU_COPY) {
   assert(!pgraph_m2mf_job_published(&job,token));
   assert(pgraph_m2mf_job_cpu_step(&job,token,ram,ram,0)==PGRAPH_M2MF_YIELD);
   PGRAPHM2MFJob before=job;assert(!pgraph_m2mf_job_start(&job,&layout,128,128));assert(!memcmp(&job,&before,sizeof(job)));
   assert(pgraph_m2mf_job_cpu_step(&job,token,ram,ram,budget)!=PGRAPH_M2MF_INVALID);
  }
  assert(job.phase==PGRAPH_M2MF_PUBLISH&&!memcmp(ram,expected,128));
  assert(!pgraph_m2mf_job_start(&job,&layout,128,128));
  assert(pgraph_m2mf_job_published(&job,token));
  assert(!pgraph_m2mf_job_published(&job,token));
  assert(!pgraph_m2mf_job_active(&job));cases++;
 }
 for(unsigned delay=0;delay<64;delay++) {
  PGRAPHM2MFJob job={0};uint64_t token=pgraph_m2mf_job_start(&job,&layout,128,128);
  assert(pgraph_m2mf_job_prepared(&job,token,true));
  for(unsigned i=0;i<delay;i++) {
   assert(job.phase==PGRAPH_M2MF_GPU_WAIT);
   assert(!pgraph_m2mf_job_published(&job,token));
   assert(!pgraph_m2mf_job_start(&job,&layout,128,128));
   assert(!pgraph_m2mf_job_gpu_retired(&job,token+1));
  }
  /* Controlled backend retirement; this is not a host GPU fence test. */
  assert(pgraph_m2mf_job_gpu_retired(&job,token));
  assert(!pgraph_m2mf_job_gpu_retired(&job,token));
  assert(pgraph_m2mf_job_published(&job,token));cases++;
 }
 for(unsigned phase=PGRAPH_M2MF_IDLE;phase<=PGRAPH_M2MF_FAILED;phase++) {
  PGRAPHM2MFJob job={.ticket=17,.phase=phase,.transfer=layout};
  pgraph_m2mf_job_reset(&job);assert(job.phase==PGRAPH_M2MF_IDLE&&job.ticket==18);
  uint64_t token=pgraph_m2mf_job_start(&job,&layout,128,128);assert(token==19);
  assert(!pgraph_m2mf_job_prepared(&job,17,true));
  assert(!pgraph_m2mf_job_gpu_retired(&job,17));
  assert(!pgraph_m2mf_job_published(&job,17));
  assert(pgraph_m2mf_job_cpu_step(&job,17,NULL,NULL,99)==PGRAPH_M2MF_INVALID);
  assert(pgraph_m2mf_job_prepared(&job,token,true));
  assert(!pgraph_m2mf_job_gpu_retired(&job,17));
  assert(pgraph_m2mf_job_gpu_retired(&job,token));assert(pgraph_m2mf_job_published(&job,token));cases++;
 }
 PGRAPHM2MFJob job={0};PGRAPHM2MFTransfer bad=layout;bad.stride_in=0;
 uint64_t token=pgraph_m2mf_job_start(&job,&bad,128,128);
 assert(token&&job.phase==PGRAPH_M2MF_FAILED&&!pgraph_m2mf_job_active(&job));
 assert(!pgraph_m2mf_job_published(&job,token));
 PGRAPHM2MFTransfer empty=layout;empty.length=0;
 token=pgraph_m2mf_job_start(&job,&empty,0,0);assert(token&&job.phase==PGRAPH_M2MF_PUBLISH);
 assert(pgraph_m2mf_job_published(&job,token));
 printf("PASS: %u M2MF lifecycle schedules; invalid/empty transfers; %zu-byte owner state\n",cases+2,sizeof(job));
 return 0;
}
