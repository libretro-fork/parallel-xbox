/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/vk/sun-shadow-capture.h"
#define HANDLE(t,n) ((t)(uintptr_t)(n))

typedef struct Version {
    uint64_t generation, handle;
    unsigned cpu, gpu, unrelated;
    bool immutable;
} Version;
typedef struct Ticket { unsigned resource; uint64_t generation; unsigned phase; PgraphSunUse use; } Ticket;
typedef struct Owner {
    Version versions[8]; Ticket tickets[128]; unsigned ticket_count;
    unsigned acquired, canceled, promoted, recorded, validated;
    unsigned fail_acquire, fail_valid, fail_record;
    bool partial_failure, reentrant;
    PgraphSunCapture *capture;
    uint64_t observed[8]; float observed_depth[8];
    bool (*emit)(void *, const PgraphSunScene *, const PgraphSunCapturePacket *);
    void *emit_opaque;
} Owner;

static void check_reentry(Owner *owner)
{
    if (!owner->reentrant) return;
    assert(!pgraph_sun_capture_cancel(owner->capture));
    assert(!pgraph_sun_capture_append(owner->capture,999,NULL,NULL,NULL));
}
static bool acquire(void *opaque,enum PgraphSunResourceSlot slot,PgraphSunResource key,
    const PgraphVkSunPass *pass,const PgraphVkSunDraw *draw,uint64_t *ticket)
{
    Owner *o=opaque;check_reentry(o);o->acquired++;
    assert(key.id<8);Version *v=&o->versions[key.id];
    uint64_t handle=slot==PGRAPH_SUN_VERTEX?(uint64_t)(uintptr_t)draw->vertex:
        slot==PGRAPH_SUN_INDEX?(uint64_t)(uintptr_t)draw->index:
        slot==PGRAPH_SUN_WEIGHTS?(uint64_t)(uintptr_t)draw->weights:
        slot==PGRAPH_SUN_UV?(uint64_t)(uintptr_t)draw->texcoords:
        slot==PGRAPH_SUN_MATERIAL?(uint64_t)(uintptr_t)draw->material:(uint64_t)(uintptr_t)pass->pipeline;
    if(v->generation!=key.generation || v->handle!=handle || !v->immutable)return false;
    bool fail=o->fail_acquire==o->acquired;
    if(fail && !o->partial_failure)return false;
    assert(o->ticket_count+1<128);*ticket=++o->ticket_count;
    o->tickets[*ticket]=(Ticket){.resource=key.id,.generation=key.generation,.phase=1};v->cpu++;
    return !fail;
}
static bool valid(void *opaque,uint64_t ticket)
{
    Owner *o=opaque;check_reentry(o);o->validated++;
    Ticket *t=&o->tickets[ticket];assert(t->phase==1);
    return o->validated!=o->fail_valid && o->versions[t->resource].generation==t->generation;
}
static void cancel(void *opaque,uint64_t ticket)
{
    Owner *o=opaque;check_reentry(o);Ticket *t=&o->tickets[ticket];
    assert(t->phase==1 && o->versions[t->resource].cpu);t->phase=0;
    o->versions[t->resource].cpu--;o->canceled++;
}
static void promote(void *opaque,uint64_t ticket,const PgraphSunUse *use)
{
    Owner *o=opaque;check_reentry(o);Ticket *t=&o->tickets[ticket];
    assert(t->phase==1 && o->versions[t->resource].cpu);t->phase=2;t->use=*use;
    o->versions[t->resource].cpu--;o->versions[t->resource].gpu++;o->promoted++;
}
static bool record(void *opaque,const PgraphSunScene *scene,const PgraphSunCapturePacket *packet)
{
    Owner *o=opaque;check_reentry(o);
    assert(o->validated>0 && scene->view[0]==1);
    assert(o->recorded<8);o->observed[o->recorded]=packet->sequence;
    o->observed_depth[o->recorded]=packet->draw.world_to_light[14];
    o->recorded++;
    bool emitted = !o->emit || o->emit(o->emit_opaque,scene,packet);
    return emitted && o->recorded!=o->fail_record;
}
static bool recyclable(Owner *o,unsigned id)
{ Version *v=&o->versions[id];return !v->cpu && !v->gpu && !v->unrelated; }
static void retire(Owner *o,uint32_t main,uint64_t aux,uint64_t display)
{
    for(unsigned i=1;i<=o->ticket_count;i++) {
        Ticket *t=&o->tickets[i];
        if(t->phase==2 && (int32_t)(main-t->use.main_submit)>=0 &&
            aux>=t->use.auxiliary_submit && display>=t->use.display_submit) {
            assert(o->versions[t->resource].gpu);o->versions[t->resource].gpu--;t->phase=0;
        }
    }
}
static void no_cpu_refs(Owner *o)
{ for(unsigned i=0;i<8;i++)assert(!o->versions[i].cpu); }
static void no_refs(Owner *o)
{ no_cpu_refs(o);for(unsigned i=0;i<8;i++)assert(!o->versions[i].gpu); }
static Owner owner(PgraphSunCapture *capture)
{
    Owner o={.capture=capture};
    for(unsigned i=1;i<8;i++)o.versions[i]=(Version){.generation=1,.handle=100+i,.immutable=true};
    return o;
}
static PgraphSunScene scene(void)
{
    PgraphSunScene s={0};for(unsigned i=0;i<16;i+=5)s.view[i]=s.projection[i]=s.inverse_view_projection[i]=1;
    s.light_travel[1]=-1;return s;
}
#ifndef SUN_CAPTURE_GPU_FIXTURE
static PgraphVkSunPass pass(bool material)
{
    return(PgraphVkSunPass){.pipeline=HANDLE(VkPipeline,106),.layout=HANDLE(VkPipelineLayout,206),
        .stride=16,.position_bytes=16,.weight_bytes=material?4:0,
        .variant={.matrices=material?2:1,.cutout=material,.remainder=material,.weight_stride=4,.uv_stride=8}};
}
static PgraphVkSunDraw draw(bool material)
{
    PgraphVkSunDraw d={.vertex=HANDLE(VkBuffer,101),.vertex_size=48,.count=3,
        .weights=material?HANDLE(VkBuffer,103):VK_NULL_HANDLE,.weight_size=12,
        .texcoords=material?HANDLE(VkBuffer,104):VK_NULL_HANDLE,.uv_size=24,
        .material=material?HANDLE(VkDescriptorSet,105):VK_NULL_HANDLE,.alpha_reference=128,.alpha_compare=4};
    for(unsigned i=0;i<16;i+=5)d.world_to_light[i]=1;
    d.world_to_light[14]=.25f;return d;
}
#endif
static PgraphSunDrawResources resources(void)
{ return(PgraphSunDrawResources){.vertex={1,1},.index={2,1},.weights={3,1},.uv={4,1},.material={5,1},.pipeline={6,1}}; }
static bool begin_capture(PgraphSunCapture *c,PgraphSunCapturePacket *storage,size_t capacity,Owner *o)
{
    PgraphSunScene s=scene();PgraphSunCaptureOwner ops={o,acquire,valid,cancel,promote,NULL};
    return pgraph_sun_capture_begin(true,c,storage,capacity,ops,PGRAPH_SUN_DOAXVB_US_11106,3,11,7,&s);
}
