/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "hw/xbox/nv2a/pgraph/sun-shadow-fifo.h"
#include "hw/xbox/nv2a/pgraph/vk/sun-shadow-owner.h"
#define HANDLE(t,n) ((t)(uintptr_t)(n))

static uint8_t ram[0x340000];
static bool bad_translation;
static unsigned scene_reads, translations, emissions;
static bool emit_failure;
static bool read_ram(void *opaque, uint32_t address, void *out, size_t size)
{
    (void)opaque; scene_reads++;
    if (address > sizeof(ram) || size > sizeof(ram) - address) return false;
    memcpy(out, ram + address, size); return true;
}
static bool physical(void *opaque, uint32_t address, uint32_t *out)
{
    (void)opaque; translations++;
    if (bad_translation) return false;
    *out = (address & 4095) + ((address & ~4095u) ^ 0x100000u); return true;
}
static void matrix(float *out)
{
    memset(out, 0, 64); for (unsigned i = 0; i < 16; i += 5) out[i] = 1;
}
static PgraphSunScene make_scene(void)
{
    PgraphSunScene scene = {0};
    matrix(scene.view); matrix(scene.projection); matrix(scene.inverse_view_projection);
    scene.light_travel[1] = -1; return scene;
}
static void camera(void)
{
    uint32_t device = 0x200000;
    memcpy(ram + pgraph_sun_doaxvb_us.device_address, &device, 4);
    PgraphSunScene s = make_scene();
    memcpy(ram + device + 0x810, s.view, 64);
    memcpy(ram + device + 0x850, s.projection, 64);
    const float sun[3] = {0,1,0}; memcpy(ram + pgraph_sun_doaxvb_us.sun_address, sun, 12);
}
static void fifo_tests(void)
{
    PgraphSunFifo f = {0}, zero = {0}; PgraphSunMarker storage[4];
    assert(!pgraph_sun_fifo_start(false, &f, storage, 4, PGRAPH_SUN_DOAXVB_US_11106, 3));
    assert(!memcmp(&f,&zero,sizeof(f)) && !scene_reads && !translations);
    assert(!pgraph_sun_fifo_start(true,&f,storage,4,PGRAPH_SUN_UNSUPPORTED,3));
    assert(pgraph_sun_fifo_start(true,&f,storage,4,PGRAPH_SUN_DOAXVB_US_11106,3));
    camera();
    assert(pgraph_sun_fifo_publish(&f,0xffc,read_ram,physical,NULL));
    /* Header at a page end: PARAMETER translation is not header-physical+4. */
    uint32_t address; assert(physical(NULL,0x1000,&address));
    PgraphSunScene owned = storage[0].scene;
    memset(ram + 0x200810, 0, 64);
    pgraph_sun_fifo_prepare(&f,address,0,0x17fc,5);
    assert(f.current.valid && f.current.sequence == 1 && !memcmp(&f.current.scene,&owned,sizeof(owned)));
    pgraph_sun_fifo_complete(&f,0x17fc,5,false);
    assert(f.count == 1 && !f.current.valid);
    pgraph_sun_fifo_prepare(&f,address,0,0x17fc,5);
    pgraph_sun_fifo_complete(&f,0x17fc,5,true);
    assert(!f.count && f.current.valid);
    pgraph_sun_fifo_prepare(&f,0,0,0x1800,0);
    pgraph_sun_fifo_complete(&f,0x1800,0,true);
    assert(f.current.valid);
    pgraph_sun_fifo_prepare(&f,0,0,0x17fc,0);
    assert(f.current.valid); /* END callback may still capture the draw */
    pgraph_sun_fifo_complete(&f,0x17fc,0,true); assert(!f.current.valid);
    assert(pgraph_sun_fifo_publish(&f,0x2000,read_ram,physical,NULL));
    assert(physical(NULL,0x2004,&address));
    pgraph_sun_fifo_prepare(&f,address,0,0x17fc,5);
    assert(!f.current.valid && f.current.sequence == 2 && f.current.scene_epoch == 2);
    pgraph_sun_fifo_complete(&f,0x17fc,5,true);
    camera();
    for (unsigned i = 0; i < 10000; i++) {
        assert(pgraph_sun_fifo_publish(&f,0x2000,read_ram,physical,NULL));
        pgraph_sun_fifo_prepare(&f,address,0,0x17fc,5);
        assert(f.current.valid && f.current.sequence == i + 3);
        pgraph_sun_fifo_complete(&f,0x17fc,5,true);
        pgraph_sun_fifo_complete(&f,0x17fc,0,true);
    }
    assert(pgraph_sun_fifo_publish(&f,0x2000,read_ram,physical,NULL));
    assert(!pgraph_sun_fifo_publish(&f,0x2000,read_ram,physical,NULL) && f.failed);
    pgraph_sun_fifo_stop(&f); assert(!f.storage);
    assert(pgraph_sun_fifo_start(true,&f,storage,4,PGRAPH_SUN_DOAXVB_US_11106,4));
    assert(pgraph_sun_fifo_publish(&f,0x2000,read_ram,physical,NULL));
    assert(pgraph_sun_fifo_publish(&f,0x3000,read_ram,physical,NULL));
    assert(physical(NULL,0x3004,&address));
    pgraph_sun_fifo_prepare(&f,address,0,0x17fc,5); assert(f.failed && !f.current.valid);
    pgraph_sun_fifo_stop(&f);
    assert(pgraph_sun_fifo_start(true,&f,storage,1,PGRAPH_SUN_DOAXVB_US_11106,5));
    assert(pgraph_sun_fifo_publish(&f,0x2000,read_ram,physical,NULL));
    assert(!pgraph_sun_fifo_publish(&f,0x3000,read_ram,physical,NULL));
    pgraph_sun_fifo_stop(&f);
    assert(pgraph_sun_fifo_start(true,&f,storage,1,PGRAPH_SUN_DOAXVB_US_11106,6));
    bad_translation = true;
    assert(!pgraph_sun_fifo_publish(&f,0x2000,read_ram,physical,NULL));
    bad_translation = false; pgraph_sun_fifo_stop(&f);
    puts("Sun FIFO: physical parameter mapping, owned camera epochs, retry/END, 10000 ring laps, invalid cameras, overflow/reorder/reuse rejection passed");
}

static bool emit(void *opaque, const PgraphSunScene *scene, const PgraphSunCapturePacket *packet)
{ (void)opaque; assert(scene->view[0] == 1 && packet->draw.count == 3); emissions++; return !emit_failure; }

static void owner_tests(void)
{
    PgraphSunVersion versions[10] = {0}; PgraphSunTicket tickets[16] = {0}; PgraphSunOwner o = {0};
    assert(pgraph_sun_owner_start(&o,versions,10,tickets,16,3,7));
    assert(!pgraph_sun_owner_start(&o,versions,10,tickets,16,3,7));
    PgraphSunVersion buffer = {.handle=101,.offset=16,.size=48,.type=PGRAPH_SUN_BUFFER_VERSION,.ready=true};
    PgraphSunDrawResources keys = {0};
    assert(!pgraph_sun_owner_publish(&o,0,buffer,true).id);
    keys.vertex = pgraph_sun_owner_publish(&o,0,buffer,false); assert(keys.vertex.id);
    keys.pipeline = pgraph_sun_owner_publish(&o,5,(PgraphSunVersion){.type=PGRAPH_SUN_PIPELINE_VERSION,
        .handle=106,.companion=206,.ready=true},false); assert(keys.pipeline.id);
    PgraphSunResource deps[4];
    const enum PgraphSunVersionType types[4]={PGRAPH_SUN_BUFFER_VERSION,PGRAPH_SUN_IMAGE_VERSION,
        PGRAPH_SUN_SAMPLER_VERSION,PGRAPH_SUN_DESCRIPTOR_POOL_VERSION};
    for (unsigned i=0;i<4;i++) {
        deps[i] = pgraph_sun_owner_publish(&o,6+i,(PgraphSunVersion){.type=types[i],
            .handle=300+i,.companion=400+i,.size=256,.ready=true},false); assert(deps[i].id);
    }
    PgraphSunVersion material = {.type=PGRAPH_SUN_MATERIAL_VERSION,.handle=105,.ready=true,.dependency_count=4};
    memcpy(material.dependencies,deps,sizeof(deps));
    keys.material=pgraph_sun_owner_publish(&o,4,material,false); assert(keys.material.id);
    buffer.handle=103;keys.weights=pgraph_sun_owner_publish(&o,2,buffer,false);
    buffer.handle=104;keys.uv=pgraph_sun_owner_publish(&o,3,buffer,false);
    PgraphVkSunPass pass={.pipeline=HANDLE(VkPipeline,106),.layout=HANDLE(VkPipelineLayout,206),
        .stride=16,.position_bytes=16,.weight_bytes=4,
        .variant={.matrices=2,.cutout=true,.remainder=true,.weight_stride=4,.uv_stride=8}};
    PgraphVkSunDraw draw={.vertex=HANDLE(VkBuffer,101),.vertex_offset=16,.vertex_size=48,.count=3,
        .weights=HANDLE(VkBuffer,103),.weight_offset=16,.weight_size=12,
        .texcoords=HANDLE(VkBuffer,104),.uv_offset=16,.uv_size=24,
        .material=HANDLE(VkDescriptorSet,105),.alpha_reference=128,.alpha_compare=4}; matrix(draw.world_to_light);
    PgraphSunCapture c={0}; PgraphSunCapturePacket packets[2]; PgraphSunScene scene=make_scene();
    PgraphSunUse use={.lifetime=3,.scene_epoch=11,.recording=7,.main_submit=UINT32_MAX-1,.auxiliary_submit=19,.display_submit=23};
    for (unsigned mode=0;mode<5;mode++) {
        c=(PgraphSunCapture){0};
        assert(pgraph_sun_capture_begin(true,&c,packets,2,pgraph_sun_owner_callbacks(&o),PGRAPH_SUN_DOAXVB_US_11106,3,11,7,&scene));
        assert(pgraph_sun_capture_append(&c,10,&pass,&draw,&keys));
        assert(!pgraph_sun_owner_writable(&o,keys.vertex));
        for(unsigned i=0;i<4;i++)assert(!pgraph_sun_owner_writable(&o,deps[i]));
        assert(!pgraph_sun_owner_publish(&o,6,(PgraphSunVersion){.handle=999},false).id);
        assert(!pgraph_sun_owner_stop(&o));
        unsigned before=emissions;
        if(mode==0) {
            assert(!pgraph_sun_owner_recording(&o,3,8));
            assert(pgraph_sun_owner_prepare_write(&o,keys.vertex,&c) && c.failed);
        }
        else if(mode==1) {
            PgraphSunUse wrong=use;wrong.recording++;
            /* Capture identity matches, but owner recording does not. */
            c.recording=wrong.recording;
            assert(!pgraph_sun_capture_submit(&c,wrong,emit,NULL));
        } else if(mode==2) {
            versions[6].ready=false;
            assert(!pgraph_sun_capture_submit(&c,use,emit,NULL)); versions[6].ready=true;
        } else {
            emit_failure=mode==4;
            assert(pgraph_sun_capture_submit(&c,use,emit,NULL)==!emit_failure);
            emit_failure=false;
            assert(emissions==before+1 && !pgraph_sun_owner_writable(&o,keys.vertex));
            pgraph_sun_owner_retire(&o,1,19,22); assert(!pgraph_sun_owner_writable(&o,keys.vertex));
            pgraph_sun_owner_retire(&o,UINT32_MAX-2,19,23); assert(!pgraph_sun_owner_writable(&o,keys.vertex));
            pgraph_sun_owner_retire(&o,1,19,23);
        }
        assert(pgraph_sun_owner_writable(&o,keys.vertex));
        for(unsigned i=0;i<4;i++)assert(pgraph_sun_owner_writable(&o,deps[i]));
    }
    /* Old tickets and keys cannot become valid when a pool slot is reused. */
    uint64_t shared_a,shared_b;
    assert(pgraph_sun_owner_acquire(&o,PGRAPH_SUN_VERTEX,keys.vertex,&pass,&draw,&shared_a));
    PgraphSunUse first_use=use;first_use.main_submit=1;first_use.auxiliary_submit=first_use.display_submit=0;
    pgraph_sun_owner_promote(&o,shared_a,&first_use);
    assert(pgraph_sun_owner_recording(&o,3,8));
    assert(!pgraph_sun_owner_recording(&o,3,7) && !pgraph_sun_owner_recording(&o,4,9));
    assert(pgraph_sun_owner_acquire(&o,PGRAPH_SUN_VERTEX,keys.vertex,&pass,&draw,&shared_b));
    PgraphSunUse second_use=first_use;second_use.recording=8;second_use.main_submit=2;
    pgraph_sun_owner_promote(&o,shared_b,&second_use);
    assert(!pgraph_sun_owner_prepare_write(&o,keys.vertex,NULL));
    pgraph_sun_owner_retire(&o,1,0,0);assert(!pgraph_sun_owner_writable(&o,keys.vertex));
    pgraph_sun_owner_retire(&o,2,0,0);assert(pgraph_sun_owner_writable(&o,keys.vertex));
    for(unsigned missing=0;missing<4;missing++) {
        PgraphSunVersion incomplete=material;incomplete.dependency_count=0;
        for(unsigned i=0;i<4;i++)if(i!=missing)incomplete.dependencies[incomplete.dependency_count++]=deps[i];
        keys.material=pgraph_sun_owner_publish(&o,4,incomplete,false);assert(keys.material.id);
        c=(PgraphSunCapture){0};
        assert(pgraph_sun_capture_begin(true,&c,packets,2,pgraph_sun_owner_callbacks(&o),PGRAPH_SUN_DOAXVB_US_11106,3,11,7,&scene));
        assert(!pgraph_sun_capture_append(&c,10,&pass,&draw,&keys));
        for(unsigned i=0;i<10;i++)assert(!versions[i].cpu_refs && !versions[i].gpu_refs);
    }
    keys.material=pgraph_sun_owner_publish(&o,4,material,false);assert(keys.material.id);
    PgraphVkSunDraw outside=draw;outside.vertex_offset=15;
    uint64_t rejected=99;
    assert(!pgraph_sun_owner_acquire(&o,PGRAPH_SUN_VERTEX,keys.vertex,&pass,&outside,&rejected) && !rejected);
    PgraphVkSunPass wrong_layout=pass;wrong_layout.layout=HANDLE(VkPipelineLayout,207);
    assert(!pgraph_sun_owner_acquire(&o,PGRAPH_SUN_PIPELINE,keys.pipeline,&wrong_layout,&draw,&rejected));
    uint64_t ticket, old;
    assert(pgraph_sun_owner_acquire(&o,PGRAPH_SUN_VERTEX,keys.vertex,&pass,&draw,&old));
    pgraph_sun_owner_cancel(&o,old);
    assert(pgraph_sun_owner_acquire(&o,PGRAPH_SUN_VERTEX,keys.vertex,&pass,&draw,&ticket));
    assert(ticket!=old && !pgraph_sun_owner_valid(&o,old));pgraph_sun_owner_cancel(&o,ticket);
    buffer.handle=101;
    PgraphSunResource newer=pgraph_sun_owner_publish(&o,0,buffer,false);
    assert(newer.generation!=keys.vertex.generation && !pgraph_sun_owner_find(&o,keys.vertex));
    /* GPU promotion capacity is reserved across ALL pending CPU leases. */
    versions[0].gpu_refs=UINT_MAX-1;
    assert(pgraph_sun_owner_acquire(&o,PGRAPH_SUN_VERTEX,newer,&pass,&draw,&ticket));
    assert(!pgraph_sun_owner_acquire(&o,PGRAPH_SUN_VERTEX,newer,&pass,&draw,&old));
    pgraph_sun_owner_cancel(&o,ticket);versions[0].gpu_refs=0;
    assert(pgraph_sun_owner_stop(&o)); assert(!o.versions && !o.tickets);
    PgraphSunTicket tiny[1]={0};
    assert(pgraph_sun_owner_start(&o,versions,10,tiny,1,3,7));keys.vertex=newer;
    c=(PgraphSunCapture){0};
    assert(pgraph_sun_capture_begin(true,&c,packets,2,pgraph_sun_owner_callbacks(&o),PGRAPH_SUN_DOAXVB_US_11106,3,11,7,&scene));
    assert(!pgraph_sun_capture_append(&c,10,&pass,&draw,&keys));
    for(unsigned i=0;i<10;i++)assert(!versions[i].cpu_refs && !versions[i].gpu_refs);
    assert(pgraph_sun_owner_stop(&o));
    puts("Sun owner: compound content/pool pins, mutation guards, preflight domains, reserved promotion, stale keys/tickets and three-domain retirement passed");
}
int main(void) { fifo_tests(); owner_tests(); return 0; }
