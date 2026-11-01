/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
static void actual_fence(void){retro_atomic_thread_fence_seq_cst();}
static void (*hook)(void);
static bool concurrent;
static void controlled_fence(void){actual_fence();if(concurrent)return;void(*f)(void)=hook;hook=NULL;if(f)f();}
#undef retro_atomic_thread_fence_seq_cst
#define retro_atomic_thread_fence_seq_cst() controlled_fence()
#include "hw/xbox/nv2a/pgraph/vk/frame-handoff.h"
#define XEMU_LIBRETRO_VK_FRAMES 8
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#endif
#define VK_NULL_HANDLE 0
#define VK_FORMAT_R8G8B8A8_UNORM 37
typedef uint64_t VkImage,VkImageView;
typedef unsigned VkFormat;
typedef struct PGRAPHVkDisplayState {
 struct PGRAPHVkFrameImage {VkImage image;VkImageView view;int width,height;bool ready;uint64_t retire_at;}frames[8],retired[16];
 PGRAPHVkFrameHandoff handoff;
 retro_atomic_64_t published;
 retro_atomic_int_t check_pending;
 uint64_t frame_count,submitted,settled;
 int width,height;
}PGRAPHVkDisplayState;
typedef struct {unsigned device;PGRAPHVkDisplayState display;}PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;}PGRAPHState;
typedef struct {PGRAPHState pgraph;}NV2AState;
static PGRAPHVkState state;
static NV2AState device;
static NV2AState *g_nv2a=&device;
static struct {unsigned device;}xemu_libretro_vk={7};
static unsigned kicks,created,destroyed;
static void pfifo_kick(NV2AState*d){assert(d==&device);kicks++;}
/* READER */
static void destroy_frame_image(PGRAPHVkState*r,struct PGRAPHVkFrameImage*f)
{assert(f->image&&f->retire_at<=r->display.settled);destroyed++;memset(f,0,sizeof(*f));}
static void create_frame_image(PGRAPHState*pg,struct PGRAPHVkFrameImage*f,int w,int h)
{assert(pg->vk_renderer_state==&state);*f=(typeof(*f)){.image=++created,.view=100+created,.width=w,.height=h};}
/* RETIRE */
static uint64_t count;
static VkImage image;static VkImageView view;static VkFormat format;static int width,height;
static bool read_frame(void){return nv2a_vk_libretro_get_frame(&image,&view,&format,&width,&height,&count);}
static void publish(unsigned slot,uint64_t n)
{
 state.display.frames[slot]=(struct PGRAPHVkFrameImage){.image=1000+n,.view=2000+n,.width=640,.height=480,.ready=true};
 retro_atomic_store_release_64(&state.display.published,(int64_t)((n<<8)|slot));
}
static void reader_during_write(void){assert(!read_frame());}
static void writer_during_read(void){assert(!pgraph_vk_frame_write_begin(&state.display.handoff,1));}
static void change_publication(void){publish(2,3);}
static unsigned produced,consumed;
static void producer(void *arg)
{
 (void)arg;unsigned slot=0;uint64_t n=0;
 for(unsigned i=0;i<300000;i++) {
  if(!pgraph_vk_frame_write_begin(&state.display.handoff,slot))continue;
  n++;state.display.frames[slot]=(struct PGRAPHVkFrameImage){.image=1000+n,.view=2000+n,.width=640,.height=480,.ready=true};
  retro_atomic_store_release_64(&state.display.published,(int64_t)((n<<8)|slot));
  pgraph_vk_frame_write_end(&state.display.handoff);
  slot=(slot+1)%8;produced++;
 }
}
static void consumer(void *arg)
{
 (void)arg;
 for(unsigned i=0;i<300000;i++) {
  if(!read_frame())continue;
  assert(image==1000+count&&view==2000+count&&width==640&&height==480);
  nv2a_vk_libretro_frame_presented();consumed++;
 }
}
int main(void)
{
 state.device=7;device.pgraph.vk_renderer_state=&state;unsigned cases=0;
 publish(0,1);hook=reader_during_write;
 assert(pgraph_vk_frame_write_begin(&state.display.handoff,0));
 assert(!count&&!retro_atomic_load_relaxed_int(&state.display.handoff.reading));
 assert(!pgraph_vk_frame_write_begin(&state.display.handoff,2));
 assert(retro_atomic_load_relaxed_int(&state.display.handoff.writing)==1);
 pgraph_vk_frame_write_end(&state.display.handoff);
 assert(read_frame()&&count==1&&image==1001);
 nv2a_vk_libretro_frame_presented();assert(retro_atomic_load_relaxed_int(&state.display.handoff.front)==1);
 publish(1,2);hook=writer_during_read;
 assert(read_frame()&&count==2&&image==1002);
 assert(!pgraph_vk_frame_write_begin(&state.display.handoff,0));
 assert(!pgraph_vk_frame_write_begin(&state.display.handoff,1));
 nv2a_vk_libretro_frame_presented();
 assert(pgraph_vk_frame_write_begin(&state.display.handoff,0));pgraph_vk_frame_write_end(&state.display.handoff);
 assert(!pgraph_vk_frame_write_begin(&state.display.handoff,1));cases+=2;
 unsigned old_kicks=kicks;assert(!read_frame()&&kicks==old_kicks+1);
 assert(!pgraph_vk_frame_write_begin(&state.display.handoff,1));
 /* Failed publication validation retains the previous persistent front. */
 publish(0,4);hook=change_publication;uint64_t old_count=count;
 assert(!read_frame()&&count==old_count&&!retro_atomic_load_relaxed_int(&state.display.handoff.reading));
 assert(!pgraph_vk_frame_write_begin(&state.display.handoff,1));
 state.display.frames[2].ready=false;assert(!read_frame()&&count==old_count);
 state.display.frames[2].ready=true;assert(read_frame()&&count==3);nv2a_vk_libretro_frame_presented();cases+=3;
 for(unsigned i=0;i<20000;i++) {
  unsigned old=retro_atomic_load_relaxed_int(&state.display.handoff.front)-1;
  unsigned slot=(old+1)%8;uint64_t next=count+1;
  assert(pgraph_vk_frame_write_begin(&state.display.handoff,slot));
  publish(slot,next);pgraph_vk_frame_write_end(&state.display.handoff);
  assert(read_frame()&&count==next&&image==1000+next&&view==2000+next&&format==37&&width==640&&height==480);
  assert(!pgraph_vk_frame_write_begin(&state.display.handoff,old));
  assert(!pgraph_vk_frame_write_begin(&state.display.handoff,slot));
  nv2a_vk_libretro_frame_presented();
  assert(pgraph_vk_frame_write_begin(&state.display.handoff,old));pgraph_vk_frame_write_end(&state.display.handoff);cases++;
 }
 memset(&state.display,0,sizeof(state.display));count=0;hook=NULL;concurrent=true;
 sthread_t *writer=sthread_create(producer,NULL),*reader=sthread_create(consumer,NULL);
 assert(writer&&reader);sthread_join(writer);sthread_join(reader);concurrent=false;
 assert(produced&&consumed&&!retro_atomic_load_relaxed_int(&state.display.handoff.reading));
 printf("Concurrent handoff: %u publications, %u stable frontend snapshots\n",produced,consumed);
 /* Resize pressure cannot destroy unfinished retired images or advance age. */
 memset(&state.display,0,sizeof(state.display));state.display.width=800;state.display.height=600;
 state.display.submitted=20;state.display.settled=9;state.display.frame_count=1000;
 state.display.frames[0]=(struct PGRAPHVkFrameImage){.image=99,.width=640,.height=480,.ready=true};
 for(unsigned i=0;i<16;i++)state.display.retired[i]=(struct PGRAPHVkFrameImage){.image=100+i,.retire_at=10};
 assert(!frame_image_for_slot(&device.pgraph,0)&&!destroyed&&!created&&state.display.frame_count==1000&&state.display.frames[0].image==99);
 state.display.settled=10;assert(frame_image_for_slot(&device.pgraph,0));
 assert(destroyed==16&&created==1&&state.display.retired[0].image==99&&state.display.retired[0].retire_at==21);
 state.display.frame_count=1000000;state.display.settled=20;assert(frame_image_for_slot(&device.pgraph,1));assert(destroyed==16);
 state.display.settled=21;assert(frame_image_for_slot(&device.pgraph,2));assert(destroyed==17);cases+=4;
 printf("PASS: %u frame handoff/serial-retirement schedules, including acquisition races and full retirement storage\n",cases);
 return 0;
}
