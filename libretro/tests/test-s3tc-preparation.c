/* SPDX-License-Identifier: GPL-2.0-or-later */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <setjmp.h>
#include "hw/xbox/nv2a/pgraph/spv/utility.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"S3TC preparation line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define assert CHECK
#define VK_CHECK(x) CHECK((x)==VK_SUCCESS)
#define PUMP_RING 64
#define PGRAPH_VK_AUX_SLOTS 8
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct {unsigned refs;} ShaderModuleInfo;
typedef struct {uint8_t bytes[4096];VkVertexInputBindingDescription binding_descriptions[16];VkVertexInputAttributeDescription attribute_descriptions[16];} PipelineKey;
typedef struct {uint8_t bytes[64];} GplPreKey;
typedef struct {uint8_t bytes[64];} GplFragKey;
/* JOB */
typedef struct {
 VkDevice device;VkPipelineCache vk_pipeline_cache;
 struct {bool enabled;uint8_t pending_mask;VkDescriptorSetLayout set_layout;VkPipelineLayout layout;VkPipeline pipelines[2];
  struct S3TCDecodeSlot{VkDescriptorPool pool;VkImageView views[96];unsigned used;}slots[8];}s3tc_decode;
} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static LibJob pump_jobs[PUMP_RING];static unsigned pump_w_local,pump_d,cursor,starts,links,graphics_lands,waits;
static atomic_int pump_w,pump_r,pump_cancel;static bool pipe_started,pgraph_vk_spirv_1_6;
static int pipe_ec_done,pipe_ec_work;static jmp_buf step;static bool worker_active,context_alive,probe_during_build,uncovered_active;
static PGRAPHState *active_pg;static unsigned native_calls,created[6],destroyed[6];
static struct{unsigned kind,parent,version;bool live;}objects[16384];static unsigned next_object=100;
enum{DS=1,PL,SM,PIPE};
static unsigned object_new(unsigned kind,unsigned parent,unsigned version){CHECK(context_alive&&next_object<ARRAY_SIZE(objects));unsigned n=++next_object;objects[n].kind=kind;objects[n].parent=parent;objects[n].version=version;objects[n].live=true;created[kind]++;native_calls++;return n;}
static void object_drop(unsigned n,unsigned kind){CHECK(context_alive&&n<ARRAY_SIZE(objects)&&objects[n].live&&objects[n].kind==kind);objects[n].live=false;destroyed[kind]++;native_calls++;}
static VkResult vkCreateDescriptorSetLayout(VkDevice d,const VkDescriptorSetLayoutCreateInfo *i,const VkAllocationCallbacks *a,VkDescriptorSetLayout *out){
 CHECK((worker_active||uncovered_active)&&d==(VkDevice)(uintptr_t)7&&!a&&i->sType==VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO&&i->bindingCount==2);
 CHECK(i->pBindings[0].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER&&i->pBindings[1].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
 *out=(VkDescriptorSetLayout)(uintptr_t)object_new(DS,0,0);return VK_SUCCESS;
}
static VkResult vkCreatePipelineLayout(VkDevice d,const VkPipelineLayoutCreateInfo *i,const VkAllocationCallbacks *a,VkPipelineLayout *out){
 unsigned ds=(unsigned)(uintptr_t)i->pSetLayouts[0];CHECK((worker_active||uncovered_active)&&d==(VkDevice)(uintptr_t)7&&!a&&objects[ds].live&&objects[ds].kind==DS);
 CHECK(i->setLayoutCount==1&&i->pushConstantRangeCount==1&&i->pPushConstantRanges[0].size==36);
 *out=(VkPipelineLayout)(uintptr_t)object_new(PL,ds,0);return VK_SUCCESS;
}
static VkResult vkCreateShaderModule(VkDevice d,const VkShaderModuleCreateInfo *i,const VkAllocationCallbacks *a,VkShaderModule *out){
 CHECK((worker_active||uncovered_active)&&d==(VkDevice)(uintptr_t)7&&!a);bool found=false;
 for(unsigned v=0;v<2;v++)for(unsigned dim=0;dim<2;dim++){SpvStaticModule m=pgraph_spv_utility(dim?SPV_UTILITY_S3TC_VOLUME:SPV_UTILITY_S3TC_ARRAY,v);if(m.words==i->pCode&&m.num_words*4==i->codeSize){found=true;*out=(VkShaderModule)(uintptr_t)object_new(SM,dim,v);}}
 CHECK(found);return VK_SUCCESS;
}
static VkResult vkCreateComputePipelines(VkDevice d,VkPipelineCache cache,uint32_t count,const VkComputePipelineCreateInfo *i,const VkAllocationCallbacks *a,VkPipeline *out){
 CHECK((worker_active||uncovered_active)&&d==(VkDevice)(uintptr_t)7&&cache==(VkPipelineCache)(uintptr_t)8&&count==1&&!a);
 unsigned layout=(unsigned)(uintptr_t)i->layout,module=(unsigned)(uintptr_t)i->stage.module;
 CHECK(objects[layout].live&&objects[layout].kind==PL&&objects[module].live&&objects[module].kind==SM&&i->stage.stage==VK_SHADER_STAGE_COMPUTE_BIT&&!strcmp(i->stage.pName,"main"));
 if(probe_during_build){probe_during_build=false;unsigned w=pump_w_local,calls=native_calls;CHECK(!pgraph_vk_s3tc_pipeline_ready(active_pg,0));CHECK(!pgraph_vk_s3tc_pipeline_ready(active_pg,1));CHECK(pump_w_local==w&&native_calls==calls&&!active_pg->vk_renderer_state->s3tc_decode.layout);}
 *out=(VkPipeline)(uintptr_t)object_new(PIPE,layout,objects[module].version);return VK_SUCCESS;
}
static void vkDestroyShaderModule(VkDevice d,VkShaderModule m,const VkAllocationCallbacks *a){CHECK((worker_active||uncovered_active)&&d==(VkDevice)(uintptr_t)7&&!a);object_drop((unsigned)(uintptr_t)m,SM);}
static void vkDestroyPipeline(VkDevice d,VkPipeline p,const VkAllocationCallbacks *a){CHECK(d==(VkDevice)(uintptr_t)7&&!a);object_drop((unsigned)(uintptr_t)p,PIPE);}
static void vkDestroyPipelineLayout(VkDevice d,VkPipelineLayout p,const VkAllocationCallbacks *a){CHECK(d==(VkDevice)(uintptr_t)7&&!a);object_drop((unsigned)(uintptr_t)p,PL);}
static void vkDestroyDescriptorSetLayout(VkDevice d,VkDescriptorSetLayout p,const VkAllocationCallbacks *a){CHECK(d==(VkDevice)(uintptr_t)7&&!a);object_drop((unsigned)(uintptr_t)p,DS);}
static void vkDestroyDescriptorPool(VkDevice d,VkDescriptorPool p,const VkAllocationCallbacks *a){(void)d;(void)p;(void)a;CHECK(false);}
static void vkDestroyImageView(VkDevice d,VkImageView p,const VkAllocationCallbacks *a){(void)d;(void)p;(void)a;CHECK(false);}
static VkResult vkCreateGraphicsPipelines(VkDevice d,VkPipelineCache c,uint32_t n,const VkGraphicsPipelineCreateInfo *i,const VkAllocationCallbacks *a,VkPipeline *out){(void)i;CHECK((worker_active||uncovered_active)&&d==(VkDevice)(uintptr_t)7&&c==(VkPipelineCache)(uintptr_t)8&&n==1&&!a);*out=(VkPipeline)(uintptr_t)object_new(PIPE,0,0);return VK_SUCCESS;}
/* BUILDER */
static int retro_atomic_load_acquire_int(atomic_int *p){return atomic_load_explicit(p,memory_order_acquire);}
static void retro_atomic_store_release_int(atomic_int *p,int v){atomic_store_explicit(p,v,memory_order_release);}
static int retro_eventcount_prepare_wait(int *p){(void)p;return 1;}
static void retro_eventcount_cancel_wait(int *p){(void)p;}
static void worker_step(void);
static void retro_eventcount_commit_wait(int *p,int key){CHECK(key==1&&p==&pipe_ec_done&&!worker_active);waits++;worker_step();}
static void retro_eventcount_notify(int *p){if(p==&pipe_ec_done){CHECK(worker_active);cursor=(unsigned)retro_atomic_load_acquire_int(&pump_r);longjmp(step,1);}CHECK(p==&pipe_ec_work);}
static void pipe_start(void){if(!pipe_started){starts++;pipe_started=true;}}
static void lib_job_link(LibJob *j){CHECK(!j->compute);links++;}
static void pipeline_land(PGRAPHState *pg,LibJob *j){CHECK(!j->compute&&pg);vkDestroyPipeline(pg->vk_renderer_state->device,j->result,NULL);j->result=VK_NULL_HANDLE;graphics_lands++;}
static void library_land(PGRAPHVkState *r,LibJob *j){CHECK(!j->compute);vkDestroyPipeline(r->device,j->result,NULL);j->result=VK_NULL_HANDLE;graphics_lands++;}
static void pgraph_vk_unref_shader_module(PGRAPHVkState *r,ShaderModuleInfo *m){(void)r;(void)m;CHECK(false);}
static void pgraph_vk_wait_for_aux(PGRAPHState *pg){CHECK(pg&&!pg->vk_renderer_state->s3tc_decode.pending_mask&&(unsigned)retro_atomic_load_acquire_int(&pump_r)==pump_w_local);}
static void qemu_thread_run_atexit(void) {}
/* FUNCTIONS */
static void worker_step(void){CHECK(!worker_active);if(!setjmp(step)){worker_active=true;pipe_worker_fn(NULL);CHECK(false);}worker_active=false;}
static void context_start(PGRAPHVkState *r,PGRAPHState *pg){memset(r,0,sizeof(*r));r->device=(VkDevice)(uintptr_t)7;r->vk_pipeline_cache=(VkPipelineCache)(uintptr_t)8;r->s3tc_decode.enabled=true;pg->vk_renderer_state=r;active_pg=pg;context_alive=true;}
static void context_end(PGRAPHState *pg){s3tc_decode_finalize(pg);CHECK(pump_d==pump_w_local);for(unsigned k=DS;k<=PIPE;k++)CHECK(created[k]==destroyed[k]);context_alive=false;}
int main(void){
 PGRAPHVkState r;PGRAPHState pg;context_start(&r,&pg);
 CHECK(!pgraph_vk_s3tc_pipeline_ready(&pg,0)&&!pump_w_local&&!starts&&!native_calls); /* no optional thread creation */
 /* No graphics worker: retain the old synchronous native path, once per
  * used variant. The fixture explicitly distinguishes it from worker calls. */
 uncovered_active=true;CHECK(pgraph_vk_s3tc_pipeline_uncovered(&pg,0));
 unsigned legacy_calls=native_calls;CHECK(pgraph_vk_s3tc_pipeline_uncovered(&pg,0));
 CHECK(native_calls==legacy_calls&&!starts&&!pump_w_local);
 CHECK(pgraph_vk_s3tc_pipeline_uncovered(&pg,1));uncovered_active=false;
 context_end(&pg);context_start(&r,&pg);
 pipe_started=true;CHECK(!pgraph_vk_s3tc_pipeline_uncovered(&pg,0));unsigned disabled_calls=native_calls;r.s3tc_decode.enabled=false;CHECK(!pgraph_vk_s3tc_pipeline_ready(&pg,0)&&!pump_w_local&&native_calls==disabled_calls);r.s3tc_decode.enabled=true;
 /* Full queue: even completed unrelated jobs cannot be drained under a lend. */
 for(unsigned i=0;i<PUMP_RING;i++){LibJob *j=pump_slot();CHECK(j);j->device=r.device;j->cache=r.vk_pipeline_cache;pump_submit(j);}
 unsigned calls=native_calls;CHECK(!pgraph_vk_s3tc_pipeline_ready(&pg,0)&&!r.s3tc_decode.pending_mask&&native_calls==calls);
 for(unsigned i=0;i<PUMP_RING;i++)worker_step();CHECK(!pgraph_vk_s3tc_pipeline_ready(&pg,0)&&!graphics_lands&&pump_d==0);pump_drain(&pg);CHECK(graphics_lands==PUMP_RING);
 context_end(&pg);
 for(unsigned generation=0;generation<128;generation++){
  context_start(&r,&pg);if(generation==64){CHECK(pump_d==pump_w_local);cursor=pump_d=pump_w_local=UINT32_MAX-2;atomic_store(&pump_w,(int)cursor);atomic_store(&pump_r,(int)cursor);}
  pgraph_vk_spirv_1_6=false;unsigned w=pump_w_local,n=native_calls;CHECK(!pgraph_vk_s3tc_pipeline_ready(&pg,0)&&pump_w_local==w+1&&native_calls==n&&r.s3tc_decode.pending_mask==1);
  for(unsigned i=0;i<100;i++){CHECK(!pgraph_vk_s3tc_pipeline_ready(&pg,0));CHECK(!pgraph_vk_s3tc_pipeline_ready(&pg,1));}CHECK(pump_w_local==w+1&&native_calls==n);
  if(generation&1){/* Cancellation before start owns no new native handles. */
   context_end(&pg);CHECK(native_calls==n&&!r.s3tc_decode.pending_mask);continue;
  }
  pgraph_vk_spirv_1_6=true;probe_during_build=true;worker_step();CHECK(!r.s3tc_decode.layout&&r.s3tc_decode.pending_mask==1);CHECK(pgraph_vk_s3tc_pipeline_ready(&pg,0));CHECK(!r.s3tc_decode.pending_mask&&r.s3tc_decode.layout&&objects[(unsigned)(uintptr_t)r.s3tc_decode.pipelines[0]].version==0);
  unsigned layout=(unsigned)(uintptr_t)r.s3tc_decode.layout,set=(unsigned)(uintptr_t)r.s3tc_decode.set_layout;
  CHECK(!pgraph_vk_s3tc_pipeline_ready(&pg,1));CHECK(r.s3tc_decode.pending_mask==2);
  if(generation&2){/* Cancel a second variant while its job borrows the adopted layout. */
   n=native_calls;CHECK(objects[layout].live&&objects[set].live);context_end(&pg);
   CHECK(native_calls==n+3&&!objects[layout].live&&!objects[set].live);continue;
  }
  pgraph_vk_spirv_1_6=false;worker_step();CHECK(pgraph_vk_s3tc_pipeline_ready(&pg,1));CHECK((unsigned)(uintptr_t)r.s3tc_decode.layout==layout&&(unsigned)(uintptr_t)r.s3tc_decode.set_layout==set&&objects[(unsigned)(uintptr_t)r.s3tc_decode.pipelines[1]].version==1);
  n=native_calls;for(unsigned i=0;i<1000;i++)CHECK(pgraph_vk_s3tc_pipeline_ready(&pg,i&1));CHECK(native_calls==n);
  context_end(&pg);
 }
 CHECK(!starts&&created[DS]==65&&created[PL]==65&&created[SM]==98&&created[PIPE]==162);
 puts("S3TC preparation: 128 generations; first-use/full-queue fallback, exact captured target, shared layout ownership, partial publication, first-owner and borrowed-layout cancellation, unrelated lends, rollover, stable ready hits and teardown PASS");return 0;
}
