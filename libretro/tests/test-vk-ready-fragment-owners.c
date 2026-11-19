/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/vk/glsl.h"
extern const UniformInfo PshUniformInfo[PshUniform__COUNT];
/* CONSTANTS */
typedef struct ShaderModuleInfo {
    unsigned refs, ready_fragment_index;
    bool uniform_valid, uniform_staged_valid;
    ShaderUniformLayout uniforms, push_constants;
} ShaderModuleInfo;
typedef struct { bool free; } LruNode;
typedef struct { unsigned returned; } Lru;
typedef struct {
    LruNode node;
    struct { unsigned kind,native_kind; struct { PshState state; GenPshGlslOptions glsl_opts; unsigned ready_kind,ready_version; } psh; } key;
    ShaderModuleInfo *module_info;
} ShaderModuleCacheEntry;
typedef struct { struct {bool cubemap;unsigned dimensionality;}state;} TextureKey;
typedef struct {TextureKey key;VkImageView image_view;VkSampler sampler;VkImageCreateInfo image_ci;}TextureBinding;
typedef struct {PshState psh;}ShaderState;
typedef struct { ShaderState state; bool shader_object_route,ready_vertex_route,ready_fragment_route;struct{ShaderModuleInfo*module_info;}psh; }ShaderBinding;
typedef struct PGRAPHVkState {
    struct {bool enabled;}shader_objects;
    struct {bool ready,route;ShaderModuleCacheEntry*stages;}ready_fragment;
    struct {bool route;}ready_vertex;
    Lru shader_module_cache;
    TextureBinding *texture_bindings[4],dummy_texture;
    ShaderBinding *shader_binding;
    bool sibling_retry,shader_bindings_changed;
    unsigned uniforms_changed;
}PGRAPHVkState;
struct PGRAPHState {
    PGRAPHVkState *vk_renderer_state;
    uint32_t control,rgb[8],alpha[8],rgb_out[8],alpha_out[8],final0,final1;
    ShaderState live;
    bool dirty;
};
static unsigned calls,made,destroyed,sources,captures,lookups;
static int fail_at=-1,bad_at=-1,bad_field;
static ShaderBinding landing;
static uint32_t pgraph_reg_r(PGRAPHState*pg,unsigned reg){
    if(reg==NV_PGRAPH_COMBINECTL)return pg->control;
    if(reg==NV_PGRAPH_COMBINESPECFOG0)return pg->final0;
    if(reg==NV_PGRAPH_COMBINESPECFOG1)return pg->final1;
    const unsigned base[4]={NV_PGRAPH_COMBINECOLORI0,NV_PGRAPH_COMBINEALPHAI0,NV_PGRAPH_COMBINECOLORO0,NV_PGRAPH_COMBINEALPHAO0};
    const uint32_t*values[4]={pg->rgb,pg->alpha,pg->rgb_out,pg->alpha_out};
    for(unsigned i=0;i<4;i++)if(reg>=base[i]&&reg<base[i]+32&&!(reg&3))return values[i][(reg-base[i])/4];
    abort();
}
static void lru_add_free(Lru*l,LruNode*n){assert(!n->free);n->free=true;l->returned++;}
static void pgraph_vk_ref_shader_module(ShaderModuleInfo*m){assert(m);m->refs++;}
static void pgraph_vk_unref_shader_module(PGRAPHVkState*r,ShaderModuleInfo*m){(void)r;assert(m&&m->refs);if(!--m->refs){free((void*)m->uniforms.uniforms);free((void*)m->push_constants.uniforms);free(m->uniforms.allocation);free(m);destroyed++;}}
static ShaderModuleInfo *pgraph_vk_create_optional_fragment_module(PGRAPHVkState*r,const char*text){
    assert(r->shader_objects.enabled&&!r->ready_fragment.ready);unsigned index=calls++;
    GlslcOptions opts={.emit_layout=true};GlslcResult result={0};assert(glslc_compile(GLSLC_STAGE_FRAGMENT,text,&opts,&result));sources++;
    ShaderModuleInfo*m=NULL;if((int)index!=fail_at){m=calloc(1,sizeof(*m));assert(m);made++;m->uniforms=(ShaderUniformLayout){result.uniforms.members,result.uniforms.count,result.uniforms.size,calloc(1,result.uniforms.size+16)};m->push_constants=(ShaderUniformLayout){result.push_constants.members,result.push_constants.count,result.push_constants.size,NULL};
        if((int)index==bad_at){if(bad_field==0)m->uniforms.total_size--;else if(bad_field==1)m->push_constants.total_size=4;else if(bad_field==2)((GlslcUniform*)m->uniforms.uniforms)[0].offset++;else if(bad_field==3)((GlslcUniform*)m->uniforms.uniforms)[m->uniforms.num_uniforms-1].offset++;else if(bad_field==4)((GlslcUniform*)m->uniforms.uniforms)[m->uniforms.num_uniforms-1].dim_a--;else m->uniforms.num_uniforms--;}
    }else{free(result.uniforms.members);free(result.push_constants.members);}glslc_result_free_words(&result);sources--;return m;
}
/* PRODUCTION */
static bool pgraph_glsl_check_shader_state_dirty(PGRAPHState*pg,const ShaderState*state){(void)state;return pg->dirty;}
static ShaderState pgraph_glsl_get_shader_state(PGRAPHState*pg){captures++;pg->dirty=false;return pg->live;}
static uint64_t xxh3_64(const uint8_t*p,size_t n,unsigned seed){(void)p;(void)n;(void)seed;return 3;}
static ShaderBinding*get_shader_binding_for_state(PGRAPHVkState*r,const ShaderState*state,uint64_t hash){(void)hash;ShaderState saved=*state;/* Poison the old owner exactly when the cache initializer may evict it. */if(r->shader_binding)memset(&r->shader_binding->state,0xff,sizeof(ShaderState));landing.state=saved;landing.shader_object_route=true;landing.ready_vertex_route=r->ready_vertex.route;landing.ready_fragment_route=r->ready_fragment.route;lookups++;return &landing;}
static void bind_route(PGRAPHState*pg){PGRAPHVkState*r=pg->vk_renderer_state;
/* BIND_ROUTE */
}
static PshState eligible(unsigned index){return(PshState){.smooth_shading=!!(index&1),.depth_format=index&2?DEPTH_FORMAT_HELPER_LIVENESS:DEPTH_FORMAT_D24,.surface_zeta_format=NV097_SET_SURFACE_FORMAT_ZETA_Z24S8,.depth_clipping=true,.final_inputs_0=0x04200000,.final_inputs_1=0x1400};}
static void setup(PGRAPHVkState*r,ShaderModuleCacheEntry entries[4]){memset(r,0,sizeof(*r));memset(entries,0,4*sizeof(*entries));r->shader_objects.enabled=true;r->ready_fragment.stages=entries;calls=made=destroyed=0;assert(!sources);r->dummy_texture.image_view=(VkImageView)(uintptr_t)1;r->dummy_texture.sampler=(VkSampler)(uintptr_t)2;for(unsigned i=0;i<4;i++)r->texture_bindings[i]=&r->dummy_texture;}
static void live(PGRAPHState*pg,PshState s){pg->live.psh=s;pg->control=s.combiner_control;pg->final0=s.final_inputs_0;pg->final1=s.final_inputs_1;memcpy(pg->rgb,s.rgb_inputs,32);memcpy(pg->alpha,s.alpha_inputs,32);memcpy(pg->rgb_out,s.rgb_outputs,32);memcpy(pg->alpha_out,s.alpha_outputs,32);}
int main(void){PGRAPHVkState r;ShaderModuleCacheEntry entries[4];setup(&r,entries);assert(vk_ready_fragment_init(&r)&&r.ready_fragment.ready&&made==4&&calls==4);
    for(unsigned i=0;i<4;i++){ShaderModuleInfo*m=entries[i].module_info;assert(m->refs==1&&m->ready_fragment_index==i+1&&entries[i].key.native_kind==1&&entries[i].key.psh.ready_kind==1&&entries[i].key.psh.ready_version==VK_READY_FRAGMENT_VERSION);PshState a=eligible(i);ShaderBinding binding={.state.psh=a,.psh.module_info=m};uint8_t*bytes=m->uniforms.allocation;memset(bytes,0xa5,VK_READY_FRAGMENT_TOTAL+16);m->uniform_valid=true;assert(vk_ready_fragment_parameters(&binding));uint8_t saved[32];memcpy(saved,bytes+VK_READY_FRAGMENT_OFFSET,32);for(unsigned j=0;j<VK_READY_FRAGMENT_OFFSET;j++)assert(bytes[j]==0xa5);for(unsigned j=VK_READY_FRAGMENT_TOTAL;j<VK_READY_FRAGMENT_TOTAL+16;j++)assert(bytes[j]==0xa5);m->uniform_staged_valid=true;assert(!vk_ready_fragment_parameters(&binding)&&m->uniform_staged_valid);binding.state.psh.shader_stage_program=1;binding.state.psh.dim_tex[0]=2;binding.state.psh.rect_tex[0]=true;assert(vk_ready_fragment_parameters(&binding)&&!m->uniform_staged_valid);binding.state.psh=a;assert(vk_ready_fragment_parameters(&binding)&&!memcmp(saved,bytes+VK_READY_FRAGMENT_OFFSET,32));assert(vk_ready_fragment_find(&r,&a)==m);}
    PGRAPHState pg={.vk_renderer_state=&r};PshState good=eligible(0);live(&pg,good);assert(vk_ready_fragment_route(&pg,&good));good.combiner_dynamic=true;assert(!vk_ready_fragment_route(&pg,&good));good.combiner_dynamic=false;
    good.z_perspective=true;assert(!vk_ready_fragment_route(&pg,&good));good.z_perspective=false;
    TextureBinding actual={.key.state.dimensionality=2,.image_view=(VkImageView)(uintptr_t)3,.sampler=(VkSampler)(uintptr_t)4,.image_ci={.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT}};
    for(unsigned i=0;i<4;i++){r.texture_bindings[i]=&actual;assert(vk_ready_fragment_route(&pg,&good));actual.image_ci.format=VK_FORMAT_R8G8B8A8_UINT;assert(!vk_ready_fragment_route(&pg,&good));actual.image_ci.format=VK_FORMAT_R8G8B8A8_UNORM;actual.key.state.cubemap=true;assert(!vk_ready_fragment_route(&pg,&good));actual.key.state.cubemap=false;actual.key.state.dimensionality=3;assert(!vk_ready_fragment_route(&pg,&good));actual.key.state.dimensionality=2;r.texture_bindings[i]=NULL;assert(!vk_ready_fragment_route(&pg,&good));r.texture_bindings[i]=&r.dummy_texture;}
    r.shader_objects.enabled=false;assert(!vk_ready_fragment_route(&pg,&good));r.shader_objects.enabled=true;
    for(unsigned unsupported=0;unsupported<2;unsupported++) {
        ShaderBinding cached={.state.psh=good,.shader_object_route=true};
        PshState fresh=good;
        if(unsupported)fresh.combiner_dynamic=true;else fresh.z_perspective=true;
        r.shader_binding=&cached;r.ready_fragment.route=false;
        captures=lookups=0;live(&pg,fresh);pg.dirty=false;
        bind_route(&pg);
        assert(!r.ready_fragment.route&&!r.shader_binding->ready_fragment_route);
        assert(captures==1&&lookups==1&&r.shader_bindings_changed);
        assert(!memcmp(&r.shader_binding->state.psh,&fresh,sizeof(fresh)));
    }
    ShaderBinding old={.state.psh=good,.shader_object_route=true,.ready_fragment_route=false};r.shader_binding=&old;captures=lookups=0;live(&pg,good);pg.dirty=false;bind_route(&pg);assert(r.ready_fragment.route&&captures==1&&lookups==1&&r.shader_bindings_changed);r.shader_bindings_changed=false;bind_route(&pg);assert(captures==1&&lookups==1&&!r.shader_bindings_changed);
    PshState unsafe=good;unsafe.combiner_control=1;unsafe.rgb_inputs[0]=14;live(&pg,unsafe);pg.dirty=false;bind_route(&pg);assert(!r.ready_fragment.route&&captures==2&&lookups==2);live(&pg,good);pg.dirty=false;bind_route(&pg);assert(r.ready_fragment.route&&captures==3&&lookups==3);bind_route(&pg);assert(captures==3&&lookups==3);
    for(unsigned count=0;count<=8;count++) {
        for(unsigned stage=0;stage<8;stage++) {
            live(&pg,good);pg.control=count;pg.rgb[stage]=14;
            assert(vk_ready_fragment_live_program(&pg)==(stage>=count));
            pg.rgb[stage]=0;pg.rgb_out[stage]=1;
            assert(vk_ready_fragment_live_program(&pg)==(stage>=count));
        }
    }
    pg.control=9;assert(!vk_ready_fragment_live_program(&pg));
    live(&pg,good);pg.final1=14u<<8;assert(!vk_ready_fragment_live_program(&pg));
    ShaderModuleInfo*held=entries[2].module_info;pgraph_vk_ref_shader_module(held);vk_ready_fragment_finalize(&r);assert(!r.ready_fragment.ready&&!r.ready_fragment.stages&&destroyed==3&&held->refs==1);pgraph_vk_unref_shader_module(&r,held);assert(destroyed==4);
    unsigned failures=0;for(int position=0;position<4;position++)for(int fault=0;fault<7;fault++){setup(&r,entries);fail_at=fault==0?position:-1;bad_at=fault?position:-1;bad_field=fault-1;assert(!vk_ready_fragment_init(&r)&&!r.ready_fragment.ready&&!r.ready_fragment.stages&&r.shader_module_cache.returned==4&&made==destroyed&&!sources);vk_ready_fragment_decline(&r);assert(r.shader_module_cache.returned==4);failures++;}fail_at=bad_at=-1;
    puts("Prepared Vulkan fragment owners: four real compiled layouts/selectors,32-byte guarded A/B/A snapshots,all4 borrowed-view admission,active live input/output/final guards,dirty-free bidirectional route edges,held-consumer lifetime and28 startup failure/layout rollback cases PASS");assert(failures==28);return 0;
}
