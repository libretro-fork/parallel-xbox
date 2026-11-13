/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "hw/xbox/nv2a/pgraph/glsl/uniform-cache.h"
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL namespace line %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
typedef unsigned GLuint;
typedef struct Lru {unsigned unused;} Lru;
typedef struct LruNode {unsigned unused;} LruNode;
typedef struct ShaderModuleCacheEntry {LruNode node;GLuint gl_shader,gl_stage_program;bool locs_pending;} ShaderModuleCacheEntry;
typedef struct ShaderBinding {LruNode node;bool initialized,cached;GLuint gl_program,gl_pipeline,gl_prog_vs,gl_prog_fs,gl_prog_gs;void *program;int program_owner;ShaderState state;} ShaderBinding;
typedef struct PGRAPHGLState {UniformCache uni_cache;Lru shader_cache,shader_module_cache;ShaderBinding *shader_binding,*spec_binding;} PGRAPHGLState;
static struct {bool live,deleted;unsigned refs;float color[4];} objects[64];
static unsigned uploads,pipeline_deletes;
static void glDeleteProgram(GLuint p){if(!p)return;assert(p<64&&objects[p].live);objects[p].deleted=true;if(!objects[p].refs)objects[p].live=false;}
static void glDeleteShader(GLuint p){(void)p;assert(false);}
static void glDeleteProgramPipelines(int count,const GLuint *p){assert(count==1&&*p==55);pipeline_deletes++;for(unsigned i=11;i<=13;i++)if(objects[i].refs){if(!--objects[i].refs&&objects[i].deleted)objects[i].live=false;}}
static void spec_binding_cancel(PGRAPHGLState *r){(void)r;assert(false);}
static void shader_binary_release(void *p,int *owner){(void)p;(void)owner;assert(false);}
/* PRODUCTION */
static void upload(void *opaque,const UniformInfo *info,int loc,const void *value){unsigned p=*(unsigned*)opaque;assert(p<64&&objects[p].live&&loc==7&&info->size==16);memcpy(objects[p].color,value,16);uploads++;}
static void color(PGRAPHGLState *r,unsigned p,const float *value){
 const UniformInfo info={"color",UniformElementType_vec4,16,1,0};const int loc=7;const int8_t section=UNISEC_VSHC;unsigned gen[UNISEC__COUNT]={1,1,1,1,1,1};
 int slot=uniform_cache_slot(&r->uni_cache,p);assert(slot>=0);uniform_cache_apply(&r->uni_cache,slot,gen,&info,&loc,value,1,&section,upload,&p);
}
int main(void){
 PGRAPHGLState *r=calloc(1,sizeof(*r));assert(r);unsigned cases=0;
 for(unsigned trial=0;trial<128;trial++)for(unsigned stages=1;stages<=3;stages++)for(unsigned pipelines=1;pipelines<=2;pipelines++){
  uniform_cache_invalidate(&r->uni_cache);memset(objects,0,sizeof(objects));uploads=pipeline_deletes=0;
  ShaderBinding b[2]={0};for(unsigned i=0;i<pipelines;i++){b[i].initialized=b[i].cached=true;b[i].gl_pipeline=55;b[i].gl_prog_vs=11;b[i].gl_prog_fs=stages>=2?12:0;b[i].gl_prog_gs=stages>=3?13:0;}
  const float value[4]={.25f,.5f,.75f,1};
  objects[63].live=true;objects[63].refs=1;color(r,63,value);
  for(unsigned p=11;p<11+stages;p++){
   objects[p].live=true;objects[p].refs=pipelines;color(r,p,value);
   ShaderModuleCacheEntry module={.gl_stage_program=p};shader_module_cache_entry_post_evict(&r->shader_module_cache,&module.node);
   assert(objects[p].live&&objects[p].deleted&&!module.gl_stage_program);
   /* A retained pipeline legitimately uses this program after its module
    * owner is evicted, recreating both memo values and generation markers. */
   color(r,p,value);assert(!memcmp(objects[p].color,value,16));
  }
  for(unsigned i=0;i<pipelines;i++){
   r->shader_binding=&b[i];shader_cache_entry_post_evict(&r->shader_cache,&b[i].node);
   assert(!r->shader_binding&&!b[i].initialized&&!b[i].gl_pipeline&&!b[i].cached);
   unsigned unrelated_before=uploads;color(r,63,value);assert(uploads==unrelated_before);
   if(i+1<pipelines)for(unsigned p=11;p<11+stages;p++){assert(objects[p].live);color(r,p,value);}
  }
  assert(pipeline_deletes==pipelines);
  for(unsigned p=11;p<11+stages;p++){
   assert(!objects[p].live);
   /* GL may recycle a retired name. Give the new object its specified
    * zero initial uniforms and the SAME guest source generation/value. */
   objects[p].live=true;objects[p].deleted=false;memset(objects[p].color,0,16);unsigned before=uploads;color(r,p,value);
   assert(uploads==before+1&&!memcmp(objects[p].color,value,16));
  }
  cases++;
 }
 printf("GL uniform namespace: %u retained-stage memo repopulations, one/two pipeline retirements, stage masks and recycled-name initial uploads PASS\n",cases);free(r);
}
