/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PGRAPH_VK_SLOTS 2
#define VK_SUCCESS 0
#define VK_NOT_READY 1
#define VK_TRUE 1
#define VK_FINISH_REASON_EAGER 11
#define VK_CHECK(s) assert((s)==VK_SUCCESS)
typedef struct {unsigned slot,eager_draw_count;bool in_command_buffer,slot_fence_pending[2];unsigned slot_main[2],slot_sync[2],slot_fence[2],slot_submit[2],settled_submit,submit_count;int device,reuse_wait_us;struct{unsigned w,w_end[2];}frame_gpu;}PGRAPHVkState;
typedef struct {PGRAPHVkState*vk_renderer_state;}PGRAPHState;
static unsigned polls,waits,retirements,resolves,submits,clock_now,drops,pool_flushes,teardowns;
static void lr_gpu_ram_drop(uintptr_t cmd){(void)cmd;drops++;}
static int status[2];static bool drain_expected,draining;static unsigned waited_fence;
static int vkGetFenceStatus(int device,unsigned fence){assert(device==9&&fence<2);polls++;return status[fence];}
static int vkWaitForFences(int device,unsigned count,unsigned*fence,unsigned all,uint64_t timeout){assert(device==9&&count==1&&*fence<2&&all==1&&timeout==UINT64_MAX);if(draining)assert(!drops&&!retirements&&!resolves);waited_fence=*fence;waits++;status[*fence]=0;return 0;}
static int64_t g_get_monotonic_time(void){return ++clock_now;}
static void ema16(int *p,int64_t value){assert(value>0);*p+=(int)value;}
static void trash_drain_retired(PGRAPHVkState*r){assert(drain_expected);retirements++;(void)r;}
static void pgraph_vk_image_pool_flush(PGRAPHVkState*r){(void)r;pool_flushes++;}
static void frame_gpu_resolve_until(PGRAPHVkState*r,unsigned end){assert(end==r->frame_gpu.w||end==r->frame_gpu.w_end[0]||end==r->frame_gpu.w_end[1]);resolves++;}
static void pgraph_vk_finish(PGRAPHState*pg,unsigned why){
 PGRAPHVkState*r=pg->vk_renderer_state;assert(why==11&&r->in_command_buffer);
 unsigned next=r->slot^1;assert(!r->slot_fence_pending[next]);
 r->slot_fence_pending[r->slot]=true;r->slot_submit[r->slot]=++r->submit_count;
 r->slot=next;r->in_command_buffer=false;submits++;
}
static void teardown_after_settle(PGRAPHState *pg){PGRAPHVkState *r=pg->vk_renderer_state;assert(!r->in_command_buffer&&!r->slot_fence_pending[0]&&!r->slot_fence_pending[1]);teardowns++;}
/* PRODUCTION */
int main(void){
 unsigned cases=0;
 for(unsigned slot=0;slot<2;slot++)for(unsigned recording=0;recording<2;recording++)
 for(unsigned pending=0;pending<2;pending++)for(unsigned gpu=0;gpu<3;gpu++)
 for(unsigned count=0;count<260;count++)for(unsigned wrap=0;wrap<2;wrap++){
  PGRAPHVkState r={.slot=slot,.device=9,.slot_fence={0,1},.eager_draw_count=count,.in_command_buffer=recording,.frame_gpu={.w_end={101,202}},.submit_count=wrap?1:100};
  PGRAPHState pg={.vk_renderer_state=&r};unsigned next=slot^1;
  r.slot_fence_pending[next]=pending;r.slot_submit[next]=r.submit_count-2;r.settled_submit=r.submit_count-4;
  status[0]=status[1]=(int)gpu;polls=waits=retirements=resolves=submits=clock_now=drops=0;
  bool attempt=recording&&count>=127;bool ready=!pending||gpu==0;drain_expected=attempt&&pending&&gpu==0;
  eager_submit_after_draw(&pg);
  assert(polls==(unsigned)(attempt&&pending));assert(!waits);
  assert(submits==(unsigned)(attempt&&ready));assert(retirements==(unsigned)drain_expected&&resolves==retirements&&drops==retirements*2);
  assert(r.eager_draw_count==(!recording?count:attempt?(ready?0:128):count+1));
  assert(r.settled_submit==(drain_expected?r.slot_submit[next]:r.submit_count-(submits?1:0)-4));
  if(submits){
   assert(r.slot==next&&!r.slot_fence_pending[next]&&r.slot_fence_pending[slot]);
   unsigned before=polls;pgraph_vk_reclaim_command_buffer(&pg,true);
   assert(!waits&&polls==before&&retirements==(unsigned)drain_expected);
  }else assert(r.slot==slot&&r.slot_fence_pending[next]==(bool)pending);
  cases++;
 }
 /* A genuinely busy direct reclaim retains its separate wait policy. */
 PGRAPHVkState r={.device=9,.slot_fence={0,1},.slot_fence_pending={true,false},.frame_gpu={.w_end={101,202}}};PGRAPHState pg={.vk_renderer_state=&r};
 r.slot_submit[0]=1;waits=0;drain_expected=true;pgraph_vk_reclaim_command_buffer(&pg,true);assert(waits==1&&!r.slot_fence_pending[0]);
 unsigned serial_cases=0;
 for(unsigned slot=0;slot<2;slot++)for(unsigned reuse=0;reuse<2;reuse++)
 for(unsigned wrap=0;wrap<2;wrap++)for(int delta=-64;delta<=64;delta++){
  PGRAPHVkState q={.device=9,.slot=slot,.slot_fence={0,1},.frame_gpu={.w_end={101,202}}};PGRAPHState p={.vk_renderer_state=&q};
  q.settled_submit=wrap?3:200;q.slot_submit[slot]=q.settled_submit+(uint32_t)delta;
  q.slot_fence_pending[slot]=true;q.slot_fence_pending[slot^1]=true;
  status[slot]=delta<=0?VK_SUCCESS:VK_NOT_READY;
  polls=waits=retirements=resolves=drops=clock_now=0;drain_expected=true;
  pgraph_vk_reclaim_command_buffer(&p,reuse);
  assert(!polls&&waits==(unsigned)(delta>0)&&retirements==1&&resolves==1&&drops==2);
  assert(!q.slot_fence_pending[slot]&&q.slot_fence_pending[slot^1]);
  assert(q.settled_submit==(delta>0?q.slot_submit[slot]:(wrap?3u:200u)));
  assert((q.reuse_wait_us!=0)==(reuse&&delta>0));
  unsigned before_waits=waits;pgraph_vk_reclaim_command_buffer(&p,reuse);
  assert(waits==before_waits&&retirements==1&&drops==2);serial_cases++;
 }
 unsigned retry_cases=0;
 for(unsigned newer=0;newer<2;newer++)for(unsigned wrap=0;wrap<2;wrap++)for(unsigned target=0;target<2;target++){
  PGRAPHVkState q={.device=9,.slot=target,.slot_fence={0,1},.slot_fence_pending={true,true},.frame_gpu={.w_end={101,202}}};PGRAPHState p={.vk_renderer_state=&q};
  uint32_t serial=wrap?0:100;q.slot_submit[newer]=serial;q.slot_submit[newer^1]=serial-1;q.settled_submit=serial-2;
  status[newer]=VK_SUCCESS;status[newer^1]=VK_NOT_READY;
  polls=waits=retirements=resolves=drops=pool_flushes=0;drain_expected=true;
  pgraph_vk_reclaim_retired_images(&q);
  assert(q.settled_submit==serial&&q.slot_fence_pending[0]&&q.slot_fence_pending[1]);
  assert(!drops&&!resolves&&pool_flushes==1&&retirements==1);
  unsigned before_polls=polls;
  pgraph_vk_reclaim_command_buffer(&p,true);
  assert(polls==before_polls&&!waits&&drops==2&&resolves==1&&retirements==2);
  assert(!q.slot_fence_pending[target]&&q.slot_fence_pending[target^1]);retry_cases++;
 }
 unsigned drain_cases=0;
 for(unsigned newer=0;newer<2;newer++)for(unsigned mask=0;mask<4;mask++)
 for(unsigned wrap=0;wrap<2;wrap++)for(int delta=-2;delta<=2;delta++){
  PGRAPHVkState q={.device=9,.slot_fence={0,1},.frame_gpu={.w=303,.w_end={101,202}}};
  uint32_t serial=wrap?0:100;q.submit_count=delta>0?serial+(uint32_t)delta:serial;q.slot_submit[newer]=serial;q.slot_submit[newer^1]=serial-1;q.settled_submit=serial+(uint32_t)delta;
  q.slot_fence_pending[0]=mask&1;q.slot_fence_pending[1]=mask&2;
  int expected=-1;
  if((mask&(1u<<newer))&&delta<0)expected=(int)newer;
  else if((mask&(1u<<(newer^1)))&&delta< -1)expected=(int)(newer^1);
  polls=waits=retirements=resolves=drops=0;draining=drain_expected=true;
  pgraph_vk_wait_all_slots(&q);draining=false;
  assert(!polls&&waits==(unsigned)(expected>=0));
  if(expected>=0)assert(waited_fence==(unsigned)expected);
  assert(drops==2*((unsigned)((mask&1)!=0)+(unsigned)((mask&2)!=0)));
  assert(!q.slot_fence_pending[0]&&!q.slot_fence_pending[1]&&q.settled_submit==q.submit_count&&retirements==1&&resolves==1);
  waits=drops=retirements=resolves=0;draining=true;pgraph_vk_wait_all_slots(&q);draining=false;
  assert(!waits&&!drops&&retirements==1&&resolves==1);drain_cases++;
 }
 unsigned settle_cases=0;
 for(unsigned newer=0;newer<2;newer++)for(unsigned mask=0;mask<4;mask++)
 for(unsigned wrap=0;wrap<2;wrap++)for(int known=-2;known<=1;known++)
 for(unsigned completed=0;completed<3;completed++)for(unsigned recording=0;recording<2;recording++){
  PGRAPHVkState q={.device=9,.slot_fence={0,1},.frame_gpu={.w_end={101,202}},.in_command_buffer=recording};PGRAPHState p={.vk_renderer_state=&q};
  uint32_t newest=wrap?0:100;q.slot_submit[newer]=newest;q.slot_submit[newer^1]=newest-1;q.settled_submit=newest+(uint32_t)known;
  q.slot_fence_pending[0]=mask&1;q.slot_fence_pending[1]=mask&2;
  status[newer]=completed==2?VK_SUCCESS:VK_NOT_READY;status[newer^1]=completed>=1?VK_SUCCESS:VK_NOT_READY;
  bool model_pending[2]={q.slot_fence_pending[0],q.slot_fence_pending[1]};uint32_t model_settled=q.settled_submit;unsigned expected_polls=0,expected_retired=0;
  for(unsigned slot=0;slot<2;slot++)if(model_pending[slot]){
   bool proven=(int32_t)(q.slot_submit[slot]-model_settled)<=0;
   if(!proven)expected_polls++;
   if(proven||status[slot]==VK_SUCCESS){model_pending[slot]=false;expected_retired++;if((int32_t)(q.slot_submit[slot]-model_settled)>0)model_settled=q.slot_submit[slot];}
  }
  polls=waits=retirements=resolves=drops=teardowns=0;drain_expected=true;
  pgraph_vk_settle_poll(&p);
  assert(polls==expected_polls&&!waits&&retirements==expected_retired&&resolves==expected_retired&&drops==2*expected_retired);
  assert(q.slot_fence_pending[0]==model_pending[0]&&q.slot_fence_pending[1]==model_pending[1]&&q.settled_submit==model_settled);
  assert(teardowns==(unsigned)(!recording&&!model_pending[0]&&!model_pending[1]));
  unsigned old_retired=retirements,old_drops=drops;polls=0;pgraph_vk_settle_poll(&p);assert(retirements==old_retired&&drops==old_drops&&!waits);
  settle_cases++;
 }
 unsigned eager_proof_cases=0;
 for(unsigned slot=0;slot<2;slot++)for(unsigned wrap=0;wrap<2;wrap++)
 for(int delta=-64;delta<=64;delta++)for(unsigned ready=0;ready<2;ready++){
  uint32_t serial=wrap?0:100;unsigned next=slot^1;
  PGRAPHVkState q={.device=9,.slot=slot,.slot_fence={0,1},.frame_gpu={.w_end={101,202}},.in_command_buffer=true,.eager_draw_count=127,.submit_count=serial+100,.settled_submit=serial+(uint32_t)delta};PGRAPHState p={.vk_renderer_state=&q};
  q.slot_fence_pending[next]=true;q.slot_submit[next]=serial;
  status[next]=ready?VK_SUCCESS:VK_NOT_READY;
  polls=waits=retirements=resolves=drops=submits=0;drain_expected=true;
  eager_submit_after_draw(&p);bool complete=delta>=0||ready;
  assert(polls==(unsigned)(delta<0)&&!waits&&submits==(unsigned)complete);
  assert(retirements==(unsigned)complete&&resolves==retirements&&drops==retirements*2);
  assert(q.slot_fence_pending[next]==!complete&&q.eager_draw_count==(complete?0u:128u));
  eager_proof_cases++;
 }
 printf("PASS: %u eager completion-proof schedules, rollover and unavailable fences\n",eager_proof_cases);
 printf("PASS: %u settle-poll schedules, completion proofs, same-queue coverage, rollover, partial retirement and recording-safe teardown\n",settle_cases);
 printf("PASS: %u eager, %u serial-proof, %u allocation-retry and %u full-drain schedules\n",cases,serial_cases,retry_cases,drain_cases);
}
