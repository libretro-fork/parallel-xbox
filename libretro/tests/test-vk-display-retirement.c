/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#ifdef _WIN32
#include <windows.h>
#endif
/* QUIET ERRORS */
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define VK_SUCCESS 0
#define VK_TRUE 1
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
typedef unsigned VkFence;
typedef struct PGRAPHVkDisplayState {
 struct PGRAPHVkFrameSync { VkFence fence; bool pending; uint64_t serial; } sync[8];
 uint64_t settled;
} PGRAPHVkDisplayState;
typedef struct { unsigned device; PGRAPHVkDisplayState display; } PGRAPHVkState;
static PGRAPHVkState state;
static uint64_t completed,submitted,previous_probe;
static unsigned probes,waits,resets;
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
 printf("Display retirement: 20000 delayed/reordered-slot workloads passed; eight completed fences -> one status query, zero older-slot waits; unfinished slots still wait/reset\n");
 return 0;
}
