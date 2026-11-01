/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
/* QUIET ERRORS */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define VK_SUCCESS 0
#define VK_NULL_HANDLE 0
#define XEMU_LIBRETRO_VK_FRAMES 8
#define VK_TRUE 1
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
typedef unsigned VkFence;
typedef struct PGRAPHVkDisplayState {
 struct PGRAPHVkFrameSync { VkFence fence; unsigned drawn,shown; bool pending; uint64_t serial; } sync[8];
 unsigned frame_pool,shown_pending;
 uint64_t settled;
} PGRAPHVkDisplayState;
typedef struct { unsigned device; PGRAPHVkDisplayState display; } PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;} PGRAPHState;
static PGRAPHVkState state;
static uint64_t completed,submitted,previous_probe;
static unsigned probes,waits,resets,destroyed_fences,destroyed_semaphores,destroyed_pools;
static unsigned fence_mask,sem_mask;
static int vkGetFenceStatus(unsigned device,VkFence fence) {
 assert(device==7 && fence && fence<=8);
 struct PGRAPHVkFrameSync *s=&state.display.sync[fence-1];
 assert(s->pending && s->serial>state.display.settled && s->serial<previous_probe);
 previous_probe=s->serial;probes++;
 return s->serial<=completed?0:1;
}
static int vkWaitForFences(unsigned device,unsigned count,const VkFence *fences,unsigned all,uint64_t timeout) {
 assert(device==7 && count==1 && all && timeout==UINT64_MAX);
 struct PGRAPHVkFrameSync *s=&state.display.sync[*fences-1];
 assert(s->pending && s->serial>state.display.settled);
 completed=MAX(completed,s->serial);waits++;return 0;
}
static int vkResetFences(unsigned device,unsigned count,const VkFence *fences) {
 assert(device==7 && count==1);
 struct PGRAPHVkFrameSync *s=&state.display.sync[*fences-1];
 assert(s->pending && s->serial<=completed);resets++;return 0;
}
static void vkDestroyFence(unsigned device,VkFence fence,const void*allocator){
 assert(device==7&&!allocator&&fence&&fence<=8);unsigned i=fence-1;
 assert(!state.display.sync[i].pending&&state.display.sync[i].serial<=completed&&!(fence_mask&(1u<<i)));
 fence_mask|=1u<<i;destroyed_fences++;
}
static void vkDestroySemaphore(unsigned device,unsigned sem,const void*allocator){
 assert(device==7&&!allocator);unsigned i=sem>=200?sem-200:sem-100;unsigned bit=i+(sem>=200?8:0);
 assert(i<8&&!state.display.sync[i].pending&&state.display.sync[i].serial<=completed&&!(sem_mask&(1u<<bit)));
 sem_mask|=1u<<bit;destroyed_semaphores++;
}
static void vkDestroyCommandPool(unsigned device,unsigned pool,const void*allocator){
 assert(device==7&&pool==9&&!allocator&&destroyed_fences==8);destroyed_pools++;
}
/* PRODUCTION */
static void poll(void) {
 uint64_t expected=state.display.settled;
 for(unsigned i=0;i<8;i++) {
  struct PGRAPHVkFrameSync *s=&state.display.sync[i];
  if(s->pending && s->serial<=completed)expected=MAX(expected,s->serial);
 }
 unsigned old=probes;previous_probe=UINT64_MAX;
 pgraph_vk_poll_display_retired(&state);
 assert(probes-old<=8 && state.display.settled==expected);
}
static void reuse(unsigned index) {
 struct PGRAPHVkFrameSync *s=&state.display.sync[index];
 unsigned old_waits=waits,old_resets=resets;bool pending=s->pending;
 bool need_wait=pending && s->serial>state.display.settled;
 wait_frame_sync(&state,s);
 assert(waits==old_waits+need_wait && resets==old_resets+pending && !s->pending);
 s->serial=++submitted;s->pending=true;
}
int main(void) {
 quiet_test_errors();state.device=7;
 for(unsigned i=0;i<8;i++) {state.display.sync[i].fence=i+1;reuse(i);}
 completed=submitted;poll();assert(probes==1 && state.display.settled==8);
 for(unsigned i=0;i<8;i++)reuse(i);
 assert(!waits && resets==8);
 /* Every completion frontier, independently of the slots' serial order. */
 for(unsigned round=0;round<20000;round++) {
  if(rand()%3==0)completed+= (submitted-completed)?rand()%(submitted-completed+1):0;
  poll();
  unsigned slot=rand()%8;reuse(slot);
  assert(state.display.settled<=completed && completed<=submitted);
 }
 unsigned drain_cases=0;
 for(unsigned separate=0;separate<2;separate++)for(unsigned mask=0;mask<256;mask++)
 for(unsigned rotation=0;rotation<8;rotation++)for(unsigned known=0;known<=8;known++){
  memset(&state,0,sizeof(state));state.device=7;state.display.frame_pool=9;state.display.shown_pending=33;
  state.display.settled=completed=known;probes=waits=resets=destroyed_fences=destroyed_semaphores=destroyed_pools=fence_mask=sem_mask=0;
  unsigned newest=known,pending=0;
  for(unsigned i=0;i<8;i++){
   struct PGRAPHVkFrameSync *q=&state.display.sync[i];q->fence=i+1;
   if(separate){q->drawn=100+i;q->shown=200+i;}
   q->pending=(mask&(1u<<i))!=0;
   if(q->pending){q->serial=1+(i+rotation)%8;newest=MAX(newest,q->serial);pending++;}
  }
  PGRAPHState pg={&state};destroy_frame_sync(&pg);
  assert(!probes&&waits==(unsigned)(newest>known)&&resets==pending);
  assert(state.display.settled==newest&&completed==newest&&destroyed_fences==8&&destroyed_semaphores==separate*16&&destroyed_pools==1);
  assert(!state.display.frame_pool&&!state.display.shown_pending);
  for(unsigned i=0;i<8;i++)assert(!state.display.sync[i].fence&&!state.display.sync[i].pending);
  destroy_frame_sync(&pg);assert(destroyed_fences==8&&destroyed_pools==1&&waits==(unsigned)(newest>known));drain_cases++;
 }
 printf("Display teardown: %u pending-mask/slot-order/known-completion/semaphore schedules, at most one wait\n",drain_cases);
 printf("Display retirement: 20000 delayed/reordered-slot workloads passed; bounded status polling and safe slot reset\n");
 return 0;
}
