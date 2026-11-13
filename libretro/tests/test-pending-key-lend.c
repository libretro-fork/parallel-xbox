/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define ctz32(v) __builtin_ctz(v)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define QTAILQ_FOREACH(n,h,f) for((n)=*(h);(n);(n)=(n)->next)
#define PUMP_RING 64
typedef struct LruNode {uint64_t hash;struct LruNode *next;} LruNode;
typedef struct {LruNode *bins[8];} Lru;
typedef struct {unsigned refs;} ShaderModuleInfo;
typedef struct ShaderBinding {LruNode node;ShaderState state;struct{ShaderModuleInfo *module_info;}vsh,geom,psh;} ShaderBinding;
typedef struct {bool compute,full;struct{ShaderState shader_state;}key;uint64_t state_hash;} LibJob;
typedef struct PGRAPHVkState {Lru shader_cache;ShaderBinding *shader_binding;bool shader_bindings_changed,sibling_vertex_family,sibling_retry;unsigned sibling_ticket,sibling_generation;} PGRAPHVkState;
struct PGRAPHState {
 PGRAPHVkState *vk_renderer_state;bool program_data_dirty,two_side_light_en;
 uint16_t uniform_attrs,swizzle_attrs,compressed_attrs;unsigned primitive_mode,surface_scale_factor;
 struct{unsigned zeta_format;}surface_shape;bool texture_matrix_enable[4];
 uint32_t shader_reg_snap[12],raw[0x2000/4];bool dirty[0x2000/4];
};
unsigned pgraph_glsl_dynamic_gen;
static LibJob pump_jobs[PUMP_RING];static unsigned pump_d,pump_w_local;
static bool geometry,poison,enabled,pipeline_dirty;
static bool pgraph_gpu_boost_gl(void){return enabled;}
static unsigned promotions;
static ShaderBinding *expected_binding;
static unsigned lru_hash_to_bin(Lru *l,uint64_t h){(void)l;return h%8;}
bool pgraph_glsl_need_geom(const GeomState *s){(void)s;return geometry;}
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned reg){assert(reg/4<ARRAY_SIZE(pg->raw));return pg->raw[reg/4];}
static bool pgraph_is_reg_dirty(PGRAPHState *pg,unsigned reg){assert(reg/4<ARRAY_SIZE(pg->dirty));return pg->dirty[reg/4];}
static ShaderBinding *get_shader_binding_for_state(PGRAPHVkState *r,const ShaderState *state,uint64_t h){
 (void)r;
 assert(state==&expected_binding->state&&h==expected_binding->node.hash);promotions++;
 if(poison){pump_d=pump_w_local;memset(pump_jobs,0xcd,sizeof(pump_jobs));}
 return expected_binding;
}
static bool check_pipeline_dirty(PGRAPHState *pg){(void)pg;return pipeline_dirty;}
/* PRODUCTION */
static void flush_retry(PGRAPHState *pg){/* FLUSH */}
int main(void){
 (void)pgraph_gpu_boost_gl;
 unsigned cases=0,borrowed=0;unsigned starts[]={0,13,UINT_MAX-3,UINT_MAX-64};
 for(unsigned base=0;base<4;base++)for(unsigned count=0;count<=64;count+=16)for(unsigned offset=0;offset<4;offset++)for(unsigned bits=0;bits<64;bits++)for(unsigned change=0;change<21;change++){
  PGRAPHVkState r={0};PGRAPHState pg={.vk_renderer_state=&r};ShaderModuleInfo info={1};ShaderBinding binding={0},collision={0};
  pump_d=starts[base];pump_w_local=pump_d+count;
  unsigned ticket=pump_d+(offset==0?0:offset==1?count-1:offset==2?count:UINT_MAX);
  memset(pump_jobs,0,sizeof(pump_jobs));LibJob *job=&pump_jobs[ticket%PUMP_RING];job->full=!(bits&1);job->state_hash=0x12345+base;
  ShaderState *state=&job->key.shader_state;state->vsh.uniform_attrs=3;state->vsh.swizzle_attrs=5;state->vsh.compressed_attrs=7;
  state->geom.primitive_mode=1;state->psh.surface_zeta_format=2;state->vsh.programmable.program_data[0][0]=base;
  pg.uniform_attrs=3;pg.swizzle_attrs=5;pg.compressed_attrs=7;pg.primitive_mode=1;pg.surface_shape.zeta_format=2;
  bool dirty_change=(change>=1&&change<=13)||change==18;
  switch(change){
   case 1:pg.program_data_dirty=true;break;
   case 2:pg.dirty[NV_PGRAPH_COMBINECTL/4]=true;break;
   case 3:pg.uniform_attrs++;break;
   case 4:pg.swizzle_attrs++;break;
   case 5:pg.compressed_attrs++;break;
   case 6:pg.primitive_mode++;break;
   case 7:pg.surface_shape.zeta_format++;break;
   case 8:state->vsh.is_fixed_function=true;state->vsh.fixed_function.lighting=true;pg.two_side_light_en=true;break;
   case 9:pg.texture_matrix_enable[0]=true;break;
   case 10:pg.dirty[NV_PGRAPH_TEXFMT0/4]=true;pg.raw[NV_PGRAPH_TEXFMT0/4]=NV_PGRAPH_TEXFMT0_CUBEMAPENABLE;break;
   case 11:pg.dirty[NV_PGRAPH_SETUPRASTER/4]=true;pg.raw[NV_PGRAPH_SETUPRASTER/4]=NV_PGRAPH_SETUPRASTER_Z_FORMAT;break;
   case 12:pg.dirty[NV_PGRAPH_TEXFILTER0/4]=true;pg.raw[NV_PGRAPH_TEXFILTER0/4]=NV_PGRAPH_TEXFILTER0_ASIGNED;break;
   case 13:pg.dirty[NV_PGRAPH_TEXADDRESS0/4]=true;pg.raw[NV_PGRAPH_TEXFMT0/4]=(NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8<<__builtin_ctz(NV_PGRAPH_TEXFMT0_COLOR))|(2<<__builtin_ctz(NV_PGRAPH_TEXFMT0_MIPMAP_LEVELS));pg.raw[NV_PGRAPH_TEXFILTER0/4]=NV_PGRAPH_TEXFILTER0_ASIGNED;break;
   case 14:pg.dirty[NV_PGRAPH_TEXADDRESS0/4]=true;break;
   case 15:pg.dirty[NV_PGRAPH_SETUPRASTER/4]=true;pg.raw[NV_PGRAPH_SETUPRASTER/4]=NV_PGRAPH_SETUPRASTER_FRONTFACE;break;
   case 16:pg.dirty[NV_PGRAPH_TEXFMT0/4]=true;break;
   case 17:pg.dirty[NV_PGRAPH_TEXFILTER0/4]=true;break;
   case 18:pg.raw[NV_PGRAPH_COMBINECTL/4]=1;pg.dirty[NV_PGRAPH_COMBINECOLORI0/4]=true;break;
   case 19:state->vsh.fixed_function.texgen_dynamic=true;pg.texture_matrix_enable[0]=true;break;
   case 20:state->vsh.is_fixed_function=true;state->vsh.fixed_function.lighting=true;state->vsh.fixed_function.csv0c_dynamic=true;pg.two_side_light_en=true;break;
  }
  binding.state=*state;binding.node.hash=job->state_hash;binding.vsh.module_info=&info;binding.psh.module_info=bits&4?NULL:&info;
  geometry=base&1;binding.geom.module_info=geometry?&info:NULL;
  collision=binding;collision.state.psh.final_inputs_0++;collision.node.next=&binding.node;
  if(!(bits&2))r.shader_cache.bins[job->state_hash%8]=&collision.node;
  r.sibling_retry=true;r.sibling_ticket=ticket;r.sibling_generation=9;pgraph_glsl_dynamic_gen=bits&8?10:9;
  poison=bits&16;enabled=!(bits&32);promotions=0;expected_binding=&binding;
  bool active=count&&(offset<=1);bool ok=active&&!(bits&47)&&!dirty_change;
  bool selected=vk_try_pending_binding(&pg);assert(selected==ok&&promotions==ok);
  if(ok){assert(r.shader_binding==&binding&&r.shader_bindings_changed&&r.sibling_retry&&!pg.program_data_dirty);borrowed++;}
  else{assert(!r.sibling_retry&&pg.program_data_dirty&&!r.shader_bindings_changed&&!r.shader_binding);}
  r.sibling_retry=cases&1;r.shader_binding=&binding;pg.program_data_dirty=false;
  flush_retry(&pg);pg.program_data_dirty=false; /* restored serialized guest bit */
  assert(!r.sibling_retry&&!r.shader_binding);
  cases++;
 }

 /* A retained ready lighting sibling needs no cache lookup while its exact
  * specialization is pending. Raster/VI/pass changes must end that lend. */
 for(unsigned changed=0;changed<3;changed++) {
  PGRAPHVkState r={0};PGRAPHState pg={.vk_renderer_state=&r};ShaderBinding binding={0};
  pump_d=7;pump_w_local=8;memset(pump_jobs,0,sizeof(pump_jobs));
  LibJob *job=&pump_jobs[7];job->full=true;job->state_hash=77;
  r.sibling_retry=true;r.sibling_vertex_family=true;r.sibling_ticket=7;
  r.sibling_generation=pgraph_glsl_dynamic_gen=9;r.shader_binding=&binding;
  enabled=true;pipeline_dirty=changed==1;pg.program_data_dirty=changed==2;
  promotions=0;bool selected=vk_try_pending_binding(&pg);
  assert(selected==(changed==0));assert(!promotions);
  assert(r.sibling_retry==(changed==0));assert(pg.program_data_dirty==(changed!=0));
  pipeline_dirty=false;
 }
 printf("Pending key lend: %u ring wrap, expired/reused/non-full slot, generation, 21 native dirty-state classes, exact cache collision, poisoned-after-query and load invalidation cases PASS; %u successful lends, zero ShaderState snapshots in selection\n",cases,borrowed);
}
