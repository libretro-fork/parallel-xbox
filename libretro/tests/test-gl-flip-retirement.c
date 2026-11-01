/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <retro_atomic.h>
typedef void *GLsync;typedef int GLint;
#define GL_UNSIGNALED 1
#define GL_SIGNALED 2
#define GL_SYNC_STATUS 3
#define GL_SYNC_GPU_COMMANDS_COMPLETE 4
#define NV2A_GL_DFRAME_TERMINATOR() ((void)0)
#define unlikely(x) (x)
#define QTAILQ_FOREACH(v,h,f) for((v)=(h)->first;(v);(v)=(v)->f.next)
typedef struct SurfaceBinding { unsigned cpu_reads,cpu_reads_last_flip;struct {struct SurfaceBinding *next;}entry; } SurfaceBinding;
typedef struct {GLsync flip_fence,flip_wait_fence;unsigned flip_scale;bool flip_failed,flip_display_queued;retro_atomic_int_t download_dirty_surfaces_pending,disp_check_pending,shader_cache_writeback_pending;bool gl_zpass_query_open;struct {SurfaceBinding *first;}surfaces;} PGRAPHGLState;
typedef struct {PGRAPHGLState *gl_renderer_state;bool host_flip_pending,reports_pending;unsigned surface_scale_factor;retro_atomic_int_t waiting_for_flip,scanout_changed,sync_pending,flush_pending;struct {retro_atomic_int_t touched,dl_pending;}cpu;} PGRAPHState;
typedef struct {PGRAPHState pgraph;struct {int lock;}pfifo;} NV2AState;
typedef struct {struct {void (*process_pending)(NV2AState *);void (*cpu_access_service)(NV2AState *);void (*surface_update)(NV2AState *,bool,bool,bool);void (*flip_stall)(NV2AState *);bool (*flip_stall_ready)(NV2AState *);int (*poll_pending_report)(NV2AState *);}ops;} PGRAPHRenderer;
static PGRAPHRenderer renderer;
static PGRAPHRenderer *pgraph_renderer(PGRAPHState *p){(void)p;return &renderer;}
static unsigned created,deleted,polls,flushes,services,closes,finishes,predownloads,refreshes,report_waits,deadlines;
static unsigned displays,unlocks,locks,maintenance;
static unsigned gpu_done,required,expected_scale;static bool dead[12000],complete_on_flush,profile,fail_fence;
static int xemu_cost_segn;static bool xemu_cost_q_open,xemu_cost_q2_open;
static int pgraph_gl_cost_ops,xemu_tbrate_fence_ns,xemu_tbrate_flipsvc_ns;
static int64_t clock_now;
static unsigned errors;
static int expected_error(FILE *stream,const char *format,...){assert(stream==stderr&&format);errors++;return 0;}
#define fprintf expected_error
static bool xemu_tbrate_enabled(void){return profile;}
static int64_t get_clock(void){return ++clock_now;}
static void xemu_tbrate_add(int *p,int64_t n){assert(n>0);*p+=(int)n;}
static GLsync glFenceSync(unsigned type,unsigned flags){assert(type==4&&!flags);if(fail_fence)return NULL;assert(++created<12000);return (void *)(uintptr_t)created;}
static void glDeleteSync(GLsync sync){if(sync){unsigned n=(uintptr_t)sync;assert(n<=created&&!dead[n]);dead[n]=true;deleted++;}}
static void glFlush(void){flushes++;if(complete_on_flush)gpu_done=created;}
static void glGetSynciv(GLsync sync,unsigned name,unsigned size,int *length,int *value){
 unsigned n=(uintptr_t)sync;assert(n&&n<=created&&!dead[n]&&name==3&&size==1&&!length);polls++;
 *value=n<=gpu_done?GL_SIGNALED:GL_UNSIGNALED;
}
static void pgraph_gl_shaders_flip_service(NV2AState*d){(void)d;services++;}
static void xemu_cost_frame_close(NV2AState*d,int *ops){(void)d;assert(ops==&pgraph_gl_cost_ops);closes++;xemu_cost_q_open=xemu_cost_q2_open=false;}
static void pgraph_gl_surface_predownload(NV2AState*d,SurfaceBinding*s){(void)d;assert(gpu_done>=required&&s->cpu_reads==s->cpu_reads_last_flip);predownloads++;}
static void pgraph_gl_surfaces_refresh_any_dirty(NV2AState*d){(void)d;assert(gpu_done>=required);refreshes++;}
static void xemu_cost_resolve_frame(NV2AState*d,int *ops,unsigned scale){(void)d;assert(ops==&pgraph_gl_cost_ops&&scale==expected_scale&&!xemu_cost_q_open&&!xemu_cost_q2_open&&gpu_done>=required);if(xemu_cost_segn)assert(gpu_done>=created);xemu_cost_segn=0;finishes++;}
static void xemu_cost_flip_deadline(NV2AState*d){(void)d;assert(!xemu_cost_segn);deadlines++;}
static void mock_surface_update(NV2AState*d,bool upload,bool color,bool zeta){(void)d;assert(!upload&&color&&zeta);}
static void trace_nv2a_pgraph_flip_stall(void){}
static void nv2a_profile_flip_stall(void){}
#define DEF_METHOD(g,n) static void generic_flip(NV2AState *d,PGRAPHState *pg)
static bool guest_stall,intr_error,intr_context,fifo_access;
#define NV_PGRAPH_INTR_ERROR 1
#define NV_PGRAPH_INTR_CONTEXT_SWITCH 2
static bool pfifo_stall_for_flip(NV2AState*d){(void)d;return guest_stall;}
static bool pgraph_pending_bit(PGRAPHState*p,unsigned flag){(void)p;return flag==1?intr_error:intr_context;}
static bool can_fifo_access(NV2AState*d){(void)d;return fifo_access;}
static int mock_report_wait(NV2AState*d){(void)d;report_waits++;return false;}
static void qemu_mutex_unlock(int*p){(void)p;unlocks++;}
static void qemu_mutex_lock(int*p){(void)p;locks++;}
#define GL_SAMPLES_PASSED 5
static void glEndQuery(unsigned target){assert(target==5);}
static void pgraph_gl_drain_pending_reports(NV2AState*d){(void)d;}
static void pgraph_gl_download_dirty_surfaces(NV2AState*d){d->pgraph.gl_renderer_state->download_dirty_surfaces_pending=0;maintenance++;}
static void pgraph_gl_sync(NV2AState*d){assert(!d->pgraph.host_flip_pending||(!d->pgraph.gl_renderer_state->flip_display_queued&&!d->pgraph.gl_renderer_state->flip_failed));d->pgraph.sync_pending=0;displays++;}
static void pgraph_gl_present_scanout(NV2AState*d){assert(!d->pgraph.host_flip_pending||(!d->pgraph.gl_renderer_state->flip_display_queued&&!d->pgraph.gl_renderer_state->flip_failed));displays++;}
static void pgraph_gl_flush(NV2AState*d){d->pgraph.flush_pending=0;maintenance++;}
static void pgraph_gl_shader_write_cache_reload_list(PGRAPHState*p){p->gl_renderer_state->shader_cache_writeback_pending=0;maintenance++;}
/* PRODUCTION */
static void reset(NV2AState*d,PGRAPHGLState*r,SurfaceBinding*s){
 memset(d,0,sizeof(*d));memset(r,0,sizeof(*r));memset(s,0,3*sizeof(*s));memset(dead,0,sizeof(dead));
 d->pgraph.gl_renderer_state=r;d->pgraph.surface_scale_factor=expected_scale=1;
 s[0].entry.next=&s[1];s[1].entry.next=&s[2];s[0].cpu_reads=3;s[2].cpu_reads=2;r->surfaces.first=s;
 created=deleted=polls=flushes=services=closes=finishes=predownloads=refreshes=report_waits=deadlines=gpu_done=required=errors=0;
 complete_on_flush=profile=guest_stall=intr_error=intr_context=fail_fence=false;fifo_access=true;
 displays=unlocks=locks=maintenance=0;
 renderer.ops.process_pending=pgraph_gl_process_pending;renderer.ops.cpu_access_service=NULL;
 renderer.ops.surface_update=mock_surface_update;renderer.ops.flip_stall=pgraph_gl_flip_stall;renderer.ops.flip_stall_ready=pgraph_gl_flip_stall_ready;renderer.ops.poll_pending_report=mock_report_wait;
 xemu_cost_segn=0;xemu_cost_q_open=xemu_cost_q2_open=false;
}
int main(void){
 NV2AState d;PGRAPHGLState r;SurfaceBinding surfaces[3];unsigned cases=0;
 for(unsigned cost=0;cost<3;cost++)for(unsigned fast=0;fast<2;fast++)
 for(unsigned busy=0;busy<17;busy++)for(unsigned scale=1;scale<4;scale++)for(unsigned timing=0;timing<2;timing++){
  reset(&d,&r,surfaces);generic_flip(&d,&d.pgraph);assert(!d.pgraph.host_flip_pending&&finishes==1&&deadlines==1&&created==1);
  profile=timing;complete_on_flush=fast;d.pgraph.surface_scale_factor=expected_scale=scale;
  xemu_cost_segn=cost;xemu_cost_q_open=xemu_cost_q2_open=cost!=0;required=cost?2:1;
  surfaces[0].cpu_reads++;unsigned downloads=predownloads;
  generic_flip(&d,&d.pgraph);assert(services==2&&closes==2&&created==2);
  if(!fast){
   assert(d.pgraph.host_flip_pending&&finishes==1&&predownloads==downloads);
   assert(pfifo_puller_should_stall(&d));d.pgraph.surface_scale_factor=scale+10;
   for(unsigned n=0;n<busy;n++){
    unsigned before=polls;d.pgraph.reports_pending=true;idle_report_prefix(&d);assert(!report_waits);
    d.pgraph.sync_pending=d.pgraph.scanout_changed=r.disp_check_pending=1;
    unsigned before_locks=locks;owner_dispatch(&d);
    assert(displays==1&&r.flip_display_queued);
    assert(locks==before_locks+(n==0));
    assert(d.pgraph.sync_pending==(n==0?0:1));
    assert(polls==before+1&&finishes==1&&d.pgraph.host_flip_pending);
    assert(created-deleted<=2&&xemu_cost_segn==(int)cost&&predownloads==downloads);
   }
   d.pgraph.sync_pending=1;
   d.pgraph.flush_pending=1;owner_dispatch(&d);
   assert(displays==1&&maintenance==1&&r.flip_display_queued);
   d.pgraph.sync_pending=1;
   gpu_done=created;owner_dispatch(&d);assert(displays==2&&d.pgraph.sync_pending==0);
  }
  assert(!d.pgraph.host_flip_pending&&finishes==2&&deadlines==2&&predownloads==downloads+1&&refreshes==2&&xemu_cost_segn==0);
  assert(!pfifo_puller_should_stall(&d));
  unsigned before=polls;for(unsigned n=0;n<20;n++)pgraph_process_pending_flip(&d);assert(polls==before&&finishes==2);
  pgraph_gl_discard_flip(&d.pgraph);assert(created==deleted);cases++;
 }
 /* Reset/renderer-switch retirement must not replay an unfinished flip or
  * return private pending measurements to a different backend. */
 for(unsigned cost=0;cost<2;cost++){
  reset(&d,&r,surfaces);generic_flip(&d,&d.pgraph);xemu_cost_segn=cost;required=cost?2:1;
  generic_flip(&d,&d.pgraph);assert(d.pgraph.host_flip_pending);
  guest_stall=false;retro_atomic_store_relaxed_int(&d.pgraph.waiting_for_flip,0);assert(pfifo_puller_should_stall(&d));
  pgraph_gl_discard_flip(&d.pgraph);assert(!d.pgraph.host_flip_pending&&created==deleted&&!xemu_cost_segn);
  unsigned before=polls;pgraph_process_pending_flip(&d);assert(polls==before);cases++;
 }
 reset(&d,&r,surfaces);generic_flip(&d,&d.pgraph);
 fail_fence=true;xemu_cost_segn=1;generic_flip(&d,&d.pgraph);
 assert(d.pgraph.host_flip_pending&&r.flip_failed&&finishes==1&&xemu_cost_segn==1);
 unsigned failed_polls=polls;
 for(unsigned i=0;i<100;i++){d.pgraph.sync_pending=1;owner_dispatch(&d);}
 assert(polls==failed_polls&&finishes==1&&d.pgraph.host_flip_pending&&errors==1&&!displays);
 pgraph_gl_discard_flip(&d.pgraph);assert(!r.flip_failed&&created==deleted&&!xemu_cost_segn);cases++;
 reset(&d,&r,surfaces);
 for(unsigned frame=0;frame<5000;frame++){
  xemu_cost_segn=frame%3;complete_on_flush=frame&1;d.pgraph.surface_scale_factor=expected_scale=1+frame%4;
  required=xemu_cost_segn?created+1:(uintptr_t)r.flip_fence;
  generic_flip(&d,&d.pgraph);assert(created-deleted<=2);d.pgraph.sync_pending=1;
  for(unsigned n=0;n<frame%7&&d.pgraph.host_flip_pending;n++)owner_dispatch(&d);
  gpu_done=created;owner_dispatch(&d);assert(displays==frame+1);
  assert(!d.pgraph.host_flip_pending&&created-deleted<=1&&finishes==frame+1&&deadlines==frame+1);
 }
 pgraph_gl_discard_flip(&d.pgraph);assert(created==deleted);
 printf("PASS: %u flip retirement schedules, 5000 mixed frames, bounded fences and deferred query reads\n",cases);
}
