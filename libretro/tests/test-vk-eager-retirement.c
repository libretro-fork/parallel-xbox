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
typedef struct {unsigned slot,eager_draw_count;bool in_command_buffer,slot_fence_pending[2];unsigned slot_main[2],slot_sync[2],slot_fence[2],slot_submit[2],settled_submit,submit_count;int device,reuse_wait_us;struct{unsigned w_end[2];}frame_gpu;}PGRAPHVkState;
typedef struct {PGRAPHVkState*vk_renderer_state;}PGRAPHState;
static unsigned polls,waits,retirements,resolves,submits,clock_now,drops;
static void lr_gpu_ram_drop(uintptr_t cmd){(void)cmd;drops++;}
static int status[2];static bool drain_expected;
static int vkGetFenceStatus(int device,unsigned fence){assert(device==9&&fence<2);polls++;return status[fence];}
static int vkWaitForFences(int device,unsigned count,unsigned*fence,unsigned all,uint64_t timeout){assert(device==9&&count==1&&*fence<2&&all==1&&timeout==UINT64_MAX);waits++;status[*fence]=0;return 0;}
static int64_t g_get_monotonic_time(void){return ++clock_now;}
static void ema16(int *p,int64_t value){assert(value>0);*p+=(int)value;}
static void trash_drain_retired(PGRAPHVkState*r){assert(drain_expected);retirements++;(void)r;}
static void frame_gpu_resolve_until(PGRAPHVkState*r,unsigned end){assert(end==r->frame_gpu.w_end[0]||end==r->frame_gpu.w_end[1]);resolves++;}
static void pgraph_vk_finish(PGRAPHState*pg,unsigned why){
 PGRAPHVkState*r=pg->vk_renderer_state;assert(why==11&&r->in_command_buffer);
 unsigned next=r->slot^1;assert(!r->slot_fence_pending[next]);
 r->slot_fence_pending[r->slot]=true;r->slot_submit[r->slot]=++r->submit_count;
 r->slot=next;r->in_command_buffer=false;submits++;
}
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
 waits=0;drain_expected=true;pgraph_vk_reclaim_command_buffer(&pg,true);assert(waits==1&&!r.slot_fence_pending[0]);
 printf("PASS: %u eager submit/retirement/slot-reuse schedules; completed slots never wait\n",cases);
}
