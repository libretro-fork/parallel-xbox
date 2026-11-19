/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <vulkan/vulkan.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#undef assert
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL GS line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define VK_READY_GEOMETRY_STAGES 16u
#define GEOM_DYNAMIC_VARIANTS 16u
#define VK_READY_GEOMETRY_BYTES 32u
#define VK_READY_GEOMETRY_VERSION 1u
#define GSH_UBO_BINDING 14
#define NV_PGRAPH_SETUPRASTER 1u
#define NV_PGRAPH_SETUPRASTER_CULLENABLE 1u
#define NV_PGRAPH_SETUPRASTER_FRONTFACE 2u
#define NV_PGRAPH_SETUPRASTER_CULLCTRL 12u
#define GET_MASK(v,m) (((v)&(m))/((m)&(-(m))))
/* STRINGS */
/* TYPES */
/* GENERATOR */
/* MAPPING */
typedef GlslcUniform ShaderUniform;
typedef struct {const ShaderUniform *uniforms;size_t num_uniforms,total_size;void *allocation;} ShaderUniformLayout;
typedef struct ShaderModuleInfo {unsigned refs;uint8_t ready_geometry_index;bool uniform_valid,uniform_staged_valid;ShaderUniformLayout uniforms,push_constants;} ShaderModuleInfo;
typedef struct {bool free;} LruNode;
typedef struct {unsigned returned;} Lru;
typedef struct {unsigned kind,native_kind;struct {GeomState state;GenGeomGlslOptions glsl_opts;uint16_t ready_kind,ready_version;}geom;} ShaderModuleCacheKey;
typedef struct {LruNode node;ShaderModuleCacheKey key;ShaderModuleInfo *module_info;} ShaderModuleCacheEntry;
typedef struct {GeomState geom;struct {unsigned ui;}psh;} ShaderState;
typedef struct {ShaderState state;struct {ShaderModuleInfo *module_info;}geom;} ShaderBinding;
typedef struct PGRAPHVkState {bool has_gs_ubo_binding;struct {bool enabled;}shader_objects;Lru shader_module_cache;struct {bool ready;ShaderModuleCacheEntry *stages;ShaderModuleInfo zero_uniform;uint32_t zero_values[8];}ready_geometry;} PGRAPHVkState;
typedef struct {PGRAPHVkState *vk_renderer_state;struct{unsigned width,height;}surface_binding_dim;unsigned line_width,setup,aa;} PGRAPHState;
static unsigned pgraph_glsl_ui_clamp_stage(const typeof(((ShaderState *)0)->psh)*s){return s->ui;}
static void pgraph_apply_anti_aliasing_factor(PGRAPHState*p,unsigned*w,unsigned*h){*w*=p->aa;*h*=p->aa;}
static uint32_t pgraph_reg_r(PGRAPHState*p,unsigned reg){assert(reg==NV_PGRAPH_SETUPRASTER);return p->setup;}
static int uniform_index(ShaderUniformLayout*l,const char*name){for(unsigned i=0;i<l->num_uniforms;i++)if(!strcmp(l->uniforms[i].name,name))return i+1;return -1;}
static void lru_add_free(Lru*l,LruNode*n){assert(!n->free);n->free=true;l->returned++;}
static unsigned made,destroyed,sources,calls;
static int fail_at=-1,bad_layout=-1;
static const char *output;
static bool export_words,pgraph_vk_spirv_1_6;
typedef struct {GlslcResult result;} ShaderSource;
static ShaderSource *pgraph_vk_shader_source_take(GlslcResult*r){ShaderSource*s=malloc(sizeof(*s));assert(s);s->result=*r;memset(r,0,sizeof(*r));sources++;return s;}
static ShaderModuleInfo *pgraph_vk_create_shader_object_from_spirv(PGRAPHVkState*r,VkShaderStageFlagBits stage,ShaderSource*s,bool optional){
 assert(!r->ready_geometry.ready&&r->shader_objects.enabled&&stage==VK_SHADER_STAGE_GEOMETRY_BIT&&optional);assert(s->result.words[0]==0x07230203u);
 unsigned index=calls++;ShaderModuleInfo*m=NULL;
 if((int)index!=fail_at){
  if(export_words){char path[2048];snprintf(path,sizeof(path),"%s/gs-%u-%u.spv",output,pgraph_vk_spirv_1_6,index);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(s->result.words,4,s->result.num_words,f)==s->result.num_words);fclose(f);}
  m=calloc(1,sizeof(*m));assert(m);made++;
  m->uniforms=(ShaderUniformLayout){s->result.uniforms.members,s->result.uniforms.count,s->result.uniforms.size,calloc(1,s->result.uniforms.size)};
  m->push_constants=(ShaderUniformLayout){s->result.push_constants.members,s->result.push_constants.count,s->result.push_constants.size,NULL};
  if((int)index==bad_layout&&m->uniforms.num_uniforms)((GlslcUniform*)m->uniforms.uniforms)[0].offset++;
 }else{free(s->result.uniforms.members);free(s->result.push_constants.members);}
 glslc_result_free_words(&s->result);free(s);sources--;return m;
}
/* FACTORY */
static void pgraph_vk_ref_shader_module(ShaderModuleInfo*m){assert(m);m->refs++;}
static void pgraph_vk_unref_shader_module(PGRAPHVkState*r,ShaderModuleInfo*m){(void)r;assert(m&&m->refs);if(!--m->refs){free((void*)m->uniforms.uniforms);free((void*)m->push_constants.uniforms);free(m->uniforms.allocation);free(m);destroyed++;}}
/* PRODUCTION */
static void reset(PGRAPHVkState*r,ShaderModuleCacheEntry*entries){memset(r,0,sizeof(*r));memset(entries,0,16*sizeof(*entries));r->has_gs_ubo_binding=true;r->shader_objects.enabled=true;r->ready_geometry.stages=entries;calls=made=destroyed=0;assert(!sources);}
int main(int argc,char**argv){assert(argc==2);output=argv[1];unsigned cases=0;
 _Static_assert(sizeof(GenGeomGlslOptions)==4&&offsetof(GenGeomGlslOptions,ui_clamp_stage)==2,"ordinary option offsets must survive");
 for(unsigned version=0;version<2;version++){
  PGRAPHVkState r;ShaderModuleCacheEntry entries[16];pgraph_vk_spirv_1_6=version;reset(&r,entries);fail_at=bad_layout=-1;export_words=true;
  assert(vk_ready_geometry_init(&r)&&r.ready_geometry.ready&&made==16&&!sources);export_words=false;
  for(unsigned i=0;i<16;i++){
   assert(entries[i].key.geom.ready_kind==1&&entries[i].key.geom.ready_version==1);
   assert(entries[i].key.geom.glsl_opts.control_ubo_binding==14&&entries[i].key.geom.glsl_opts.vulkan);
   for(unsigned z=0;z<2;z++)for(unsigned first=0;first<2;first++)for(unsigned rot=0;rot<3;rot++)for(unsigned ui=0;ui<5;ui++){
    ShaderModuleCacheKey key=entries[i].key;key.geom.state.z_perspective=z;key.geom.state.first_vertex_is_provoking=first;key.geom.state.tri_rot0=rot;key.geom.state.tri_rot1=2-rot;key.geom.glsl_opts.ui_clamp_stage=ui;
    ShaderModuleCacheKey saved=key;int v=pgraph_glsl_geom_dynamic_variant(&key.geom.state,key.geom.glsl_opts);ShaderModuleInfo*m=vk_ready_geometry_find(&r,&key);
    assert(m==(v<0?NULL:entries[v].module_info)&&!memcmp(&key,&saved,sizeof(key)));cases++;
   }
   ShaderBinding binding={.state.geom=entries[i].key.geom.state,.geom.module_info=entries[i].module_info};PGRAPHState pg={.vk_renderer_state=&r,.surface_binding_dim={640,480},.line_width=24,.aa=1};
   assert(vk_ready_geometry_parameters(&pg,&binding));assert(!vk_ready_geometry_parameters(&pg,&binding));
   uint8_t original[32];memcpy(original,binding.geom.module_info->uniforms.allocation,32);binding.geom.module_info->uniform_staged_valid=true;
   binding.state.geom.z_perspective=true;binding.state.psh.ui=3;assert(vk_ready_geometry_parameters(&pg,&binding)&&!binding.geom.module_info->uniform_staged_valid);
   binding.state.geom.z_perspective=false;binding.state.psh.ui=0;assert(vk_ready_geometry_parameters(&pg,&binding)&&!memcmp(original,binding.geom.module_info->uniforms.allocation,32));
   bool wide=i==2||i==3;if(i>=4){binding.state.geom.wide_lines=true;assert(!vk_ready_geometry_parameters(&pg,&binding));}
   pg.line_width++;assert(vk_ready_geometry_parameters(&pg,&binding)==wide);pg.aa=2;assert(vk_ready_geometry_parameters(&pg,&binding)==wide);pg.setup=NV_PGRAPH_SETUPRASTER_CULLENABLE|NV_PGRAPH_SETUPRASTER_FRONTFACE|4;assert(vk_ready_geometry_parameters(&pg,&binding)==wide);
  }
  ShaderModuleCacheKey key=entries[4].key;key.native_kind=0;assert(!vk_ready_geometry_find(&r,&key));key.native_kind=1;key.kind=VK_SHADER_STAGE_VERTEX_BIT;assert(!vk_ready_geometry_find(&r,&key));key.kind=VK_SHADER_STAGE_GEOMETRY_BIT;key.geom.glsl_opts.ui_clamp_stage=5;assert(!vk_ready_geometry_find(&r,&key));
  ShaderBinding absent={0};assert(vk_ready_geometry_uniform_owner(&r,&absent)==&r.ready_geometry.zero_uniform);PGRAPHState pg={.vk_renderer_state=&r};assert(!vk_ready_geometry_parameters(&pg,&absent));
  ShaderModuleInfo*held=entries[4].module_info;pgraph_vk_ref_shader_module(held);vk_ready_geometry_finalize(&r);assert(destroyed==15&&held->refs==1);pgraph_vk_unref_shader_module(&r,held);assert(destroyed==16&&!sources&&!r.ready_geometry.stages);
 }
 for(unsigned invalid=0;invalid<2;invalid++)for(unsigned at=0;at<16;at++){
  PGRAPHVkState r;ShaderModuleCacheEntry entries[16];reset(&r,entries);fail_at=invalid?-1:(int)at;bad_layout=invalid?(int)at:-1;
  assert(!vk_ready_geometry_init(&r)&&!r.ready_geometry.ready&&!r.ready_geometry.stages&&r.shader_module_cache.returned==16&&made==destroyed&&!sources);
 }
 printf("Prepared Vulkan geometry: %u raw-state selectors,32-byte A/B/A/unchanged/line/AA metadata,32 startup failure/layout cases, exact16slot retirement and held-consumer lifetime PASS\n",cases);
 return 0;
}
