/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define NV2A_VK_DPRINTF(...) ((void)0)
#define QTAILQ_FOREACH(n,h,f) for((n)=*(h);(n);(n)=(n)->next)
typedef struct LruNode{uint64_t hash;struct LruNode *next;}LruNode;
typedef struct Lru{LruNode *bins[1];}Lru;
typedef struct ShaderModuleInfo{unsigned identity;}ShaderModuleInfo;
typedef struct ShaderBinding{LruNode node;bool ready_vertex_route,ready_fragment_route,shader_object_route;ShaderState state;struct{ShaderModuleInfo *module_info;}vsh,geom,psh;}ShaderBinding;
typedef struct PGRAPHVkState{Lru shader_cache;struct{bool ready,route;}ready_vertex;struct{bool route;}ready_fragment;struct{bool enabled;}shader_objects;ShaderBinding *shader_binding;bool sibling_retry;unsigned uniforms_changed;struct{bool valid;}vi_last;}PGRAPHVkState;
struct PGRAPHState{PGRAPHVkState *vk_renderer_state;bool program_data_dirty;unsigned uniform_attrs;struct{float inline_value[4];}vertex_attributes[16];};
/* TYPES */
static ShaderBinding owners[4];static ShaderModuleInfo ordinary={0},prepared={1},native_module={2};static unsigned allocated,uniforms;
static void shader_cache_entry_init(Lru*,LruNode*,const void*);
static bool shader_cache_entry_compare(Lru*,LruNode*,const void*);
static void shader_cache_entry_init_modules(Lru *l,LruNode *n){(void)l;ShaderBinding *b=container_of(n,ShaderBinding,node);b->vsh.module_info=b->psh.module_info=!b->shader_object_route?&native_module:b->ready_vertex_route?&prepared:&ordinary;}
static void update_shader_uniform_locs(ShaderBinding *b){assert(b);uniforms++;}
static unsigned lru_hash_to_bin(Lru *l,uint64_t h){(void)l;(void)h;return 0;}
static LruNode *lru_lookup(Lru *l,uint64_t h,const void *key){for(LruNode *n=l->bins[0];n;n=n->next)if(n->hash==h&&!shader_cache_entry_compare(l,n,key))return n;assert(allocated<4);LruNode *n=&owners[allocated++].node;n->hash=h;shader_cache_entry_init(l,n,key);n->next=l->bins[0];l->bins[0]=n;return n;}
bool pgraph_glsl_need_geom(const GeomState *s){(void)s;return false;}
/* PRODUCTION */
int main(void){
 PGRAPHVkState r={0};r.ready_vertex.ready=true;r.shader_objects.enabled=true;PGRAPHState pg={.vk_renderer_state=&r,.uniform_attrs=1};ShaderState original={0};original.vsh.programmable.program_length=1;uint64_t hash=123;
 refresh_route(&pg);assert(r.ready_vertex.route&&pg.program_data_dirty&&(r.uniforms_changed&1)&&!r.vi_last.valid);
 ShaderBinding *finite=get_shader_binding_for_state(&r,&original,hash);assert(finite->ready_vertex_route&&finite->vsh.module_info==&prepared&&allocated==1);r.shader_binding=finite;
 uint32_t nan=0x7fc12345;memcpy(&pg.vertex_attributes[0].inline_value[0],&nan,4);pg.program_data_dirty=false;r.vi_last.valid=true;r.sibling_retry=true;r.uniforms_changed=0;refresh_route(&pg);assert(!r.ready_vertex.route&&pg.program_data_dirty&&!r.vi_last.valid&&!r.sibling_retry&&(r.uniforms_changed&1));
 ShaderBinding *exotic=get_shader_binding_for_state(&r,&original,hash);assert(exotic!=finite&&!exotic->ready_vertex_route&&exotic->vsh.module_info==&ordinary&&allocated==2);assert(finite->ready_vertex_route&&finite->vsh.module_info==&prepared&&!memcmp(&finite->state,&original,sizeof(original)));assert(pgraph_vk_ready_shader_binding(&r,&original,hash)==exotic);
 uint32_t zero=0x80000000;memcpy(&pg.vertex_attributes[0].inline_value[0],&zero,4);pg.program_data_dirty=false;refresh_route(&pg);assert(r.ready_vertex.route&&pg.program_data_dirty);assert(get_shader_binding_for_state(&r,&original,hash)==finite&&allocated==2);assert(pgraph_vk_ready_shader_binding(&r,&original,hash)==finite);
 ShaderStateView view={.head=&original};memcpy(&view.tail,(const char*)&original+VK_SHADER_HEAD_BYTES,sizeof(view.tail));ShaderBinding viewed={0};shader_cache_entry_init_view(&r.shader_cache,&viewed.node,&view);assert(viewed.ready_vertex_route&&!memcmp(&viewed.state,&original,sizeof(original)));assert(!shader_cache_entry_compare_view(&r.shader_cache,&viewed.node,&view));r.ready_vertex.route=false;assert(shader_cache_entry_compare_view(&r.shader_cache,&viewed.node,&view));
 ReadyBindingInput input={.state=&original,.vsh=&ordinary,.psh=&ordinary};assert(ready_binding_compare(&r.shader_cache,&finite->node,&input));assert(!ready_binding_compare(&r.shader_cache,&exotic->node,&input));ShaderBinding materialized={0};ready_binding_init(&r.shader_cache,&materialized.node,&input);assert(!materialized.ready_vertex_route&&!memcmp(&materialized.state,&original,sizeof(original))&&uniforms==1);
 /* Original guest bytes/hash and exact (nonprepared) recipe stay identical
  * while native ShaderEXT/VkShaderModule ownership changes independently. */
 r.shader_objects.enabled=false;ShaderBinding *fallback=get_shader_binding_for_state(&r,&original,hash);assert(fallback!=exotic&&!fallback->shader_object_route&&fallback->vsh.module_info==&native_module&&allocated==3);assert(exotic->shader_object_route&&exotic->vsh.module_info==&ordinary);assert(pgraph_vk_ready_shader_binding(&r,&original,hash)==fallback);assert(shader_cache_entry_compare_view(&r.shader_cache,&viewed.node,&view));assert(ready_binding_compare(&r.shader_cache,&exotic->node,&input));assert(!ready_binding_compare(&r.shader_cache,&fallback->node,&input));ShaderBinding native_view={0};shader_cache_entry_init_view(&r.shader_cache,&native_view.node,&view);assert(!native_view.shader_object_route&&!shader_cache_entry_compare_view(&r.shader_cache,&native_view.node,&view));
 r.shader_objects.enabled=true;assert(get_shader_binding_for_state(&r,&original,hash)==exotic&&allocated==3);assert(pgraph_vk_ready_shader_binding(&r,&original,hash)==exotic);assert(shader_cache_entry_compare_view(&r.shader_cache,&native_view.node,&view));
 puts("Vulkan prepared route:actual finite->NaN->finite dirty selection, stable same guest/hash namespace, immutable A/B/A owners, independent ShaderEXT/module namespace, view and ready materialization/query mode guards PASS");return 0;
}
