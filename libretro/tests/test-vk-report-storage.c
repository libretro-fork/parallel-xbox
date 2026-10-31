/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vulkan/vulkan.h>
#include "qemu/queue.h"
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
/* QUIET ERRORS */
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
#define NV2A_VK_DPRINTF(...) ((void)0)
/* REPORT TYPE */
typedef struct {
 VkDevice device;VkQueryPool query_pool;
 int max_queries_in_flight,num_queries_in_flight,queries_read,queries_submitted;
 bool query_in_flight;
 uint64_t waited_query_result[2];int waited_query_slot;bool waited_query_valid;
 uint32_t zpass_pixel_count_result;
 uint64_t guard_before;
 /* RESULT STORAGE */
 uint64_t guard_after;
 int report_pool;
 QSIMPLEQ_HEAD(, QueryReport) report_queue;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;unsigned surface_scale_factor;bool reports_pending;} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
static PGRAPHVkState state;
static uint64_t query_values[PGRAPH_VK_MAX_QUERIES];
static unsigned ready,query_calls,retired,writes,wait_calls;
static bool wait_error;
static uint32_t params[32],values[32];
static void obj_pool_give(int *pool,void *p){assert(pool==&state.report_pool&&p);retired++;}
static void pgraph_write_zpass_pixel_cnt_report(NV2AState *d,uint32_t parameter,uint32_t value){(void)d;assert(writes<32);params[writes]=parameter;values[writes++]=value;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice device,VkQueryPool pool,uint32_t first,uint32_t count,size_t bytes,void *data,VkDeviceSize stride,VkQueryResultFlags flags){
 assert(device==state.device&&pool==state.query_pool);
 if(flags==(VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT)){
  assert(count==1&&first<(unsigned)state.queries_submitted&&bytes==8&&stride==8&&data==state.waited_query_result);wait_calls++;
  if(wait_error)return VK_ERROR_DEVICE_LOST;
  *(uint64_t*)data=query_values[first];if(ready<first+1)ready=first+1;return VK_SUCCESS;
 }
 assert(first==(unsigned)state.queries_read&&count==(unsigned)(state.queries_submitted-state.queries_read));
 assert(data==state.query_results&&bytes==count*16&&stride==16);
 assert(flags==(VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
 assert(count&&count<=PGRAPH_VK_MAX_QUERIES);query_calls++;
 uint64_t *out=data;bool all=true;
 for(unsigned i=0;i<count;i++){unsigned slot=first+i;bool available=slot<ready||(slot>ready&&(slot&1));out[2*i+1]=available;if(available)out[2*i]=query_values[slot];else all=false;}
 return all?VK_SUCCESS:VK_NOT_READY;
}
/* PRODUCTION */
static uint32_t random_state=0x917452ab;
static uint32_t random_word(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static bool checking;
static unsigned allocations;
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void*,size_t);
void *__wrap_malloc(size_t n){if(checking)allocations++;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s){if(checking)allocations++;return __real_calloc(n,s);}
void *__wrap_realloc(void *p,size_t n){if(checking)allocations++;return __real_realloc(p,n);}
static void setup_wait(NV2AState *d,QueryReport *report,unsigned count){
 memset(&state,0,sizeof(state));state.device=(VkDevice)(uintptr_t)3;state.query_pool=(VkQueryPool)(uintptr_t)7;state.max_queries_in_flight=PGRAPH_VK_MAX_QUERIES;state.num_queries_in_flight=state.queries_submitted=count;state.guard_before=state.guard_after=0xc91ab76e583e91ffull;QSIMPLEQ_INIT(&state.report_queue);
 *d=(NV2AState){.pgraph={.vk_renderer_state=&state,.surface_scale_factor=1}};
 *report=(QueryReport){.clear=false,.parameter=0x1234,.query_count=count};QSIMPLEQ_INSERT_TAIL(&state.report_queue,report,entry);
 ready=query_calls=retired=writes=wait_calls=0;wait_error=false;
}
static void waited_result_tests(void){
 NV2AState d;QueryReport report;
 for(unsigned i=0;i<10000;i++){
  setup_wait(&d,&report,1);query_values[0]=((uint64_t)i<<32)|(i+17);
  checking=true;assert(pgraph_vk_wait_pending_report(&d));pgraph_vk_process_pending_reports_internal(&d);checking=false;
  assert(wait_calls==1&&!query_calls&&writes==1&&values[0]==i+17&&params[0]==0x1234&&retired==1&&!state.waited_query_valid&&!state.num_queries_in_flight&&!allocations);
 }
 /* A wait on the last slot proves preceding slots complete, but its value
  * is not their aggregate: multi-slot reads must retain ordinary polling. */
 setup_wait(&d,&report,3);query_values[0]=1;query_values[1]=2;query_values[2]=3;assert(pgraph_vk_wait_pending_report(&d));pgraph_vk_process_pending_reports_internal(&d);assert(wait_calls==1&&query_calls==1&&writes==1&&values[0]==6&&!state.waited_query_valid);
 /* New submissions after a successful wait invalidate one-slot eligibility. */
 setup_wait(&d,&report,1);query_values[0]=10;query_values[1]=20;assert(pgraph_vk_wait_pending_report(&d));state.num_queries_in_flight=state.queries_submitted=report.query_count=2;ready=2;pgraph_vk_process_pending_reports_internal(&d);assert(query_calls==1&&writes==1&&values[0]==30&&!state.waited_query_valid);
 /* A cached value from another slot must not be consumed. */
 setup_wait(&d,&report,2);state.queries_read=1;state.zpass_pixel_count_result=100;state.waited_query_valid=true;state.waited_query_slot=0;state.waited_query_result[0]=999;state.waited_query_result[1]=1;query_values[1]=7;ready=2;pgraph_vk_process_pending_reports_internal(&d);assert(query_calls==1&&writes==1&&values[0]==107&&!state.waited_query_valid);
 /* No submitted slot, an already-counted report and an empty queue do not wait. */
 setup_wait(&d,&report,1);state.queries_submitted=0;assert(!pgraph_vk_wait_pending_report(&d)&&!wait_calls);state.queries_read=1;assert(pgraph_vk_wait_pending_report(&d)&&!wait_calls);QSIMPLEQ_REMOVE_HEAD(&state.report_queue,entry);assert(pgraph_vk_wait_pending_report(&d)&&!wait_calls);
 /* Driver errors never publish stale cached data. Normal polling remains. */
 setup_wait(&d,&report,1);state.waited_query_valid=true;state.waited_query_result[0]=999;state.waited_query_result[1]=1;wait_error=true;assert(pgraph_vk_wait_pending_report(&d)&&!state.waited_query_valid);pgraph_vk_process_pending_reports_internal(&d);assert(!writes&&query_calls==1);ready=1;query_values[0]=42;pgraph_vk_process_pending_reports_internal(&d);assert(writes==1&&values[0]==42);
 puts("Waited query reuse: 10000 duplicate poll calls eliminated; multi-slot, new-submission, wrong-slot, no-work, error and pool-reset guards passed");
}
int main(void){
 quiet_test_errors();unsigned polls=0;
 for(unsigned trial=0;trial<3000;trial++){
  unsigned n=trial%4==0?PGRAPH_VK_MAX_QUERIES:trial%4==1?0:trial%4==2?1:1+random_word()%PGRAPH_VK_MAX_QUERIES;
  memset(&state,0,sizeof(state));state.device=(VkDevice)(uintptr_t)3;state.query_pool=(VkQueryPool)(uintptr_t)7;state.max_queries_in_flight=PGRAPH_VK_MAX_QUERIES;state.num_queries_in_flight=n;state.queries_submitted=n/2;state.query_in_flight=true;state.guard_before=state.guard_after=0xc91ab76e583e91ffull;memset(state.query_results,0xa7,sizeof(state.query_results));QSIMPLEQ_INIT(&state.report_queue);
  NV2AState d={.pgraph={.vk_renderer_state=&state,.surface_scale_factor=1+trial%9,.reports_pending=true}};
  for(unsigned i=0;i<n;i++)query_values[i]=((uint64_t)random_word()<<32)|random_word();
  QueryReport reports[20];for(unsigned i=0;i<20;i++){reports[i].clear=i%4==0;reports[i].parameter=0x100+i*16;reports[i].query_count=i*n/19;QSIMPLEQ_INSERT_TAIL(&state.report_queue,&reports[i],entry);}
  unsigned model_read=0,model_head=0,model_writes=0,model_retired=0;uint32_t model_sum=0,model_params[32],model_values[32];retired=writes=query_calls=0;
  const unsigned progress[]={0,0,1,n/3,n/3,n/2,n?n-1:0,n,n};
  for(unsigned step=0;step<ARRAY_SIZE(progress);step++){
   ready=progress[step];if(step==3)state.queries_submitted=n;if(step==7)state.query_in_flight=false;
   unsigned limit=ready<(unsigned)state.queries_submitted?ready:(unsigned)state.queries_submitted;
   if(limit<model_read)limit=model_read;
   while(model_head<20&&reports[model_head].query_count<=limit){QueryReport *r=&reports[model_head++];while(model_read<r->query_count)model_sum+=(uint32_t)query_values[model_read++];if(r->clear)model_sum=0;else{model_params[model_writes]=r->parameter;model_values[model_writes++]=model_sum/(d.pgraph.surface_scale_factor*d.pgraph.surface_scale_factor);}model_retired++;}
   if(model_head==20)while(model_read<limit)model_sum+=(uint32_t)query_values[model_read++];
   bool reset=model_head==20&&!state.query_in_flight&&model_read==n;
   checking=true;pgraph_vk_process_pending_reports_internal(&d);checking=false;polls++;
   assert(!allocations&&state.guard_before==0xc91ab76e583e91ffull&&state.guard_after==0xc91ab76e583e91ffull);
   assert(state.zpass_pixel_count_result==model_sum&&retired==model_retired&&writes==model_writes);
   assert(!memcmp(params,model_params,writes*4)&&!memcmp(values,model_values,writes*4));
   assert(d.pgraph.reports_pending==(model_head!=20));
   if(reset){assert(!state.queries_read&&!state.queries_submitted&&!state.num_queries_in_flight);break;}
   assert((unsigned)state.queries_read==model_read);
  }
  assert(QSIMPLEQ_EMPTY(&state.report_queue)&&retired==20);
  unsigned old_calls=query_calls;checking=true;pgraph_vk_process_pending_reports_internal(&d);checking=false;assert(query_calls==old_calls&&!allocations);polls++;
 }
 waited_result_tests();
 printf("Query result storage: %u polls, 3000 clear/report windows, delayed/out-of-order availability, scales 1-9, 1024-slot limits, wraparound and direct-pointer guards passed; zero polling allocations\n",polls);
 return 0;
}
