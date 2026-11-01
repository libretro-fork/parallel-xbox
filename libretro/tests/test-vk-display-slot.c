/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MAX(a,b) ((a)>(b)?(a):(b))
#define XEMU_LIBRETRO_VK_FRAMES 8
#define VK_SUCCESS 0
#define VK_NOT_READY 1
#define VK_CHECK(x) assert((x)==VK_SUCCESS)
typedef int VkResult;
typedef unsigned VkFence;
typedef struct PGRAPHVkDisplayState {
 struct PGRAPHVkFrameSync {VkFence fence;bool pending;uint64_t serial;}sync[8];
 uint64_t settled,frame_count,published;
 unsigned next_slot;
 bool retry_pending;
 uint64_t last_surface_addr;
 int draw_time;
 unsigned last_line_offset,last_mode_width,last_mode_height,last_scale;
 bool last_interlaced;
}PGRAPHVkDisplayState;
typedef struct {unsigned device;PGRAPHVkDisplayState display;}PGRAPHVkState;
static PGRAPHVkState state;
static uint64_t completed;
static unsigned probes,resets,work;
static bool shared=true;
static VkResult vkGetFenceStatus(unsigned device,VkFence fence)
{assert(device==7&&fence>=1&&fence<=8);probes++;return state.display.sync[fence-1].serial<=completed?VK_SUCCESS:VK_NOT_READY;}
static VkResult vkResetFences(unsigned device,unsigned n,const VkFence*fences)
{assert(device==7&&n==1&&state.display.sync[*fences-1].serial<=completed);resets++;return VK_SUCCESS;}
static bool on_frontend_device(void){return shared;}
/* ACQUIRE */
static void attempt(PGRAPHVkState*r)
{
 PGRAPHVkDisplayState *disp=&r->display;
/* PREFIX */
 (void)sync;
 /* Model work after the production acquisition boundary. */
 work++;disp->next_slot=(slot+1)%8;disp->frame_count++;disp->published++;
}
typedef struct {uint64_t vram_addr;int draw_time;bool upload_pending;}SurfaceBinding;
typedef struct {unsigned surface_scale_factor;bool pvideo;}PGRAPHState;
static struct {bool enabled;}get_pvideo_state(PGRAPHState*pg){return (typeof(get_pvideo_state(pg))){pg->pvideo};}
static bool cached(PGRAPHVkDisplayState*disp,bool retry)
{
 disp->retry_pending=retry;
 SurfaceBinding sample={.vram_addr=0x1000,.draw_time=9};SurfaceBinding*surface=&sample;
 struct {unsigned line_offset;}vga_display_params={32};unsigned width=640,height=480;
 bool interlaced=false;PGRAPHState p={.surface_scale_factor=2},*pg=&p;
 return /* CACHE */;
}
int main(void)
{
 unsigned cases=0;
 for(unsigned pending=0;pending<2;pending++)
 for(unsigned done=0;done<2;done++)
 for(unsigned known=0;known<2;known++)
 for(unsigned slot=0;slot<8;slot++) {
  state=(PGRAPHVkState){.device=7};state.display.next_slot=slot;
  struct PGRAPHVkFrameSync*s=&state.display.sync[slot];*s=(typeof(*s)){.fence=slot+1,.pending=pending,.serial=41};
  completed=done||known?41:40;state.display.settled=known?41:0;
  state.display.frame_count=17;state.display.published=9;probes=resets=work=0;
  bool busy=pending&&!done&&!known;
  attempt(&state);
  assert(probes==(pending&&!known?1u:0u));
  assert(resets==(pending&&!busy?1u:0u));
  assert(work==!busy&&state.display.retry_pending==busy);
  assert(state.display.next_slot==(busy?slot:(slot+1)%8));
  assert(state.display.frame_count==(busy?17u:18u)&&state.display.published==(busy?9u:10u));
  if(busy) {
   assert(s->pending&&s->serial==41);
   /* Frame pulses may retry a busy slot without resetting it. */
   for(unsigned i=0;i<100;i++)attempt(&state);
   assert(!work&&!resets&&state.display.frame_count==17);
   completed=41;attempt(&state);assert(work==1&&resets==1&&!state.display.retry_pending&&!s->pending);
  }
  cases++;
 }
 PGRAPHVkDisplayState d={.last_surface_addr=0x1000,.draw_time=9,.last_line_offset=32,
  .last_mode_width=640,.last_mode_height=480,.last_scale=2};
 assert(cached(&d,false));assert(!cached(&d,true));
 d.last_mode_width=800;assert(!cached(&d,false));
 printf("PASS: %u Vulkan slot schedules, repeated busy probes and mode-only retry preservation\n",cases+3);
 return 0;
}
