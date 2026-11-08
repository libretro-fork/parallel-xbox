/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sun-shadow-capture-owner.h"

int main(void)
{
    PgraphSunCapturePacket storage[4];PgraphSunCapture c={0};Owner o=owner(&c);
    PgraphVkSunPass p=pass(true);PgraphVkSunDraw d=draw(true);PgraphSunDrawResources refs=resources();
    PgraphSunUse use={.lifetime=3,.scene_epoch=11,.recording=7,.main_submit=UINT32_MAX-1,.auxiliary_submit=19,.display_submit=23};
    unsigned schedules=0;
    const unsigned orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    for(unsigned order=0;order<6;order++) {
        c=(PgraphSunCapture){0};o=owner(&c);o.reentrant=true;
        assert(begin_capture(&c,storage,4,&o));
        assert(pgraph_sun_capture_append(&c,10,&p,&d,&refs));
        assert(pgraph_sun_capture_append(&c,20,&p,&d,&refs));
        o.versions[1].unrelated=1;
        assert(!recyclable(&o,1));assert(pgraph_sun_capture_submit(&c,use,record,&o));
        assert(o.recorded==2 && o.observed[0]==10 && o.observed[1]==20);
        assert(o.promoted==10 && !o.canceled);no_cpu_refs(&o);
        assert(!c.owner.opaque && !c.packets && !c.count);
        memset(storage,0x99,sizeof(storage));assert(!recyclable(&o,1));
        uint32_t main=UINT32_MAX-2;uint64_t aux=18,display=22;
        for(unsigned i=0;i<3;i++) {
            if(orders[order][i]==0)main=1; /* wrapping main domain */
            if(orders[order][i]==1)aux=19;
            if(orders[order][i]==2)display=23;
            retire(&o,main,aux,display);
            if(i<2)assert(o.versions[1].gpu);
        }
        no_refs(&o);assert(!recyclable(&o,1));o.versions[1].unrelated=0;assert(recyclable(&o,1));schedules++;
    }
    /* Failed acquisition rolls back partial and already queued leases. */
    for(unsigned failure=1;failure<=10;failure++)for(unsigned partial=0;partial<2;partial++) {
        c=(PgraphSunCapture){0};o=owner(&c);o.fail_acquire=failure;o.partial_failure=partial;o.reentrant=true;
        assert(begin_capture(&c,storage,4,&o));
        bool first=pgraph_sun_capture_append(&c,10,&p,&d,&refs);
        if(first)assert(!pgraph_sun_capture_append(&c,20,&p,&d,&refs));
        assert(c.failed && !o.recorded && !o.promoted);no_refs(&o);
    }
    /* A generation failure preflights the whole batch: no partial record. */
    c=(PgraphSunCapture){0};o=owner(&c);assert(begin_capture(&c,storage,4,&o));
    assert(pgraph_sun_capture_append(&c,10,&p,&d,&refs));o.versions[5].generation++;
    assert(!pgraph_sun_capture_submit(&c,use,record,&o));assert(!o.recorded && !o.promoted);no_refs(&o);
    for(unsigned failure=1;failure<=10;failure++) {
        c=(PgraphSunCapture){0};o=owner(&c);o.fail_valid=failure;
        assert(begin_capture(&c,storage,4,&o));
        assert(pgraph_sun_capture_append(&c,10,&p,&d,&refs));assert(pgraph_sun_capture_append(&c,20,&p,&d,&refs));
        assert(!pgraph_sun_capture_submit(&c,use,record,&o));
        assert(!o.recorded && !o.promoted);no_refs(&o);
    }
    /* Recording failure transfers every lease, even for later packets. */
    for(unsigned failure=1;failure<=2;failure++) {
        c=(PgraphSunCapture){0};o=owner(&c);o.fail_record=failure;assert(begin_capture(&c,storage,4,&o));
        assert(pgraph_sun_capture_append(&c,10,&p,&d,&refs));assert(pgraph_sun_capture_append(&c,20,&p,&d,&refs));
        assert(!pgraph_sun_capture_submit(&c,use,record,&o));assert(o.promoted==10 && !o.canceled);
        assert(o.versions[1].gpu);retire(&o,1,19,23);no_refs(&o);
    }
    for(unsigned failure=0;failure<6;failure++) {
        c=(PgraphSunCapture){0};o=owner(&c);assert(begin_capture(&c,storage,1,&o));
        assert(pgraph_sun_capture_append(&c,10,&p,&d,&refs));
        if(failure==0)assert(!pgraph_sun_capture_append(&c,20,&p,&d,&refs)); /* capacity */
        if(failure==1)assert(!pgraph_sun_capture_append(&c,10,&p,&d,&refs)); /* order */
        if(failure==2){PgraphSunUse changed=use;changed.lifetime++;assert(!pgraph_sun_capture_submit(&c,changed,record,&o));}
        if(failure==3){PgraphSunUse changed=use;changed.recording++;assert(!pgraph_sun_capture_submit(&c,changed,record,&o));}
        if(failure==4)assert(pgraph_sun_capture_cancel(&c));
        if(failure==5){PgraphSunUse changed=use;changed.scene_epoch++;assert(!pgraph_sun_capture_submit(&c,changed,record,&o));}
        assert(!o.recorded && !o.promoted);no_refs(&o);
        assert(!begin_capture(&c,storage,4,&o)); /* failure is sticky for this scene */
    }
    /* Borrowed mutable material and a key naming the wrong buffer decline. */
    for(unsigned failure=0;failure<2;failure++) {
        c=(PgraphSunCapture){0};o=owner(&c);assert(begin_capture(&c,storage,4,&o));
        PgraphSunDrawResources changed=refs;
        if(failure==0)o.versions[5].immutable=false;
        else changed.weights=changed.vertex;
        assert(!pgraph_sun_capture_append(&c,10,&p,&d,&changed));no_refs(&o);
    }
    /* Owned constants survive caller mutation; inactive builds acquire none. */
    c=(PgraphSunCapture){0};o=owner(&c);PgraphSunScene s=scene();PgraphSunCaptureOwner ops={&o,acquire,valid,cancel,promote,NULL};
    assert(!pgraph_sun_capture_begin(false,NULL,NULL,0,ops,PGRAPH_SUN_DOAXVB_US_11106,3,11,7,NULL));assert(!o.acquired);
    assert(!pgraph_sun_capture_begin(true,&c,storage,4,ops,PGRAPH_SUN_UNSUPPORTED,3,11,7,&s));assert(!o.acquired);
    assert(!pgraph_sun_capture_begin(true,&c,storage,4,ops,PGRAPH_SUN_DOAXVB_US_11106,UINT64_MAX,11,7,&s));
    assert(pgraph_sun_capture_begin(true,&c,storage,4,ops,PGRAPH_SUN_DOAXVB_US_11106,3,11,7,&s));
    s.view[0]=99;assert(pgraph_sun_capture_append(&c,10,&p,&d,&refs));d.world_to_light[14]=.75f;
    assert(pgraph_sun_capture_submit(&c,use,record,&o));assert(o.observed_depth[0]==.25f);
    retire(&o,1,19,23);no_refs(&o);
    /* Ordering survives batch completion, rather than restarting at zero
     * whenever storage is reused within the same recording. */
    assert(begin_capture(&c,storage,4,&o));
    assert(!pgraph_sun_capture_append(&c,10,&p,&d,&refs));no_refs(&o);
    /* Every required slot is checked, including aliased index/vertex data. */
    for(unsigned alias=0;alias<2;alias++) {
        c=(PgraphSunCapture){0};o=owner(&c);PgraphVkSunDraw indexed=draw(true);
        PgraphSunDrawResources index_refs=refs;
        indexed.index=HANDLE(VkBuffer,alias?101:102);indexed.index_size=12;indexed.index_type=VK_INDEX_TYPE_UINT32;
        if(alias)index_refs.index=index_refs.vertex;
        assert(begin_capture(&c,storage,4,&o));
        assert(pgraph_sun_capture_append(&c,10,&p,&indexed,&index_refs));
        assert(pgraph_sun_capture_submit(&c,use,record,&o));assert(o.promoted==6);
        retire(&o,1,19,23);no_refs(&o);
    }
    for(unsigned invalid=0;invalid<3;invalid++) {
        c=(PgraphSunCapture){0};o=owner(&c);assert(begin_capture(&c,storage,4,&o));
        PgraphVkSunPass broken=p;
        if(invalid==0)broken.layout=VK_NULL_HANDLE;
        if(invalid==1)broken.variant.weight_stride=0;
        if(invalid==2)broken.variant.uv_stride=0;
        assert(!pgraph_sun_capture_append(&c,10,&broken,&d,&refs));
        assert(!o.acquired);no_refs(&o);
    }
    /* Camera identity cannot change behind an existing scene epoch. A new
     * scene recovers without dropping leases already promoted by the old one. */
    c=(PgraphSunCapture){0};o=owner(&c);d=draw(true);
    assert(begin_capture(&c,storage,4,&o));assert(pgraph_sun_capture_append(&c,10,&p,&d,&refs));
    assert(pgraph_sun_capture_submit(&c,use,record,&o));assert(o.versions[1].gpu);
    s=scene();s.view[12]=1;s.inverse_view_projection[12]=-1;
    ops=(PgraphSunCaptureOwner){&o,acquire,valid,cancel,promote,NULL};
    assert(!pgraph_sun_capture_begin(true,&c,storage,4,ops,PGRAPH_SUN_DOAXVB_US_11106,3,11,7,&s));
    assert(c.failed && o.recorded==1 && o.versions[1].gpu);
    assert(pgraph_sun_capture_begin(true,&c,storage,4,ops,PGRAPH_SUN_DOAXVB_US_11106,3,12,7,&s));
    assert(pgraph_sun_capture_append(&c,20,&p,&d,&refs));
    PgraphSunUse next_scene=use;next_scene.scene_epoch=12;
    assert(pgraph_sun_capture_submit(&c,next_scene,record,&o));
    retire(&o,1,19,23);no_refs(&o);
    printf("Sun capture: %u retirement orders, transactional rollback, generation/epoch rejection, failed recording, immutable-version gates and owned constants passed\n",schedules);
    return 0;
}
