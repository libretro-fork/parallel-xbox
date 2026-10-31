/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t hwaddr;
#include "hw/xbox/nv2a/pgraph/surface.h"
#define NV097_SET_SURFACE_FORMAT_TYPE_SWIZZLE 2
#define DIRTY_MEMORY_NV2A 0
#define NV2A_UNIMPLEMENTED(...) assert(false)
typedef struct SurfaceBinding {
    SurfaceShape shape;
    struct { unsigned vk_format; } host_fmt;
    struct { unsigned bytes_per_pixel; } fmt;
    bool color,swizzle,native_resolution,upload_pending,initialized;
    unsigned width,height,pitch;
    hwaddr vram_addr,size;
} SurfaceBinding;
typedef struct { hwaddr offset; unsigned pitch; bool buffer_dirty,draw_dirty,write_enabled_cache; } Surface;
typedef struct {
    SurfaceBinding *color_binding,*zeta_binding,*cached;
    SurfaceBinding request,created;
    PGRAPHRtMemo rt_memo;
    int surface_pool;
} PGRAPHVkState;
typedef struct {
    PGRAPHVkState *vk_renderer_state;
    Surface surface_color,surface_zeta;
    hwaddr dma_color,dma_zeta;
    SurfaceShape surface_shape;
    unsigned surface_type;
    bool clearing;
    struct { unsigned width,height,clip_x,clip_y,clip_width,clip_height; } surface_binding_dim;
} PGRAPHState;
typedef struct { PGRAPHState pgraph; void *vram; } NV2AState;
static bool tcg=true,dirty;
static unsigned evictions,creates,uploads,gets,downloads,settles,stales;
static bool tcg_enabled(void) { return tcg; }
static void eager_submit_at_switch(PGRAPHState *pg) { (void)pg; }
static void pgraph_vk_ensure_not_in_render_pass(PGRAPHState *pg) { (void)pg; }
static void unbind_surface(NV2AState *d,bool color) {
    if(color) d->pgraph.vk_renderer_state->color_binding=NULL;
    else d->pgraph.vk_renderer_state->zeta_binding=NULL;
}
static void bind_surface(PGRAPHVkState *r,SurfaceBinding *s) {
    if(s->color) r->color_binding=s;else r->zeta_binding=s;
}
static void populate_surface_binding_target(NV2AState *d,bool color,SurfaceBinding *target) {
    assert(!color);*target=d->pgraph.vk_renderer_state->request;
}
static void populate_surface_binding_target_sized(NV2AState *d,bool color,unsigned w,unsigned h,SurfaceBinding *target) {
    populate_surface_binding_target(d,color,target);target->width=w;target->height=h;
}
static bool memory_region_test_and_clear_dirty(void *vram,hwaddr addr,hwaddr size,int client) {
    bool was=dirty;dirty=false;return was;
}
static void pgraph_vk_vertex_mark_stale(PGRAPHState *pg,hwaddr addr,hwaddr size) { stales++; }
static SurfaceBinding *pgraph_vk_surface_get(NV2AState *d,hwaddr addr) {
    gets++;SurfaceBinding *s=d->pgraph.vk_renderer_state->cached;
    return s && s->vram_addr==addr?s:NULL;
}
static void trace_stub(uint32_t addr,uint32_t w,uint32_t h,const char *layout,uint32_t aa,
                       uint32_t cx,uint32_t cw,uint32_t cy,uint32_t ch,uint32_t pitch) {}
#define trace_nv2a_pgraph_surface_match_color trace_stub
#define trace_nv2a_pgraph_surface_match_zeta trace_stub
#define trace_nv2a_pgraph_surface_create_color trace_stub
#define trace_nv2a_pgraph_surface_create_zeta trace_stub
#define trace_nv2a_pgraph_surface_hit_color trace_stub
#define trace_nv2a_pgraph_surface_hit_zeta trace_stub
#define trace_nv2a_pgraph_surface_target(...) ((void)0)
#define trace_nv2a_pgraph_surface_evict_reason(...) ((void)0)
static void compare_surfaces(const SurfaceBinding *a,const SurfaceBinding *b) {}
static void evict_surface(NV2AState *d,SurfaceBinding *s) {
    evictions++;downloads++;pgraph_rt_memo_forget(&d->pgraph.vk_renderer_state->rt_memo,s);
}
static bool parked_settle_for_create(NV2AState *d,const SurfaceBinding *s) { settles++;return false; }
static bool pgraph_writeback_discarded(PGRAPHState *pg,hwaddr addr,hwaddr size) { return false; }
static SurfaceBinding *get_any_compatible_invalid_surface(PGRAPHVkState *r,SurfaceBinding *s) { return NULL; }
static void migrate_surface_image(SurfaceBinding *a,SurfaceBinding *b) { assert(false); }
static SurfaceBinding *allocation;
static SurfaceBinding *obj_pool_take(int *pool) { return allocation; }
static void create_surface_image(PGRAPHState *pg,SurfaceBinding *s) { creates++; }
static void set_surface_label(PGRAPHState *pg,SurfaceBinding *s) {}
static void surface_put(NV2AState *d,SurfaceBinding *s) { d->pgraph.vk_renderer_state->cached=s; }
static void pgraph_vk_surface_download_if_dirty(NV2AState *d,SurfaceBinding *s) { downloads++; }
/* PRODUCTION */
int main(void) {
    unsigned cases=0;
    const unsigned dims[]={64,128,640};
    for(unsigned memo=0;memo<2;memo++)
    for(unsigned mode=0;mode<3;mode++)
    for(unsigned cw=0;cw<3;cw++) for(unsigned ch=0;ch<3;ch++)
    for(unsigned zw=0;zw<3;zw++) for(unsigned zh=0;zh<3;zh++)
    for(unsigned flags=0;flags<64;flags++) {
        PGRAPHVkState r={0};NV2AState d={.pgraph={.vk_renderer_state=&r}};
        PGRAPHState *pg=&d.pgraph;
        SurfaceBinding c={.color=true,.width=dims[cw],.height=dims[ch],
            .shape={.anti_aliasing=1,.clip_x=3,.clip_y=5,.clip_width=7,.clip_height=9}};
        SurfaceBinding z={.width=dims[zw],.height=dims[zh],.pitch=2560,
            .vram_addr=0x4000,.size=2560*dims[zh],.host_fmt={1},
            .shape={.anti_aliasing=1,.clip_x=30,.clip_y=50,.clip_width=70,.clip_height=90}};
        z.swizzle=flags&1;c.swizzle=flags&2;
        if(flags&4) z.shape.anti_aliasing=2;
        z.native_resolution=flags&8;c.native_resolution=flags&16;
        if(flags&32) z.pitch=5120;
        r.color_binding=&c;r.cached=&z;allocation=&r.created;
        r.request=z;r.request.width=c.width;r.request.height=c.height;
        r.request.pitch=2560;r.request.shape=c.shape;r.request.upload_pending=true;
        pg->surface_shape=c.shape;pg->surface_zeta.buffer_dirty=true;
        pg->surface_zeta.offset=z.vram_addr;pg->surface_zeta.pitch=2560;
        pg->dma_zeta=0x200;tcg=mode==0;dirty=mode==2;
        bool coverage=z.width>=c.width && z.height>=c.height;
        bool same=z.width==c.width && z.height==c.height;
        bool reusable=coverage && (same || (!z.swizzle && !c.swizzle &&
            z.shape.anti_aliasing==c.shape.anti_aliasing && z.native_resolution==c.native_resolution));
        reusable &= z.pitch==2560;
        /* A memo entry only exists after its DMA/offset/pitch resolved. */
        if(memo && z.pitch==2560) pgraph_rt_memo_store(&r.rt_memo,false,
            pg->dma_zeta,pg->surface_zeta.offset,pg->surface_zeta.pitch,&z);
        evictions=creates=uploads=gets=downloads=settles=stales=0;
        update_surface_part(&d,true,false);
        assert(r.zeta_binding && !pg->surface_zeta.buffer_dirty);
        assert(pg->surface_binding_dim.width==c.width && pg->surface_binding_dim.height==c.height);
        assert(pg->surface_binding_dim.clip_x==3 && pg->surface_binding_dim.clip_y==5);
        assert(pg->surface_binding_dim.clip_width==7 && pg->surface_binding_dim.clip_height==9);
        if(reusable) {
            assert(r.zeta_binding==&z && !creates && !evictions && !downloads && !settles);
            if(memo && tcg) assert(!gets);
            if(mode==2) assert(z.upload_pending && stales==1);
        } else {
            assert(r.zeta_binding==&r.created && creates==1 && evictions==1 && downloads==1);
        }
        cases++;
    }
    PGRAPHVkState r={0};SurfaceBinding z={.width=640,.height=480};
    assert(zeta_covers_color(&r,&z));
    puts("Vulkan depth reuse: full lookup/memo, clips, dimensions, AA/layout/scale and dirty-memory guards passed");
    printf("%u production resolution cases; eligible paths make no eviction/download/create\n",cases);
}
