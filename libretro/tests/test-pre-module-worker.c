/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <vulkan/vulkan.h>
#include <retro_atomic.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define VSH_UBO_BINDING 0
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef struct ShaderSource {unsigned refs;} ShaderSource;
typedef struct ShaderModuleInfo {unsigned refs;} ShaderModuleInfo;
typedef struct Lru Lru;
typedef struct LruNode {uint64_t hash;} LruNode;
struct Lru {void (*init_node)(Lru *,LruNode *,const void *);};
typedef struct PGRAPHVkState {bool use_push_constants_for_uniform_attrs;Lru shader_module_cache;} PGRAPHVkState;
struct PGRAPHState {PGRAPHVkState *vk_renderer_state;};
/* TYPES */
/* GLOBALS */
static retro_atomic_int_t vk_spec_cancel,vk_spec_inflight;
static int vk_spec_ec,vk_spec_idle_ec;
static bool vk_spec_started;
typedef struct {uint64_t state_hash;} VkSpecDone;
static int vk_spec_out;
static struct {int phase;} vk_spec_track[1];
static unsigned vk_spec_undrained;
#define VK_SPEC_ADOPTED 2
static size_t retro_spsc_read(int *q,void *data,size_t bytes) {assert(q==&vk_spec_out && data && bytes==sizeof(VkSpecDone));return 0;}
static void vk_spec_ready_push(const VkSpecDone *d) {(void)d;assert(false);}
static int vk_spec_find(uint64_t hash) {(void)hash;return -1;}
static ShaderSource sources[VK_PRE_REQUESTS][2];
static ShaderModuleInfo modules[VK_PRE_REQUESTS][2];
static ShaderModuleCacheKey expected[VK_PRE_REQUESTS][2];
static VkPrewarmState expected_pipeline[VK_PRE_REQUESTS];
static _Thread_local bool in_worker;
static bool accept_pipeline,cache_hit,start_ok=true,collide,stress;
static unsigned compiles,adoptions,transfers,work_wakes,idle_wakes,starts;
static bool need_geometry;
bool pgraph_glsl_need_geom(const GeomState *s) {(void)s;return need_geometry;}
static uint64_t fast_hash(const uint8_t *bytes,size_t count) {
    if(collide)return 1;
    uint64_t h=UINT64_C(1469598103934665603);
    for(size_t i=0;i<count;i++)h=(h^bytes[i])*UINT64_C(1099511628211);
    return h;
}
static bool vk_spec_start(void) {starts++;if(start_ok)vk_spec_started=true;return start_ok;}
static void shader_module_cache_entry_init(Lru *l,LruNode *n,const void *k) {(void)l;(void)n;(void)k;assert(false);}
static void shader_module_cache_entry_init_pre(Lru *,LruNode *,const void *);
static void pgraph_vk_shader_source_unref(ShaderSource *s) {assert(s && s->refs);s->refs--;}
static void pgraph_vk_unref_shader_module(PGRAPHVkState *r,ShaderModuleInfo *m) {assert(r && m && m->refs);m->refs--;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m) {assert(m);m->refs++;}
static ShaderModuleInfo *pgraph_vk_create_shader_module_from_spirv(PGRAPHVkState *r,ShaderSource *source) {
    assert(r && source && source->refs==1);
    for(unsigned j=0;j<VK_PRE_REQUESTS;j++)for(unsigned i=0;i<2;i++)if(source==&sources[j][i]) {
        assert(!modules[j][i].refs);pgraph_vk_shader_source_unref(source);return &modules[j][i];
    }
    assert(false);return NULL;
}
static ShaderSource *vk_spec_compile_module(const ShaderModuleCacheKey *key,uint64_t hash) {
    assert(in_worker && key->kind!=VK_SHADER_STAGE_FRAGMENT_BIT);
    assert(hash==fast_hash((const uint8_t *)key,sizeof(*key)));
    for(unsigned j=0;j<VK_PRE_REQUESTS;j++)for(unsigned i=0;i<vk_pre_requests[j].count;i++) {
        if(key==&vk_pre_requests[j].keys[i]) {
            if(!stress)assert(!memcmp(key,&expected[j][i],sizeof(*key)));
            else {sched_yield();assert(hash==fast_hash((const uint8_t *)key,sizeof(*key)));}
            assert(retro_atomic_load_acquire_int(&vk_pre_requests[j].phase)==VK_PRE_QUEUED);
            sources[j][i].refs++;compiles++;return &sources[j][i];
        }
    }
    assert(false);return NULL;
}
static ShaderSource *vk_pre_take_ready(const ShaderModuleCacheKey *,uint64_t);
static ShaderModuleInfo *get_and_ref_shader_module_for_key(PGRAPHVkState *r,const ShaderModuleCacheKey *key) {
    assert(r && !in_worker && vk_pre_ready_lend);
    assert(r->shader_module_cache.init_node==shader_module_cache_entry_init_pre);
    VkPreRequest *request=vk_pre_ready_lend;
    unsigned j=(unsigned)(request-vk_pre_requests),i=(unsigned)(key-request->keys);
    assert(j<VK_PRE_REQUESTS && i<request->count && request->words[i] && request->words[i]->refs==1);
    if(!cache_hit) {
        ShaderModuleCacheEntry entry={.node={.hash=request->hashes[i]}};
        r->shader_module_cache.init_node(&r->shader_module_cache,&entry.node,key);
        assert(entry.module_info==&modules[j][i] && !memcmp(&entry.key,key,sizeof(*key)));
        assert(entry.module_info->refs==1 && !request->words[i]);
        pgraph_vk_ref_shader_module(entry.module_info);
        /* Reclaim the fixture's cache owner immediately: the caller ref
         * must pin the module through later pipeline backpressure. */
        pgraph_vk_unref_shader_module(r,entry.module_info);
    } else {assert(!modules[j][i].refs);pgraph_vk_ref_shader_module(&modules[j][i]);}
    adoptions++;return &modules[j][i];
}
static bool pgraph_vk_pre_library_ready(PGRAPHState *pg,const VkPrewarmState *pipeline,ShaderModuleInfo *vsh,ShaderModuleInfo *geom) {
    assert(pg && !in_worker);
    VkPreRequest *request=(VkPreRequest *)((char *)pipeline - offsetof(VkPreRequest,pipeline));
    unsigned j=(unsigned)(request-vk_pre_requests);assert(j<VK_PRE_REQUESTS);
    if(!stress)assert(!memcmp(pipeline,&expected_pipeline[j],sizeof(*pipeline)));
    else assert(pipeline->push_size==request->keys[request->count-1].vsh.state.programmable.program_data[0][0]%8*16);
    assert(vsh==&modules[j][request->count-1] && vsh->refs==1);
    assert((request->count==2)==!!geom);
    if(!accept_pipeline)return false;
    pgraph_vk_unref_shader_module(pg->vk_renderer_state,vsh);
    if(geom)pgraph_vk_unref_shader_module(pg->vk_renderer_state,geom);
    transfers++;return true;
}
static void retro_eventcount_notify(int *event) {
    if(event==&vk_spec_ec) {assert(!in_worker);work_wakes++;}
    else {assert(event==&vk_spec_idle_ec && in_worker);idle_wakes++;}
}
/* PRODUCTION */
static void clean_owners(void) {
    for(unsigned j=0;j<VK_PRE_REQUESTS;j++)for(unsigned i=0;i<2;i++)assert(!sources[j][i].refs && !modules[j][i].refs);
    assert(!vk_pre_ready_lend && !vk_pre_live && !vk_pre_owner);
}
static void *stress_worker(void *unused) {
    (void)unused;in_worker=true;
    for(unsigned n=0;n<8192;) {if(vk_pre_process_input())n++;else sched_yield();}
    return NULL;
}
static void duplicate_matrix(PGRAPHState *pg) {
    PGRAPHVkState *r=pg->vk_renderer_state;unsigned cases=0;
    for(unsigned generation=0;generation<128;generation++)for(unsigned mode=0;mode<4;mode++)for(unsigned variant=0;variant<12;variant++) {
        clean_owners();assert(retro_atomic_load_acquire_int(&vk_spec_inflight)==0);
        unsigned base=generation&1?UINT_MAX-1:generation*17;
        vk_pre_write_local=base;retro_atomic_store_relaxed_int(&vk_pre_write,(int)base);retro_atomic_store_relaxed_int(&vk_pre_read,(int)base);
        retro_atomic_store_relaxed_int(&vk_spec_cancel,0);accept_pipeline=false;cache_hit=false;collide=mode&1;need_geometry=true;
        ShaderState state={0};state.vsh.programmable.program_data[0][0]=generation;state.vsh.uniform_attrs=5;
        VkPrewarmState pipeline={0};pipeline.render_pass.color_format=VK_FORMAT_R8G8B8A8_UNORM;pipeline.push_size=32;
        assert(pgraph_vk_queue_pre_modules(pg,&state,&pipeline));
        memcpy(expected[0],vk_pre_requests[0].keys,sizeof(expected[0]));expected_pipeline[0]=pipeline;
        if(mode&2) {in_worker=true;assert(vk_pre_process_input());in_worker=false;vk_pre_service(pg);assert(retro_atomic_load_acquire_int(&vk_pre_requests[0].phase)==VK_PRE_ADOPTED);}
        unsigned writes=vk_pre_write_local,wakes=work_wakes,live=vk_pre_live;
        switch(variant) {
        case 0:break;
        case 1:state.vsh.programmable.program_data[0][0]^=0x100;break;
        case 2:state.geom.primitive_mode^=1;break;
        case 3:pipeline.render_pass.color_format=VK_FORMAT_R5G6B5_UNORM_PACK16;break;
        case 4:pipeline.raster.cullMode=VK_CULL_MODE_BACK_BIT;break;
        case 5:pipeline.ia.topology=VK_PRIMITIVE_TOPOLOGY_LINE_LIST;break;
        case 6:pipeline.dynamic_states[0]=VK_DYNAMIC_STATE_SCISSOR;break;
        case 7:pipeline.num_dynamic=1;break;
        case 8:pipeline.push_size=48;break;
        case 9:((unsigned char *)&state.psh)[0]^=1;break;
        case 10:need_geometry=false;break;
        case 11:pipeline.render_pass.zeta_format=VK_FORMAT_D16_UNORM;break;
        }
        bool same=variant==0 || variant==9;
        assert(pgraph_vk_queue_pre_modules(pg,&state,&pipeline));
        assert(vk_pre_write_local==writes+!same && work_wakes==wakes+!same && vk_pre_live==live+!same);
        assert(vk_pre_requests[1].count==(same?0u:1u+need_geometry));
        /* Poison caller storage while the original and any distinct queued
         * request still own their exact immutable metadata. */
        memset(&state,0xcd,sizeof(state));memset(&pipeline,0xef,sizeof(pipeline));
        assert(!memcmp(vk_pre_requests[0].keys,expected[0],sizeof(expected[0])));
        assert(!memcmp(&vk_pre_requests[0].pipeline,&expected_pipeline[0],sizeof(pipeline)));
        retro_atomic_store_release_int(&vk_spec_cancel,1);in_worker=true;while(vk_pre_process_input()){}in_worker=false;
        assert(retro_atomic_load_acquire_int(&vk_spec_inflight)==0);vk_pre_clear(r);clean_owners();cases++;
    }
    retro_atomic_store_relaxed_int(&vk_spec_cancel,0);collide=false;accept_pipeline=false;
    printf("Pre-module duplicates: %u queued/adopted, exact/colliding keys, changed raster/pass/range/dynamic state, poisoned caller, cancellation and rollover cases PASS; one queue publication per unique request\n",cases);
}
int main(void) {
    PGRAPHVkState r={.shader_module_cache={.init_node=shader_module_cache_entry_init}};PGRAPHState pg={&r};unsigned cases=0;
    duplicate_matrix(&pg);
    for(unsigned generation=0;generation<128;generation++)for(unsigned bits=0;bits<64;bits++) {
        clean_owners();assert(retro_atomic_load_acquire_int(&vk_spec_inflight)==0);
        unsigned base=generation&1?UINT_MAX-4:generation*19;
        vk_pre_write_local=base;retro_atomic_store_relaxed_int(&vk_pre_write,(int)base);retro_atomic_store_relaxed_int(&vk_pre_read,(int)base);
        retro_atomic_store_relaxed_int(&vk_spec_cancel,0);compiles=adoptions=transfers=work_wakes=idle_wakes=starts=0;
        cache_hit=bits&1;collide=bits&2;r.use_push_constants_for_uniform_attrs=bits&4;
        accept_pipeline=false;
        for(unsigned j=0;j<VK_PRE_REQUESTS;j++) {
            ShaderState state={0};state.vsh.programmable.program_data[0][0]=generation*17+j;
            state.vsh.uniform_attrs=(uint16_t)(1u<<j);state.geom.primitive_mode=j;
            need_geometry=j&1;
            VkPrewarmState pipeline={0};pipeline.render_pass.color_format=VK_FORMAT_R8G8B8A8_UNORM;pipeline.push_size=j*16;
            assert(pgraph_vk_queue_pre_modules(&pg,&state,&pipeline));
            VkPreRequest *request=&vk_pre_requests[j];
            assert(request==vk_pre_ring[(base+j)%VK_PRE_REQUESTS]);
            assert(request->count==1u+need_geometry);
            memcpy(expected[j],request->keys,sizeof(expected[j]));expected_pipeline[j]=pipeline;
            memset(&state,0xce,sizeof(state));memset(&pipeline,0xdd,sizeof(pipeline));
        }
        assert(!compiles && !adoptions && work_wakes==8 && vk_pre_live==8);
        ShaderState state={0};VkPrewarmState pipeline={0};unsigned before=starts;
        assert(!pgraph_vk_queue_pre_modules(&pg,&state,&pipeline) && starts==before);
        bool cancelled=bits&8;if(cancelled)retro_atomic_store_release_int(&vk_spec_cancel,1);
        in_worker=true;for(unsigned j=0;j<VK_PRE_REQUESTS;j++)assert(vk_pre_process_input());assert(!vk_pre_process_input());in_worker=false;
        assert(idle_wakes==8 && retro_atomic_load_acquire_int(&vk_spec_inflight)==0);
        assert(compiles==(cancelled?0u:12u));
        if(!cancelled) {
            /* Equal hashes cannot lend another key's words. */
            vk_pre_ready_lend=&vk_pre_requests[0];ShaderModuleCacheKey wrong=vk_pre_requests[0].keys[0];wrong.vsh.state.uniform_attrs^=0x8000;
            assert(!vk_pre_take_ready(&wrong,vk_pre_requests[0].hashes[0]));vk_pre_ready_lend=NULL;
            if(!(bits&32)) {
                vk_spec_drain();assert(adoptions==12 && !transfers && vk_pre_live==8);
                vk_pre_service(&pg);assert(adoptions==12 && !transfers && vk_pre_live==8);
                if(!(bits&16)) {accept_pipeline=true;vk_pre_service(&pg);assert(transfers==8 && vk_pre_live==0);}
            }
        }
        vk_pre_clear(&r);clean_owners();assert(r.shader_module_cache.init_node==shader_module_cache_entry_init);retro_atomic_store_relaxed_int(&vk_spec_cancel,0);cases++;
    }
    start_ok=false;ShaderState state={0};VkPrewarmState pipeline={0};
    assert(!pgraph_vk_queue_pre_modules(&pg,&state,&pipeline));clean_owners();
    stress=start_ok=accept_pipeline=true;cache_hit=collide=false;r.use_push_constants_for_uniform_attrs=false;
    compiles=adoptions=transfers=work_wakes=idle_wakes=0;
    pthread_t thread;assert(!pthread_create(&thread,NULL,stress_worker,NULL));
    for(unsigned n=0;n<8192;) {
        vk_pre_service(&pg);
        unsigned slot=0;while(slot<VK_PRE_REQUESTS && retro_atomic_load_relaxed_int(&vk_pre_requests[slot].phase)!=VK_PRE_FREE)slot++;
        if(slot==VK_PRE_REQUESTS) {sched_yield();continue;}
        state=(ShaderState){0};state.vsh.programmable.program_data[0][0]=n;state.geom.primitive_mode=n;
        need_geometry=n&1;pipeline=(VkPrewarmState){0};pipeline.push_size=n%8*16;expected_pipeline[slot]=pipeline;
        accept_pipeline=false;
        if(pgraph_vk_queue_pre_modules(&pg,&state,&pipeline)) {
            unsigned writes=vk_pre_write_local,live=vk_pre_live;
            for(unsigned repeat=0;repeat<4;repeat++) {
                assert(pgraph_vk_queue_pre_modules(&pg,&state,&pipeline)==(live<VK_PRE_REQUESTS));
                assert(vk_pre_write_local==writes && vk_pre_live==live);
                sched_yield(); /* Race QUEUED -> READY -> ADOPTED publication. */
            }
            n++;
        } else sched_yield();
        accept_pipeline=true;
    }
    while(vk_pre_live){vk_pre_service(&pg);sched_yield();}
    assert(!pthread_join(thread,NULL));
    assert(compiles==12288 && adoptions==12288 && transfers==8192 && work_wakes==8192 && idle_wakes==8192);
    assert(retro_atomic_load_acquire_int(&vk_spec_inflight)==0);vk_pre_clear(&r);clean_owners();
    printf("Pre-module worker: %u eight-slot generations plus8192 threaded jobs and32768 racing duplicate/backpressure submissions, pointer-only handoff, exact keys/collisions, no fragment compile, pressure, cancellation, teardown and rollover PASS; request_bytes=%zu\n",cases,sizeof(VkPreRequest));
}
