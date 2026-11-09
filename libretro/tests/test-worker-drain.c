/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); _Exit(1); } } while(0)
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <retro_spsc.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#ifdef _WIN32
#include <windows.h>
#endif
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
/* TYPES */
#define VK_SPEC_RING 256
#define VK_SPEC_READY (3 * VK_SPEC_RING)
#define VK_SPEC_ADOPTED 2
static retro_spsc_t vk_spec_out;
static bool vk_spec_started;
static PGRAPHState *vk_pre_owner;
static void vk_pre_service(PGRAPHState *pg) {(void)pg;assert(false);}
static struct { uint64_t hash; int phase; } vk_spec_track[1024];
static unsigned vk_spec_undrained;
static ShaderSource sources[64];
static void pgraph_vk_shader_source_unref(ShaderSource *source)
{
    uintptr_t address=(uintptr_t)source,base=(uintptr_t)sources;
    assert(address>=base&&address-base<sizeof(sources)&&
           (address-base)%sizeof(sources[0])==0);
    assert(retro_atomic_fetch_sub_int(&source->refs,1)>1);
}
/* GLOBALS */
#ifdef CHECKED
static unsigned read_calls;
static size_t observed_read(retro_spsc_t *q,void *data,size_t bytes)
{
    read_calls++;return retro_spsc_read(q,data,bytes);
}
#define retro_spsc_read observed_read
#endif
#if defined(CHECKED) && defined(VIEW_DRAIN)
static VkSpecDone borrowed_record;
static bool borrowed;
static size_t observed_begin(retro_spsc_t *q,const void **first,size_t *first_bytes,const void **second,size_t bytes)
{
    assert(!borrowed);
    read_calls++;
    size_t n=retro_spsc_read_begin_pair(q,first,first_bytes,second,bytes);
    if(n==sizeof(borrowed_record)) {
        memcpy(&borrowed_record,*first,*first_bytes);
        if(n>*first_bytes)memcpy((char*)&borrowed_record+*first_bytes,*second,n-*first_bytes);
        borrowed=true;
    }
    return n;
}
static void observed_end(retro_spsc_t *q,size_t bytes)
{
    if(bytes==sizeof(VkSpecDone)) {
        assert(borrowed&&vk_spec_ready_n);
        unsigned slot=vk_spec_ready_order[vk_spec_ready_n-1];
        assert(!memcmp(&vk_spec_ready[slot],&borrowed_record,sizeof(borrowed_record)));
        borrowed=false;
    }
    retro_spsc_read_end(q,bytes);
}
#define retro_spsc_read_begin_pair observed_begin
#define retro_spsc_read_end observed_end
#endif
/* FUNCTIONS */
#undef retro_spsc_read_begin_pair
#undef retro_spsc_read_end
#undef retro_spsc_read
static void (*drain[2])(void)={reference_drain,vk_spec_drain};

static void position_queue(size_t position)
{
    retro_atomic_size_init(&vk_spec_out.head,position);
    retro_atomic_size_init(&vk_spec_out.tail,position);
    vk_spec_out.cached_head=vk_spec_out.cached_tail=position;
}
static void reset(size_t position)
{
    position_queue(position);
    memset(vk_spec_ready,0,sizeof(vk_spec_ready));
    memset(vk_spec_ready_order_storage,0,sizeof(vk_spec_ready_order_storage));
    vk_spec_ready_order=vk_spec_ready_order_storage;
    vk_spec_ready_n=0;
    for(unsigned i=0;i<VK_SPEC_READY;i++)vk_spec_ready_order[i]=i;
    memset(vk_spec_track,0,sizeof(vk_spec_track));
    for(unsigned i=0;i<ARRAY_SIZE(vk_spec_track);i++) {
        vk_spec_track[i].hash=i+1;vk_spec_track[i].phase=1;
    }
    for(unsigned i=0;i<ARRAY_SIZE(sources);i++)retro_atomic_int_init(&sources[i].refs,1);
    vk_spec_started=true;vk_spec_undrained=5000;
}
static void fill_record(VkSpecDone *d,unsigned serial)
{
    memset(d,0,sizeof(*d));
    for(size_t i=0;i<sizeof(d->key);i++)((uint8_t*)&d->key)[i]=(uint8_t)(serial*19+i*31);
    d->key_hash=serial;
    d->spirv=&sources[serial%ARRAY_SIZE(sources)];
    d->state_hash=serial%3==2?(serial/3)%ARRAY_SIZE(vk_spec_track)+1:0;
}
static void own(ShaderSource *s){retro_atomic_fetch_add_int(&s->refs,1);}
static void clear_ready(void)
{
    vk_spec_ready_clear();
    for(unsigned i=0;i<ARRAY_SIZE(sources);i++)assert(retro_atomic_load_relaxed_int(&sources[i].refs)==1);
}
typedef struct Snapshot {
    VkSpecDone ready[VK_SPEC_READY];
    unsigned n,order[VK_SPEC_READY+VK_SPEC_READY_HEADROOM];
    size_t offset,head,tail,cached_head;
    unsigned undrained;
    int refs[64],phases[1024];
} Snapshot;
static void run_case(unsigned which,unsigned trial,Snapshot *snap)
{
    size_t position=trial&1?SIZE_MAX-trial*31u:vk_spec_out.mask-trial%sizeof(VkSpecDone);
    reset(position);
    unsigned initial[]={0,31,768,767};
    VkSpecDone d;
    for(unsigned i=0;i<initial[trial%4];i++) {
        fill_record(&d,trial*4096+i);own(d.spirv);reference_push(&d);
    }
    for(unsigned i=0;i<40&&vk_spec_ready_n>32;i++)pgraph_vk_shader_source_unref(vk_spec_ready_take_at(0));
    unsigned counts[]={0,1,3,769,850,32,255,768};
    unsigned count=counts[(trial/4)%ARRAY_SIZE(counts)];
    for(unsigned i=0;i<count;i++) {
        fill_record(&d,trial*4096+1024+i);own(d.spirv);
        assert(retro_spsc_write(&vk_spec_out,&d,sizeof(d))==sizeof(d));
    }
    size_t partial=trial%5==0?trial%sizeof(d):0;
    memset(&d,0xa5,sizeof(d));
    assert(retro_spsc_write(&vk_spec_out,&d,partial)==partial);
    if(trial%11==0) {
        /* Production cannot have a positive credit before startup. */
        vk_spec_started=false;unsigned saved=vk_spec_undrained;vk_spec_undrained=0;
        unsigned n=vk_spec_ready_n;
        drain[which]();
        assert(vk_spec_ready_n==n&&retro_atomic_load_relaxed_size(&vk_spec_out.tail)==position);
        vk_spec_started=true;vk_spec_undrained=saved;
    }
    drain[which]();
    assert(!retro_spsc_read_avail(&vk_spec_out));
    for(unsigned i=0;i<trial%17&&vk_spec_ready_n;i++) {
        unsigned index=(trial+i*17)%vk_spec_ready_n;
        VkSpecDone *entry=&vk_spec_ready[vk_spec_ready_order[index]];
        ShaderModuleCacheKey key=entry->key;
        ShaderSource *expected=entry->spirv;
        ShaderSource *taken=vk_spec_take_ready(&key,entry->key_hash);
        assert(taken==expected);pgraph_vk_shader_source_unref(taken);
    }
    memset(snap,0,sizeof(*snap));
    memcpy(snap->ready,vk_spec_ready,sizeof(snap->ready));
    snap->n=vk_spec_ready_n;
    memcpy(snap->order,vk_spec_ready_order_storage,sizeof(snap->order));
    snap->offset=(size_t)(vk_spec_ready_order-vk_spec_ready_order_storage);
    snap->head=retro_atomic_load_relaxed_size(&vk_spec_out.head);
    snap->tail=retro_atomic_load_relaxed_size(&vk_spec_out.tail);
    snap->cached_head=vk_spec_out.cached_head;
    snap->undrained=vk_spec_undrained;
    for(unsigned i=0;i<ARRAY_SIZE(sources);i++)snap->refs[i]=retro_atomic_load_relaxed_int(&sources[i].refs);
    for(unsigned i=0;i<ARRAY_SIZE(vk_spec_track);i++)snap->phases[i]=vk_spec_track[i].phase;
    clear_ready();
}
/* State credits are render-owned. Zero is possible only before queueing or
 * after consuming all final markers (or after quiescing cancelled work). */
static void credit_cases(void)
{
    for(unsigned which=0;which<2;which++) {
#ifdef CHECKED
        bool guarded=which!=0;
#ifdef NO_IDLE_GUARD
        guarded=false;
#endif
#endif
        reset(SIZE_MAX-79);vk_spec_undrained=0;vk_spec_started=false;
#ifdef CHECKED
        read_calls=0;
#endif
        drain[which]();
#ifdef CHECKED
        assert(!read_calls);
#endif
        vk_spec_started=true;
#ifdef CHECKED
        read_calls=0;
#endif
        for(unsigned i=0;i<1000;i++)drain[which]();
        assert(!vk_spec_ready_n&&retro_atomic_load_relaxed_size(&vk_spec_out.tail)==SIZE_MAX-79);
#ifdef CHECKED
        assert(read_calls==(guarded?0:1000));
#endif
        /* A queued or cancelled-but-not-quiesced state remains nonzero,
         * even while no worker record is available. */
        vk_spec_undrained=1;
#ifdef CHECKED
        read_calls=0;
#endif
        drain[which]();
        assert(vk_spec_undrained==1&&!vk_spec_ready_n);
#ifdef CHECKED
        assert(read_calls==1);
#endif
        for(unsigned modules=2;modules<=3;modules++) {
            VkSpecDone d;
            for(unsigned j=0;j<modules;j++) {
                fill_record(&d,j);d.state_hash=j==modules-1?1:0;
                own(d.spirv);assert(retro_spsc_write(&vk_spec_out,&d,sizeof(d))==sizeof(d));
            }
            drain[which]();
            assert(vk_spec_undrained==0&&vk_spec_ready_n==modules);
            assert(vk_spec_track[0].phase==VK_SPEC_ADOPTED&&!retro_spsc_read_avail(&vk_spec_out));
            clear_ready();
            /* A subsequent queue publication can precede its render-owned
             * credit update; this render thread cannot drain in between. */
            vk_spec_undrained=1;
        }
        /* The maximum valid admission still polls and lands every marker. */
        vk_spec_undrained=VK_SPEC_RING;
        unsigned records=0;
        for(unsigned state=0;state<VK_SPEC_RING;state++) {
            unsigned modules=2+(state&1);
            for(unsigned j=0;j<modules;j++) {
                VkSpecDone d;fill_record(&d,state*3+j);
                d.state_hash=j==modules-1?state+1:0;
                own(d.spirv);assert(retro_spsc_write(&vk_spec_out,&d,sizeof(d))==sizeof(d));
                records++;
            }
        }
        drain[which]();
        assert(vk_spec_undrained==0&&vk_spec_ready_n==records);
        for(unsigned state=0;state<VK_SPEC_RING;state++)
            assert(vk_spec_track[state].phase==VK_SPEC_ADOPTED);
        clear_ready();
        vk_spec_undrained=1;drain[which]();
        assert(vk_spec_undrained==1&&!vk_spec_ready_n);
        /* After quiescing a cancelled state there is no producer left to
         * publish; resetting its credit makes later empty drains idle. */
        vk_spec_undrained=0;
#ifdef CHECKED
        read_calls=0;
#endif
        drain[which]();
#ifdef CHECKED
        assert(read_calls==(guarded?0:1));
#endif
    }
#ifdef NO_IDLE_GUARD
    puts("State-credit copying control: legacy polls retained; pending, final markers and quiesced reset pass");
#else
    puts("State-credit checks: idle skips all queue reads; one pending/cancelled credit still polls; two/three-module final markers re-enable idle skipping");
#endif
}
static void correctness(void)
{
    Snapshot *expected=malloc(sizeof(*expected)),*actual=malloc(sizeof(*actual));assert(expected&&actual);
    for(unsigned trial=0;trial<1024;trial++) {
        run_case(0,trial,expected);run_case(1,trial,actual);
        assert(!memcmp(expected,actual,sizeof(*expected)));
    }
    free(expected);free(actual);
    puts("1024 schedules: full ready-slot bytes, FIFO/window order, eviction/adoption/clear refs, tracks, empty/stopped/partial drains and unaligned physical/size_t wrap match frozen drain");
}

/* BENCH HELPERS */
enum { RECORDS=196608 };
static unsigned concurrent_mode;
static retro_atomic_int_t started,start_gate,producer_done;
static VkSpecDone prepared[768];
static void wait_start(const char *name)
{
    pin_thread(name);retro_atomic_fetch_add_int(&started,1);
    while(!retro_atomic_load_acquire_int(&start_gate))sched_yield();
}
static void *produce(void *arg)
{
    (void)arg;wait_start("WORKER_BENCH_PRODUCER_CPU");
    for(unsigned i=0;i<RECORDS;i++) {
        VkSpecDone *d=&prepared[i%ARRAY_SIZE(prepared)];
        while(retro_spsc_write_avail(&vk_spec_out)<sizeof(*d))sched_yield();
        own(d->spirv);assert(retro_spsc_write(&vk_spec_out,d,sizeof(*d))==sizeof(*d));
    }
    retro_atomic_store_release_int(&producer_done,1);return NULL;
}
static void *consume(void *arg)
{
    (void)arg;wait_start("WORKER_BENCH_CONSUMER_CPU");
    do {
        drain[concurrent_mode]();
        for(unsigned i=0;i<vk_spec_ready_n;i++) {
            VkSpecDone *d=&vk_spec_ready[vk_spec_ready_order[i]];
            unsigned serial=(unsigned)d->key_hash;
            assert(serial<ARRAY_SIZE(prepared));
            assert(!memcmp(d,&prepared[serial],sizeof(*d)));
        }
        vk_spec_ready_clear();
    } while(!retro_atomic_load_acquire_int(&producer_done)||retro_spsc_read_avail(&vk_spec_out));
    return NULL;
}
static double concurrent(unsigned which)
{
    reset(SIZE_MAX-1245);concurrent_mode=which;vk_spec_undrained=RECORDS/3;
    retro_atomic_int_init(&started,0);retro_atomic_int_init(&start_gate,0);retro_atomic_int_init(&producer_done,0);
    pthread_t producer,consumer;
    assert(!pthread_create(&consumer,NULL,consume,NULL));assert(!pthread_create(&producer,NULL,produce,NULL));
    while(retro_atomic_load_acquire_int(&started)!=2)sched_yield();
    double a=monotonic_ns();retro_atomic_store_release_int(&start_gate,1);
    assert(!pthread_join(producer,NULL));assert(!pthread_join(consumer,NULL));
    double b=monotonic_ns();assert(!retro_spsc_read_avail(&vk_spec_out)&&!vk_spec_undrained);
    clear_ready();return (b-a)/RECORDS;
}
#ifdef BENCHMARK
static double sequential(unsigned which,unsigned records)
{
    reset(0);bool pending=records==(unsigned)-1;
    if(pending)records=0;
    unsigned count=records?65536:10000000;
    vk_spec_undrained=pending?1:count*(records/3);
    double a=monotonic_ns();
    for(unsigned i=0;i<count;i++) {
        for(unsigned j=0;j<records;j++) {
            VkSpecDone *d=&prepared[j];own(d->spirv);
            assert(retro_spsc_write(&vk_spec_out,d,sizeof(*d))==sizeof(*d));
        }
        drain[which]();
    }
    double b=monotonic_ns();clear_ready();return (b-a)/count;
}
#endif
int main(void)
{
    pin_thread("WORKER_BENCH_PRODUCER_CPU");
    assert(retro_spsc_init(&vk_spec_out,3*256*sizeof(VkSpecDone)));
    for(unsigned i=0;i<ARRAY_SIZE(prepared);i++)fill_record(&prepared[i],i);
    credit_cases();correctness();
#ifndef BENCHMARK
    concurrent(0);concurrent(1);
    puts("393216 concurrent records: complete ready bytes and ownership/markers passed; checked views also verify release-after-copy");
#else
    puts("CPU-only fixture: production/frozen drains, actual layouts and queue; source refs modeled; real marker lookup; final record validation included");
    for(unsigned round=0;round<7;round++) {
        double empty[2],pending[2],three[2],con[2];
        for(unsigned k=0;k<2;k++) {
            unsigned w=k^(round&1);empty[w]=sequential(w,0);pending[w]=sequential(w,(unsigned)-1);three[w]=sequential(w,3);con[w]=concurrent(w);
        }
        printf("round=%u empty old=%.3f current=%.3f ns/drain; pending-empty old=%.3f current=%.3f ns/drain; three old=%.3f current=%.3f ns/drain; concurrent old=%.3f current=%.3f ns/record\n",round,empty[0],empty[1],pending[0],pending[1],three[0],three[1],con[0],con[1]);
    }
#endif
    retro_spsc_free(&vk_spec_out);
}
