/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "qemu/lru.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define VSH_UBO_BINDING 0
#define PSH_UBO_BINDING 1
#define PSH_TEX_BINDING 2
typedef struct ShaderModuleInfo { unsigned refs; } ShaderModuleInfo;
typedef struct ShaderBinding {
    LruNode node; ShaderState state;
    struct { ShaderModuleInfo *module_info; } vsh,geom,psh;
} ShaderBinding;
/* TYPES */
typedef struct PGRAPHVkState {
    Lru shader_cache,shader_module_cache;
    ShaderBinding *shader_binding;
    bool use_push_constants_for_uniform_attrs;
} PGRAPHVkState;
static bool geometry,collisions;
static unsigned uniforms,ordinary,evictions;
static ShaderModuleInfo *initial[3],*module_owner;
static uint64_t xxh3_64(const uint8_t *p,size_t n, uint64_t seed __attribute__((unused))) {
    if(collisions)return 1;
    uint64_t h=1469598103934665603ull;while(n--)h=(h^*p++)*1099511628211ull;return h;
}
bool pgraph_glsl_need_geom(const GeomState *s){(void)s;return geometry;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo *m){assert(m&&m->refs);m->refs++;}
static void unref(ShaderModuleInfo *m){if(m){assert(m->refs);m->refs--;}}
static void update_shader_uniform_locs(ShaderBinding *b){assert(b->vsh.module_info&&b->psh.module_info);uniforms++;}
static void ordinary_init(Lru *l,LruNode *node,const void *key){
    ShaderBinding *b=container_of(node,ShaderBinding,node);memcpy(&b->state,key,sizeof(b->state));
    b->vsh.module_info=initial[0];b->geom.module_info=initial[1];b->psh.module_info=initial[2];ordinary++;
}
static bool ordinary_compare(Lru *l,LruNode *node,const void *key){return memcmp(&container_of(node,ShaderBinding,node)->state,key,sizeof(ShaderState));}
static void binding_evict(Lru *l,LruNode *node){
    ShaderBinding *b=container_of(node,ShaderBinding,node);unref(b->vsh.module_info);unref(b->geom.module_info);unref(b->psh.module_info);evictions++;
}
static void module_init(Lru *l,LruNode *node,const void *key){ShaderModuleCacheEntry *e=container_of(node,ShaderModuleCacheEntry,node);memcpy(&e->key,key,sizeof(e->key));e->module_info=module_owner;}
static bool module_compare(Lru *l,LruNode *node,const void *key){return memcmp(&container_of(node,ShaderModuleCacheEntry,node)->key,key,sizeof(ShaderModuleCacheKey));}
static void module_evict(Lru *l,LruNode *node){unref(container_of(node,ShaderModuleCacheEntry,node)->module_info);}
/* PRODUCTION */
int main(void){
    PGRAPHVkState *r=calloc(1,sizeof(*r));assert(r);
    unsigned cases=0;
    for(unsigned gen=1;gen<=8;gen++)for(unsigned bits=0;bits<512;bits++){
        ShaderBinding entries[3]={0};ShaderModuleCacheEntry modules[1]={0};
        ShaderModuleInfo owners[4]={{1},{1},{1},{1}};
        ShaderState full={0},state,expected;
        geometry=!!(bits&32);collisions=!!(gen&1);uniforms=ordinary=evictions=0;
        full.vsh.programmable.program_data[0][0]=gen;full.psh.final_inputs_0=gen;full.psh.shader_stage_program=PS_TEXTUREMODES_PROJECT2D;
        state=full;state.psh.combiner_dynamic=true;state.psh.alpha_dynamic=true;state.psh.final_inputs_0=0;
        if(bits&8)state.vsh.programmable.program_data[0][0]^=0x8000;
        if(bits&16)state.psh.depth_format=DEPTH_FORMAT_UI_CLAMP;
        expected=state;
        initial[0]=bits&64?NULL:&owners[0];initial[1]=geometry&&!(bits&128)?&owners[1]:NULL;initial[2]=&owners[2];
        lru_init(&r->shader_cache);lru_init(&r->shader_module_cache);
        r->shader_cache.init_node=ordinary_init;r->shader_cache.compare_nodes=ordinary_compare;r->shader_cache.post_node_evict=binding_evict;
        r->shader_module_cache.init_node=module_init;r->shader_module_cache.compare_nodes=module_compare;r->shader_module_cache.post_node_evict=module_evict;
        lru_add_free(&r->shader_cache,&entries[0].node);
        if(bits&1)lru_add_free(&r->shader_cache,&entries[1].node);
        if((bits&257)==257)lru_add_free(&r->shader_cache,&entries[2].node);
        uint64_t fh=xxh3_64((const uint8_t *)&full,sizeof(full), 0);
        r->shader_binding=container_of(lru_lookup(&r->shader_cache,fh,&full),ShaderBinding,node);
        ShaderModuleCacheKey keys[3];int n=vk_state_module_keys(&state,r->use_push_constants_for_uniform_attrs,keys);
        ShaderModuleCacheKey *key=&keys[n-1];uint64_t mh=xxh3_64((const uint8_t *)key,sizeof(*key), 0);
        if(bits&4)key->psh.glsl_opts.tex_binding++;
        module_owner=bits&2?NULL:&owners[3];lru_add_free(&r->shader_module_cache,&modules[0].node);
        lru_lookup(&r->shader_module_cache,mh,key);
        uint64_t sh=xxh3_64((const uint8_t *)&state,sizeof(state), 0);
        /* An exact incomplete shader-cache entry must remain untouched. */
        if((bits&256)&&(bits&1)){
            initial[0]=NULL;initial[1]=NULL;initial[2]=NULL;
            lru_lookup(&r->shader_cache,sh,&state);ordinary--;
        }
        bool ok=(bits&1)&&!(bits&(2|4|8|16|64|256))&&(!geometry||!(bits&128));
        unsigned before=ordinary;
        ShaderBinding *b=pgraph_vk_materialize_ready_shader_binding(r,&state,sh);
        assert((b!=NULL)==ok);assert(ordinary==before&&evictions==0&&uniforms==ok);
        if(ok){
            assert(b!=r->shader_binding&&!memcmp(&b->state,&state,sizeof(state)));
            assert(owners[0].refs==2&&owners[3].refs==2&&(!geometry||owners[1].refs==2));
            assert(b->vsh.module_info==&owners[0]&&b->geom.module_info==(geometry?&owners[1]:NULL)&&b->psh.module_info==&owners[3]);
            assert(pgraph_vk_materialize_ready_shader_binding(r,&state,sh)==b);
            assert(owners[0].refs==2&&owners[3].refs==2&&uniforms==1);
            memset(&state,0xcd,sizeof(state));assert(!memcmp(&b->state,&expected,sizeof(expected)));
            lru_flush(&r->shader_module_cache);assert(owners[3].refs==1);
            lru_evict_node(&r->shader_cache,&r->shader_binding->node);
            assert(owners[0].refs==1&&owners[2].refs==0);
            assert(!memcmp(&b->state,&expected,sizeof(expected))&&b->psh.module_info->refs==1);
            lru_flush(&r->shader_cache);assert(owners[0].refs==0&&owners[3].refs==0&&(!geometry||owners[1].refs==0));
        }else{
            assert(owners[0].refs==1&&owners[1].refs==1&&owners[2].refs==1&&owners[3].refs==1);
            lru_flush(&r->shader_cache);lru_flush(&r->shader_module_cache);
        }
        cases++;
    }
    printf("Ready shader binding: %u native-LRU collision, fragment option, prefix/clamp, pressure, partial-entry, borrowed-input and owner-eviction cases PASS; final state snapshot=%zu, fragment lookup metadata=%zu, scoped input=%zu bytes\n",cases,sizeof(ShaderState),sizeof(ShaderModuleCacheKey),sizeof(ReadyBindingInput));
    free(r);
}
