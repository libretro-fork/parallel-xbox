/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Execute actual option/latch and startup gate source. Native owner behavior
 * and shader math remain covered by the unchanged dedicated owner/GPU suites. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "hw/xbox/nv2a/pgraph/gl/ready-config.h"
#define HAVE_NO_LANGEXTRA 1
#include "ui/libretro_core_options.h"
#include "hw/xbox/nv2a/pgraph/gl/ready-ff-state.h"
#include "hw/xbox/nv2a/pgraph/glsl/geom.h"
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"PREPARED_OPTION_FAIL line=%u: %s\n",__LINE__,#x);exit(86); } } while(0)
#define MOCK static __attribute__((unused))
#if !defined(__ANDROID__) && !defined(XEMU_DISABLE_READY_GL)
#define OPTION_VISIBLE 1
#else
#define OPTION_VISIBLE 0
#endif
static bool use_vulkan;
static retro_environment_t environ_cb;
/* UI_OPTION */
static const char *frontend_value;
static bool frontend_accept;
static unsigned requests;
static bool frontend(unsigned command,void *data){
 assert(command==RETRO_ENVIRONMENT_GET_VARIABLE);struct retro_variable *v=data;
 assert(v&&!strcmp(v->key,"xemu_gl_prepared_shaders")&&!v->value);requests++;
 v->value=frontend_value;return frontend_accept;
}
/* This reset belongs only to the test: production has no reset of the latch. */
static void reset_frontend(void){
#if OPTION_VISIBLE
 memset(&gl_prepared_shaders,0,sizeof(gl_prepared_shaders));
#endif
 use_vulkan=false;environ_cb=frontend;frontend_value=NULL;frontend_accept=true;requests=0;
}
static unsigned check_table(void){
 unsigned count=0;
 for(unsigned i=0;option_defs_us[i].key;i++){
  const struct retro_core_option_v2_definition *d=&option_defs_us[i];
  if(strcmp(d->key,"xemu_gl_prepared_shaders"))continue;
  count++;assert(!strcmp(d->desc,"Prepared Shaders (Restart)")&&!strcmp(d->category_key,"video")&&!strcmp(d->default_value,"disabled"));
  assert(!strcmp(d->values[0].value,"disabled")&&!strcmp(d->values[1].value,"enabled")&&!d->values[2].value);
  assert(strstr(d->info,"startup")&&strstr(d->info,"GPU")&&strstr(d->info,"Restart RetroArch"));
 }
 assert(count==OPTION_VISIBLE);return count;
}
typedef struct {bool free;} LruNode;
typedef struct {LruNode node;unsigned owner;} ShaderBinding;
typedef struct {LruNode node;unsigned owner;} ShaderModuleCacheEntry;
typedef struct {unsigned free_count;} Lru;
typedef struct {
 Lru shader_cache,shader_module_cache;ShaderBinding *shader_cache_entries,*ready_pipelines;
 ShaderModuleCacheEntry *shader_module_cache_entries;
 struct {ShaderBinding *pipelines;ShaderModuleCacheEntry *stages;}ready_ff;
 struct {ShaderModuleCacheEntry *stages;}ready_geom,ready_psh;
} PGRAPHGLState;
enum {CACHE_SLOTS=50*1024};
static ShaderBinding bindings[CACHE_SLOTS];
static ShaderModuleCacheEntry modules[CACHE_SLOTS];
static bool supported_sso;
static unsigned init_step,tuple_index_checks;
MOCK bool gl_sso_enabled(void){return supported_sso;}
MOCK void lru_add_free(Lru *cache,LruNode *node){assert(!node->free);node->free=true;cache->free_count++;}
MOCK void pipeline_owner_finalize(PGRAPHGLState *r){assert(r&&init_step==0);tuple_index_checks++;}
MOCK void ready_vsh_init(PGRAPHGLState *r){assert(r&&++init_step==1);}
MOCK void ready_geom_init(PGRAPHGLState *r){assert(r->ready_geom.stages==modules&&++init_step==2);}
MOCK void ready_psh_init(PGRAPHGLState *r){assert(r->ready_psh.stages==modules+GEOM_DYNAMIC_VARIANTS&&++init_step==3);}
MOCK void ready_pipelines_init(PGRAPHGLState *r){assert(r->ready_pipelines==bindings&&++init_step==4);}
MOCK void ready_ff_init(PGRAPHGLState *r){assert(r->ready_ff.pipelines==bindings+18&&r->ready_ff.stages==modules+GEOM_DYNAMIC_VARIANTS+2&&++init_step==5);}
static void startup(void){
 PGRAPHGLState state={0},*r=&state;
 memset(bindings,0,sizeof(bindings));memset(modules,0,sizeof(modules));
 r->shader_cache_entries=bindings;r->shader_module_cache_entries=modules;
 const size_t shader_cache_size=CACHE_SLOTS,shader_module_cache_size=CACHE_SLOTS;
 unsigned reserved_bindings=0,reserved_geometry=0;init_step=tuple_index_checks=0;
#ifdef XEMU_EXPERIMENTAL_READY_VSH
 /* PREPARE_PREDICATE */
 /* BINDING_RESERVATIONS */
#endif
 /* BINDING_FREE */
#ifdef XEMU_EXPERIMENTAL_READY_VSH
 /* MODULE_RESERVATIONS */
#endif
 /* MODULE_FREE */
#ifdef XEMU_EXPERIMENTAL_READY_VSH
 pipeline_owner_finalize(r); /* Ordinary tuple sharing stays active when off. */
 /* OPTIONAL_INIT */
 assert(tuple_index_checks==1);
#else
 assert(!tuple_index_checks);
#endif
 bool expected=OPTION_VISIBLE&&supported_sso&&pgraph_gl_prepared_shaders_enabled();
 assert(reserved_bindings==(expected?18+GL_READY_FF_PIPELINE_COUNT:0));
 assert(reserved_geometry==(expected?GEOM_DYNAMIC_VARIANTS+2+GL_READY_FF_STAGE_COUNT:0));
 assert(init_step==(expected?5u:0u));
 assert(state.shader_cache.free_count==CACHE_SLOTS-reserved_bindings&&state.shader_module_cache.free_count==CACHE_SLOTS-reserved_geometry);
 if(!expected)assert(!state.ready_pipelines&&!state.ready_ff.pipelines&&!state.ready_ff.stages&&!state.ready_geom.stages&&!state.ready_psh.stages);
 for(unsigned i=0;i<CACHE_SLOTS;i++){
  assert(bindings[i].node.free==(i>=reserved_bindings));assert(modules[i].node.free==(i>=reserved_geometry));
 }
}
int main(void){
 assert(check_table()==OPTION_VISIBLE);
 const char *values[]={NULL,"disabled","enabled","bad","ENABLED"};unsigned cases=0;
 for(unsigned accepted=0;accepted<2;accepted++)for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++){
  reset_frontend();frontend_accept=accepted;frontend_value=values[i];
  assert(!pgraph_gl_prepared_shaders_enabled());update_gl_prepared_shader_option();
  assert(requests==OPTION_VISIBLE);use_vulkan=true;latch_gl_prepared_shader_option();
  assert(!pgraph_gl_prepared_shaders_enabled());use_vulkan=false;latch_gl_prepared_shader_option();
  bool expected=OPTION_VISIBLE&&accepted&&values[i]&&!strcmp(values[i],"enabled");
  assert(pgraph_gl_prepared_shaders_enabled()==expected);
  for(unsigned ss=0;ss<2;ss++){supported_sso=ss;startup();cases++;}
  frontend_accept=true;frontend_value=expected?"disabled":"enabled";update_gl_prepared_shader_option();latch_gl_prepared_shader_option();
  assert(pgraph_gl_prepared_shaders_enabled()==expected);
 }
 reset_frontend();environ_cb=NULL;update_gl_prepared_shader_option();latch_gl_prepared_shader_option();assert(!pgraph_gl_prepared_shaders_enabled()&&!requests);
 reset_frontend();use_vulkan=true;frontend_value="enabled";update_gl_prepared_shader_option();latch_gl_prepared_shader_option();assert(!pgraph_gl_prepared_shaders_enabled());
 frontend_value="disabled";update_gl_prepared_shader_option();latch_gl_prepared_shader_option();use_vulkan=false;latch_gl_prepared_shader_option();assert(!pgraph_gl_prepared_shaders_enabled());
 reset_frontend();use_vulkan=true;frontend_value="disabled";update_gl_prepared_shader_option();latch_gl_prepared_shader_option();frontend_value="enabled";update_gl_prepared_shader_option();use_vulkan=false;latch_gl_prepared_shader_option();assert(pgraph_gl_prepared_shaders_enabled()==!!OPTION_VISIBLE);
 puts("PASS prepared option: actual Video definition/default/visibility, frontend decline/null/invalid values, first-GL latch, Vulkan fallback order, immutable active value, SSO admission, exact pool partition and zero optional init when off");
 printf("Wiring variants: %u exact startup cases; visible=%u\n",cases,OPTION_VISIBLE);
 return 0;
}
