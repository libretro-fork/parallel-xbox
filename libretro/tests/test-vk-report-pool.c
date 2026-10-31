/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "qemu/queue.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#ifdef _WIN32
#include <windows.h>
#include <signal.h>
#endif
/* QUIET ERRORS */
static size_t allocations,frees,live;
static void *g_malloc(size_t size){void *p=malloc(size);assert(p);allocations++;live++;return p;}
#define g_malloc_n(n,s) g_malloc((n)*(s))
static void g_free(void *p){if(p){assert(live);frees++;live--;free(p);}}
#include "hw/xbox/nv2a/pgraph/pool.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
#define XEMU_COST_SEGS 1024
static unsigned ctz32(uint32_t value){return __builtin_ctz(value);}
#define NV2A_VK_DGROUP_BEGIN(...) ((void)0)
#define NV2A_VK_DGROUP_END() ((void)0)
#define NV2A_VK_DPRINTF(...) ((void)0)
/* REPORT TYPE */
typedef struct {
 VkDevice device;VkQueryPool query_pool,cost_stats_pool,cost_occl_pool;
 int max_queries_in_flight,num_queries_in_flight,queries_read,queries_submitted,cost_pending_slot;
 bool new_query_needed,query_in_flight,query_in_render_pass,in_command_buffer;
 struct {bool pipelineStatisticsQuery;} enabled_physical_device_features;
 uint32_t zpass_pixel_count_result;
 uint64_t query_results[2*PGRAPH_VK_MAX_QUERIES];
 QSIMPLEQ_HEAD(,QueryReport) report_queue;
 ObjPool report_pool;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;unsigned surface_scale_factor;bool reports_pending;} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
static PGRAPHVkState state;
static unsigned created,destroyed,ready,writes,expected_writes;
static uint32_t expected_parameters[2048];
static void pgraph_write_zpass_pixel_cnt_report(NV2AState *d,uint32_t parameter,uint32_t value){(void)d;assert(writes<expected_writes&&parameter==expected_parameters[writes]&&value==0);writes++;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateQueryPool(VkDevice device,const VkQueryPoolCreateInfo *info,const VkAllocationCallbacks *allocator,VkQueryPool *out){assert(device==state.device&&!allocator);if(!created)assert(info->queryType==VK_QUERY_TYPE_OCCLUSION&&info->queryCount==PGRAPH_VK_MAX_QUERIES);*out=(VkQueryPool)(uintptr_t)++created;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroyQueryPool(VkDevice device,VkQueryPool pool,const VkAllocationCallbacks *allocator){assert(device==state.device&&pool&&!allocator);destroyed++;}
VKAPI_ATTR VkResult VKAPI_CALL vkGetQueryPoolResults(VkDevice device,VkQueryPool pool,uint32_t first,uint32_t count,size_t size,void *data,VkDeviceSize stride,VkQueryResultFlags flags){assert(device==state.device&&pool==state.query_pool&&first==0&&count==1&&size==16&&stride==16&&data==state.query_results);assert(flags==(VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));uint64_t *result=data;result[0]=123;result[1]=ready;return ready?VK_SUCCESS:VK_NOT_READY;}
void pgraph_vk_process_pending_reports_internal(NV2AState *d);
/* PRODUCTION */
static void queue_batch(NV2AState *d,unsigned n){
 writes=expected_writes=0;state.in_command_buffer=true;state.num_queries_in_flight=1;state.queries_submitted=1;state.queries_read=0;
 for(unsigned i=0;i<n;i++){if(i%3==0)pgraph_vk_clear_report_value(d);else{uint32_t parameter=0x01000000u+i*16;expected_parameters[expected_writes++]=parameter;pgraph_vk_get_report(d,parameter);}assert(state.new_query_needed);}
 unsigned i=0;QueryReport *report;QSIMPLEQ_FOREACH(report,&state.report_queue,entry){assert(report->clear==(i%3==0)&&report->query_count==1);assert(report->parameter==(report->clear?0:0x01000000u+i*16));i++;}assert(i==n);
}
int main(void){
 quiet_test_errors();unsigned requests=0;
 for(unsigned mode=0;mode<2;mode++){
  memset(&state,0,sizeof(state));state.device=(VkDevice)(uintptr_t)7;state.enabled_physical_device_features.pipelineStatisticsQuery=mode;created=destroyed=0;size_t start=allocations;
  NV2AState d={.pgraph={.vk_renderer_state=&state,.surface_scale_factor=1}};pgraph_vk_init_reports(&d.pgraph);assert(allocations==start+2&&live==2&&state.report_pool.nfree==PGRAPH_VK_MAX_QUERIES&&created==(mode?3u:1u));
  size_t baseline=allocations,baseline_frees=frees;
  for(unsigned round=0;round<64;round++){
   unsigned n=round%2?PGRAPH_VK_MAX_QUERIES:37;queue_batch(&d,n);assert(state.report_pool.nfree==PGRAPH_VK_MAX_QUERIES-n&&allocations==baseline);ready=0;pgraph_vk_process_pending_reports_internal(&d);assert(!writes&&state.report_pool.nfree==PGRAPH_VK_MAX_QUERIES-n&&allocations==baseline&&frees==baseline_frees);
   ready=1;pgraph_vk_process_pending_reports_internal(&d);assert(writes==expected_writes&&QSIMPLEQ_EMPTY(&state.report_queue)&&state.report_pool.nfree==PGRAPH_VK_MAX_QUERIES&&allocations==baseline&&frees==baseline_frees);requests+=n;
  }
  queue_batch(&d,PGRAPH_VK_MAX_QUERIES+37);assert(allocations==baseline+37&&!state.report_pool.nfree&&live==39);ready=1;pgraph_vk_process_pending_reports_internal(&d);assert(writes==expected_writes&&state.report_pool.nfree==PGRAPH_VK_MAX_QUERIES&&live==2&&frees==baseline_frees+37);requests+=PGRAPH_VK_MAX_QUERIES+37;
  /* Finalize with both reserve and overflow nodes still queued. */
  queue_batch(&d,PGRAPH_VK_MAX_QUERIES+37);pgraph_vk_finalize_reports(&d.pgraph);assert(!live&&QSIMPLEQ_EMPTY(&state.report_queue)&&!state.report_pool.block&&!state.report_pool.free&&!state.report_pool.nfree&&destroyed==created);requests+=PGRAPH_VK_MAX_QUERIES+37;
  /* Reinitialization must rebuild the pool rather than retain stale nodes. */
  created=destroyed=0;pgraph_vk_init_reports(&d.pgraph);assert(live==2&&state.report_pool.nfree==PGRAPH_VK_MAX_QUERIES);queue_batch(&d,19);ready=1;pgraph_vk_process_pending_reports_internal(&d);pgraph_vk_finalize_reports(&d.pgraph);assert(!live&&created==destroyed);requests+=19;
 }
 printf("Vulkan report pool: %u clear/get requests, allocation-free reserve reuse, FIFO, overflow retirement, pending teardown and reinit passed\n",requests);
 return 0;
}
