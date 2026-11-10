/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int VkResult;
#define VK_SUCCESS 0
#define VK_NOT_READY 1
#define VK_CHECK(s) assert((s)==VK_SUCCESS)
#define VK_FINISH_REASON_FLIP_STALL 7
typedef struct {unsigned slot;bool slot_fence_pending[2];unsigned slot_fence[2],slot_submit[2],settled_submit;int device;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;unsigned surface_scale_factor;bool host_flip_pending;} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
typedef struct {struct {bool (*flip_stall_ready)(NV2AState*);}ops;} PGRAPHRenderer;
static PGRAPHRenderer renderer;
static const PGRAPHRenderer *pgraph_renderer(PGRAPHState *pg){(void)pg;return &renderer;}
static unsigned polls,retired,finishes,resolves,deadlines,services,saves;
static bool done[2],submit,rotate,cost;
static int pgraph_vk_cost_ops;
static VkResult vkGetFenceStatus(int device,unsigned fence){assert(device==9&&fence<2);polls++;return done[fence]?VK_SUCCESS:VK_NOT_READY;}
static void pgraph_vk_slot_retired(PGRAPHState *pg,unsigned slot){
 PGRAPHVkState *r=pg->vk_renderer_state;
 assert(!r->slot_fence_pending[slot]&&(done[slot]||(int32_t)(r->slot_submit[slot]-r->settled_submit)<=0));
 if((int32_t)(r->slot_submit[slot]-r->settled_submit)>0)r->settled_submit=r->slot_submit[slot];
 retired++;
}
static void pgraph_vk_finish(PGRAPHState *pg,int reason){
 assert(reason==7);PGRAPHVkState *r=pg->vk_renderer_state;finishes++;
 if(submit){assert(!r->slot_fence_pending[r->slot]);r->slot_fence_pending[r->slot]=true;done[r->slot]=false;if(rotate)r->slot^=1;}
 if(cost){for(unsigned s=0;s<2;s++){done[s]=true;r->slot_fence_pending[s]=false;}}
}
static void pgraph_vk_settle_poll(PGRAPHState *pg){PGRAPHVkState*r=pg->vk_renderer_state;for(unsigned s=0;s<2;s++)if(r->slot_fence_pending[s]&&vkGetFenceStatus(r->device,r->slot_fence[s])==VK_SUCCESS){r->slot_fence_pending[s]=false;pgraph_vk_slot_retired(pg,s);}}
static void pgraph_vk_eager_frame_update(PGRAPHState *pg){(void)pg;services++;}
static void pgraph_vk_cpu_ahead_refresh(NV2AState *d){(void)d;services++;}
static void xemu_cost_resolve_frame(NV2AState*d,int*ops,unsigned scale){assert(ops==&pgraph_vk_cost_ops&&scale==d->pgraph.surface_scale_factor);resolves++;}
static void pgraph_vk_debug_frame_terminator(void){services++;}
static void pgraph_vk_seed_service(PGRAPHState *pg){assert(pg && pg->vk_renderer_state);services++;}
static void pgraph_vk_libpump_step(PGRAPHState*pg){(void)pg;services++;}
static void pgraph_vk_save_pipeline_cache(PGRAPHState*pg,bool wait){(void)pg;assert(!wait);saves++;}
static void xemu_cost_flip_deadline(NV2AState*d){assert(!d->pgraph.host_flip_pending);deadlines++;}
/* PRODUCTION */
int main(void){
 unsigned cases=0;
 renderer.ops.flip_stall_ready=pgraph_vk_flip_stall_ready;
 for(unsigned mode=0;mode<128;mode++)for(unsigned delay=0;delay<17;delay++){
  PGRAPHVkState r={.slot=mode&1,.slot_fence={0,1},.slot_submit={1,2},.device=9};NV2AState d={.pgraph={.vk_renderer_state=&r,.surface_scale_factor=1+mode%4}};
  polls=retired=finishes=resolves=deadlines=services=0;
  submit=mode&2;rotate=mode&4;cost=mode&8;
  r.slot_fence_pending[r.slot]=!submit&&(mode&16);
  r.slot_fence_pending[r.slot^1]=mode&32;
  done[0]=done[1]=false;
  pgraph_vk_flip_stall(&d);
  assert(finishes==1&&resolves==1&&services==5);
  assert(d.pgraph.host_flip_pending==r.slot_fence_pending[r.slot]);
  /* Starting a flip performs only the existing settle probes, no extra probe. */
  assert(polls==(cost?0:(unsigned)(!submit&&(mode&16)!=0)+(unsigned)((mode&32)!=0)+(unsigned)submit));
  bool blocked=d.pgraph.host_flip_pending;
  unsigned other=r.slot^1;bool other_pending=r.slot_fence_pending[other];
  for(unsigned n=0;n<delay;n++){
   unsigned before=polls,old_services=services;
   assert(!pgraph_process_pending_flip(&d));
   assert(polls==before+(unsigned)blocked&&services==old_services);
   assert(r.slot_fence_pending[other]==other_pending&&!deadlines);
   assert(d.pgraph.host_flip_pending==blocked);
  }
  done[r.slot]=true;
  /* A lifecycle flush can settle the slot before the continuation runs. */
  if(mode&64)r.slot_fence_pending[r.slot]=false;
  unsigned before=polls;
  assert(pgraph_process_pending_flip(&d)==blocked);
  assert(polls==before+(unsigned)(blocked&&!(mode&64))&&!d.pgraph.host_flip_pending);
  assert(deadlines==(unsigned)blocked&&finishes==1&&resolves==1&&services==5);
  assert(r.slot_fence_pending[other]==other_pending);
  before=polls;assert(!pgraph_process_pending_flip(&d)&&polls==before);
  cases++;
 }
 assert(saves==cases/600);
 unsigned proof_cases=0;
 for(unsigned slot=0;slot<2;slot++)for(unsigned wrap=0;wrap<2;wrap++)
 for(int delta=-64;delta<=64;delta++)for(unsigned ready=0;ready<2;ready++){
  uint32_t serial=wrap?0:100;
  PGRAPHVkState r={.slot=slot,.slot_fence={0,1},.device=9,.settled_submit=serial+(uint32_t)delta};
  r.slot_submit[slot]=serial;r.slot_fence_pending[slot]=true;r.slot_fence_pending[slot^1]=true;
  NV2AState d={.pgraph={.vk_renderer_state=&r}};done[0]=done[1]=false;done[slot]=ready;
  polls=retired=0;bool complete=delta>=0||ready;
  assert(pgraph_vk_flip_stall_ready(&d)==complete);
  assert(polls==(unsigned)(delta<0)&&retired==(unsigned)complete);
  assert(r.slot_fence_pending[slot]==!complete&&r.slot_fence_pending[slot^1]);
  if(complete){unsigned before=polls;assert(pgraph_vk_flip_stall_ready(&d)&&polls==before&&retired==1);}
  proof_cases++;
 }
 printf("PASS: %u flip completion-proof schedules, rollover, busy/ready fences and exactly-once retirement\n",proof_cases);
 printf("PASS: %u Vulkan flip slot/rotation/cost/empty/retirement schedules\n",cases);
}
