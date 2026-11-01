/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
typedef void *GLsync;
#define GL_SYNC_GPU_COMMANDS_COMPLETE 1
#define GL_TIMEOUT_IGNORED UINT64_MAX
#define DISP_POST_NEW 4
#define ATTEMPTS 100000
#define MAX_FENCES (2*ATTEMPTS+64)
static retro_atomic_int_t created,deleted,waits,kicks;
static retro_atomic_int_t dead[MAX_FENCES];
static GLsync glFenceSync(unsigned kind,unsigned flags) {
 assert(kind==GL_SYNC_GPU_COMMANDS_COMPLETE && !flags);
 unsigned id=retro_atomic_fetch_add_int(&created,1)+1;assert(id<MAX_FENCES);
 return (void *)(uintptr_t)id;
}
static void glDeleteSync(GLsync sync) {
 if(!sync)return;
 unsigned id=(uintptr_t)sync;assert(id<MAX_FENCES);
 assert(!retro_atomic_exchange_int(&dead[id],1));retro_atomic_inc_int(&deleted);
}
static void glWaitSync(GLsync sync,unsigned flags,uint64_t timeout) {
 assert(sync && !flags && timeout==GL_TIMEOUT_IGNORED);
 assert(!retro_atomic_load_acquire_int(&dead[(uintptr_t)sync]));
 retro_atomic_inc_int(&waits); /* GPU enqueue only; fences never signal here. */
}
static void glFlush(void) {}
typedef struct {
 unsigned tex,draw_time;bool pvideo;GLsync rendered;
} DispPost;
typedef struct {
 int disp_post_write,disp_post_read;retro_atomic_int_t disp_post_middle;
 retro_atomic_int_t have_surfaces,disp_shown,disp_check_pending;
} PGRAPHGLState;
typedef struct {PGRAPHGLState *gl_renderer_state;retro_atomic_int_t sync_pending;} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
static PGRAPHGLState renderer;
static NV2AState device;
static struct {int cur;struct {unsigned tex,width,height;retro_atomic_ptr_t presented;DispPost post;} slot[3];} ui;
static struct {unsigned presented_frame_id;} g_nv2a_stats;
static void pfifo_kick(NV2AState *d) {assert(d==&device);retro_atomic_inc_int(&kicks);}
/* PRODUCTION */
static void reset(void) {
 memset(&renderer,0,sizeof(renderer));memset(&device,0,sizeof(device));
 memset(&ui,0,sizeof(ui));
 memset(dead,0,sizeof(dead));created=deleted=waits=kicks=0;
 renderer.disp_post_write=0;renderer.disp_post_read=1;
 retro_atomic_int_init(&renderer.disp_post_middle,2);
 retro_atomic_int_init(&renderer.have_surfaces,1);
 retro_atomic_int_init(&renderer.disp_shown,1);
 device.pgraph.gl_renderer_state=&renderer;ui.cur=-1;
 for(unsigned i=0;i<3;i++)ui.slot[i].tex=100+i;
}
static void publish(unsigned generation) {
 unsigned slot=renderer.disp_post_write;
 display_slot_reuse(slot);
 ui.slot[slot].width=generation;ui.slot[slot].height=generation^0x123456;
 DispPost p={.tex=100+slot,.draw_time=generation,
  .rendered=glFenceSync(1,0),.pvideo=generation&1};
 disp_post_ticket(&renderer,&p);
}
static void consume(void) {
 unsigned width=0,height=0;
 unsigned tex=pgraph_gl_get_framebuffer_surface(&device,&width,&height);
 if(tex) {
  assert(tex==ui.slot[ui.cur].tex);
  assert(height==(width^0x123456));
  assert(width==g_nv2a_stats.presented_frame_id);
 }
}
static void cleanup(void) {
 int middle=retro_atomic_load_relaxed_int(&renderer.disp_post_middle);
 if(middle&DISP_POST_NEW)disp_post_release(&ui.slot[middle&3].post);
 for(unsigned i=0;i<3;i++)glDeleteSync(retro_atomic_exchange_ptr(&ui.slot[i].presented,NULL));
 assert(retro_atomic_load_relaxed_int(&created)==retro_atomic_load_relaxed_int(&deleted));
}
static void producer(void *unused) {(void)unused;for(unsigned i=1;i<=ATTEMPTS;i++)publish(i);}
static void consumer(void *unused) {(void)unused;for(unsigned i=0;i<ATTEMPTS;i++)consume();}
int main(void) {
 unsigned cases=0;
 for(unsigned schedule=0;schedule<4096;schedule++) {
  reset();
  for(unsigned bit=0;bit<12;bit++) {
   if(schedule&(1u<<bit))publish(bit+1);else consume();
   assert(renderer.disp_post_write!=renderer.disp_post_read);
   int middle=retro_atomic_load_relaxed_int(&renderer.disp_post_middle)&3;
   assert(middle!=renderer.disp_post_write && middle!=renderer.disp_post_read);
   if(ui.cur>=0)assert(ui.cur==renderer.disp_post_read);
  }
  cleanup();cases++;
 }
 reset();publish(1);consume();int held=ui.cur;
 unsigned before=created;for(unsigned i=0;i<1000;i++)consume();assert(created==(int)before);
 for(unsigned i=2;i<1000;i++){publish(i);assert(renderer.disp_post_write!=held);assert(ui.slot[held].width==1);}
 consume();assert(g_nv2a_stats.presented_frame_id==999);cleanup();cases++;
 reset();renderer.have_surfaces=renderer.disp_shown=0;consume();assert(created==0);cleanup();cases++;
 reset();sthread_t *p=sthread_create(producer,NULL),*c=sthread_create(consumer,NULL);assert(p&&c);
 sthread_join(p);sthread_join(c);consume();assert(g_nv2a_stats.presented_frame_id==ATTEMPTS);cleanup();
 printf("PASS: %u GL mailbox schedules, held-front stability and %u concurrent attempts per thread; all fences released\n",cases,ATTEMPTS);
}
