/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define NV2A_MAX_TEXTURES 4
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct TextureBinding { bool s2t_borrowed; uintptr_t image;
    struct { bool byte_reinterpret; uint64_t texture_vram_offset; } key;
    unsigned draw_time;
    unsigned sampled_frame,submit_time; } TextureBinding;
typedef struct SurfaceBinding { unsigned frame_time; uintptr_t image;
    uint64_t vram_addr; unsigned draw_time; struct SurfaceBinding *next; bool evicted; } SurfaceBinding;
typedef struct { SurfaceBinding *first; } Surfaces;
typedef struct { TextureBinding *texture_bindings[4]; unsigned submit_count;
    Surfaces surfaces; } PGRAPHVkState;
typedef struct { unsigned frame_time; PGRAPHVkState *vk_renderer_state; } PGRAPHState;
typedef struct { PGRAPHState pgraph; } NV2AState;
#define QTAILQ_FOREACH_SAFE(v,h,e,n) for(v=(h)->first;v && ((n=v->next),true);v=n)
#define trace_nv2a_pgraph_surface_evict_reason(a,b) ((void)0)
static void evict_surface(NV2AState *d,SurfaceBinding *s) {
    SurfaceBinding **p=&d->pgraph.vk_renderer_state->surfaces.first;
    while(*p!=s) p=&(*p)->next;
    *p=s->next; s->evicted=true;
}
/* PRODUCTION */
int main(void) {
    const unsigned source_frames[]={0,5,6,9}, sample_frames[]={0,5,6,9,10};
    unsigned cases=0;
    for(unsigned sf=0;sf<4;sf++)
    for(unsigned tf=0;tf<5;tf++)
    for(unsigned borrowed=0;borrowed<2;borrowed++)
    for(unsigned matches=0;matches<2;matches++)
    for(unsigned bound=0;bound<2;bound++) {
        SurfaceBinding s={.frame_time=source_frames[sf],.image=1};
        TextureBinding t={.sampled_frame=sample_frames[tf],
            .s2t_borrowed=borrowed,.image=matches?1:2};
        PGRAPHVkState r={.surfaces={&s},.texture_bindings={bound?&t:NULL}};
        NV2AState d={.pgraph={10,&r}};
        bool source_recent=10-source_frames[sf]<5;
        bool sample_recent=bound && borrowed && matches && 10-sample_frames[tf]<5;
        expire_old_surfaces(&d);
        assert(s.evicted==!(source_recent || sample_recent));
        cases++;
    }
    for(unsigned address_match=0;address_match<2;address_match++)
    for(unsigned draw_match=0;draw_match<2;draw_match++)
    for(unsigned copied=0;copied<2;copied++)
    for(unsigned recent=0;recent<2;recent++) {
        SurfaceBinding s={.frame_time=0,.image=1,.vram_addr=100,.draw_time=12};
        TextureBinding t={.sampled_frame=recent?9:0,.image=2,.draw_time=draw_match?12:11,
            .key={copied,address_match?100:101}};
        PGRAPHVkState r={.surfaces={&s},.texture_bindings={&t}};
        NV2AState d={.pgraph={10,&r}};
        expire_old_surfaces(&d);
        assert(s.evicted==!(copied && address_match && draw_match && recent));
        if(!s.evicted) {
            d.pgraph.frame_time=15;
            expire_old_surfaces(&d); assert(s.evicted);
        }
        cases++;
    }
    /* Both normal and fast binds stamp usage through this production helper. */
    TextureBinding t={.s2t_borrowed=true,.image=1};
    SurfaceBinding s={.frame_time=0,.image=1};
    PGRAPHVkState r={.surfaces={&s},.texture_bindings={&t},.submit_count=33};
    NV2AState d={.pgraph={10,&r}};
    update_timestamps(&d.pgraph);
    assert(t.sampled_frame==10 && t.submit_time==33);
    expire_old_surfaces(&d);
    assert(!s.evicted && s.frame_time==10);
    d.pgraph.frame_time=16; /* Still bound but unused: must not pin forever. */
    expire_old_surfaces(&d); assert(s.evicted);
    s=(SurfaceBinding){.frame_time=UINT32_MAX-8,.image=1}; r.surfaces.first=&s;
    t.sampled_frame=UINT32_MAX; d.pgraph.frame_time=2;
    expire_old_surfaces(&d); assert(!s.evicted && s.frame_time==UINT32_MAX);
    SurfaceBinding faces[6]={0},flat={.frame_time=5};
    SurfaceBinding *face_ptrs[6];
    for(unsigned i=0;i<6;i++) face_ptrs[i]=&faces[i];
    d.pgraph.frame_time=0;
    touch_texture_sources(&d.pgraph,&flat,face_ptrs,false);
    assert(flat.frame_time==0);
    for(unsigned i=0;i<6;i++) assert(faces[i].frame_time==0);
    d.pgraph.frame_time=20;
    touch_texture_sources(&d.pgraph,NULL,face_ptrs,true);
    for(unsigned i=0;i<6;i++) assert(faces[i].frame_time==20);
    d.pgraph.frame_time=30;
    touch_texture_sources(&d.pgraph,NULL,face_ptrs,false);
    for(unsigned i=0;i<6;i++) assert(faces[i].frame_time==20);
    puts("OpenGL flat/cube source-use stamps passed");
    printf("surface sampling age: %u selection cases, timestamp, idle expiry and wrap passed\n",cases);
}
