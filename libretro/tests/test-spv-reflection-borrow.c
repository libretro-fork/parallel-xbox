/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "deps/SPIRV-Reflect/spirv_reflect.h"
/* TYPES */
typedef struct {uint8_t *data;unsigned len;} GByteArray;
typedef struct {ShaderUniformLayout uniforms,push_constants;} ShaderModuleInfo;
static unsigned allocations;
static void *tracked_alloc(size_t n,bool zero){void *p=zero?calloc(1,n?n:1):malloc(n?n:1);assert(p);allocations++;return p;}
static void tracked_free(void *p){if(p){assert(allocations);allocations--;free(p);}}
#define MAX(a,b) ((a)>(b)?(a):(b))
#define g_malloc_n(n,s) tracked_alloc((n)*(s),false)
#define g_malloc0_n(n,s) tracked_alloc((n)*(s),true)
#define g_malloc0(n) tracked_alloc((n),true)
#define g_free tracked_free
static unsigned borrowed_calls;
static SpvReflectResult checked_reflect(SpvReflectModuleFlags flags,size_t size,const void *code,SpvReflectShaderModule *module){SpvReflectResult r=spvReflectCreateShaderModule2(flags,size,code,module);if(r==SPV_REFLECT_RESULT_SUCCESS&&flags&SPV_REFLECT_MODULE_FLAG_NO_COPY){assert(spvReflectGetCode(module)==code);borrowed_calls++;}return r;}
#define spvReflectCreateShaderModule2 checked_reflect
/* PRODUCTION */
static void release(ShaderModuleInfo *i){finalize_uniform_layout(&i->uniforms);finalize_uniform_layout(&i->push_constants);}
static void layout_same(const ShaderUniformLayout *a,const ShaderUniformLayout *b){assert(a->num_uniforms==b->num_uniforms&&a->total_size==b->total_size);for(unsigned j=0;j<a->num_uniforms;j++){const ShaderUniform *x=&a->uniforms[j],*y=&b->uniforms[j];assert(!strcmp(x->name,y->name)&&x->offset==y->offset&&x->dim_v==y->dim_v&&x->dim_a==y->dim_a&&x->stride==y->stride&&x->align==y->align);}}
static void name(const char *a,const char *b){assert((!a&&!b)||(a&&b&&!strcmp(a,b)));}
static void block(const SpvReflectBlockVariable *a,const SpvReflectBlockVariable *b)
{
 name(a->name,b->name);assert(a->offset==b->offset&&a->absolute_offset==b->absolute_offset&&a->size==b->size&&a->padded_size==b->padded_size&&a->member_count==b->member_count);
 assert(!memcmp(&a->numeric,&b->numeric,sizeof(a->numeric)));assert(!memcmp(&a->array,&b->array,sizeof(a->array)));
 for(unsigned i=0;i<a->member_count;i++)block(&a->members[i],&b->members[i]);
}
static void same(const SpvReflectShaderModule *a,const SpvReflectShaderModule *b)
{
 assert(a->shader_stage==b->shader_stage&&a->descriptor_binding_count==b->descriptor_binding_count&&a->descriptor_set_count==b->descriptor_set_count&&a->push_constant_block_count==b->push_constant_block_count&&a->input_variable_count==b->input_variable_count&&a->output_variable_count==b->output_variable_count);
 for(unsigned i=0;i<a->descriptor_binding_count;i++){
  const SpvReflectDescriptorBinding *x=&a->descriptor_bindings[i],*y=&b->descriptor_bindings[i];name(x->name,y->name);assert(x->binding==y->binding&&x->set==y->set&&x->count==y->count&&x->descriptor_type==y->descriptor_type);block(&x->block,&y->block);
 }
 for(unsigned i=0;i<a->descriptor_set_count;i++){assert(a->descriptor_sets[i].set==b->descriptor_sets[i].set&&a->descriptor_sets[i].binding_count==b->descriptor_sets[i].binding_count);}
 for(unsigned i=0;i<a->push_constant_block_count;i++)block(&a->push_constant_blocks[i],&b->push_constant_blocks[i]);
}
/* Copy-mode descriptor-set enumeration is an independent baseline for the
 * production flat-binding layout extraction. */
static void layout_control(ShaderModuleInfo *i,const SpvReflectShaderModule *m){for(unsigned set=0;set<m->descriptor_set_count;set++)for(unsigned j=0;j<m->descriptor_sets[set].binding_count;j++){const SpvReflectDescriptorBinding *b=m->descriptor_sets[set].bindings[j];if(b->descriptor_type==SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER)block_to_uniforms(&b->block,&i->uniforms);}if(m->push_constant_block_count)block_to_uniforms(&m->push_constant_blocks[0],&i->push_constants);}
int main(void)
{
 static const struct{GlslcStage stage;const char *text;} programs[]={
 {GLSLC_STAGE_VERTEX,"#version 450\nlayout(set=0,binding=0,std140) uniform U { vec4 color; mat4 transform; vec3 lights[3]; } u; layout(push_constant) uniform P { vec4 attr[16]; } p; layout(location=0) out vec4 v; void main(){v=u.color;gl_Position=u.transform*p.attr[0];}"},
 {GLSLC_STAGE_FRAGMENT,"#version 450\nlayout(set=0,binding=1,std140) uniform F { vec4 color; float alpha; } f; layout(set=0,binding=2) uniform sampler2D t; layout(location=0) in vec4 v; layout(location=0) out vec4 o; void main(){o=texture(t,vec2(0.25))*v*f.color;if(o.a<f.alpha)discard;}"},
 {GLSLC_STAGE_GEOMETRY,"#version 450\nlayout(triangles) in; layout(triangle_strip,max_vertices=3) out; void main(){gl_Position=gl_in[0].gl_Position;EmitVertex();gl_Position=gl_in[1].gl_Position;EmitVertex();gl_Position=gl_in[2].gl_Position;EmitVertex();EndPrimitive();}"},
 {GLSLC_STAGE_COMPUTE,"#version 450\nlayout(local_size_x=1) in; layout(set=0,binding=3,std430) buffer B { uint values[]; } b; void main(){b.values[gl_GlobalInvocationID.x]=7u;}"}
 };
 unsigned cases=0;
 for(unsigned v=0;v<2;v++)for(unsigned i=0;i<sizeof(programs)/sizeof(programs[0]);i++){
  GlslcOptions opt={.spirv_1_6=v!=0};GlslcResult code;
  if(!glslc_compile(programs[i].stage,programs[i].text,&opt,&code)){fprintf(stderr,"stage=%u line=%d: %s\n",i,code.error_line,code.error);return 1;}
  size_t bytes=code.num_words*4;uint32_t *saved=malloc(bytes);assert(saved);memcpy(saved,code.words,bytes);
  GByteArray bytes_array={(uint8_t *)code.words,(unsigned)bytes};
  ShaderModuleInfo control={0},one={0},two={0};
  SpvReflectShaderModule copied,borrowed,also;
  assert(spvReflectCreateShaderModule(bytes,code.words,&copied)==SPV_REFLECT_RESULT_SUCCESS);
  assert(spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NO_COPY,bytes,code.words,&borrowed)==SPV_REFLECT_RESULT_SUCCESS);
  assert(spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NO_COPY,bytes,code.words,&also)==SPV_REFLECT_RESULT_SUCCESS);
  assert(spvReflectGetCode(&copied)!=code.words&&spvReflectGetCode(&borrowed)==code.words&&spvReflectGetCode(&also)==code.words);
  same(&copied,&borrowed);same(&copied,&also);layout_control(&control,&copied);
  unsigned count=borrowed_calls;init_layout_from_spv(&one,&bytes_array);init_layout_from_spv(&two,&bytes_array);assert(borrowed_calls==count+2);
  layout_same(&control.uniforms,&one.uniforms);layout_same(&control.push_constants,&one.push_constants);
  spvReflectDestroyShaderModule(&borrowed);assert(!memcmp(saved,code.words,bytes));same(&copied,&also);
  spvReflectDestroyShaderModule(&copied);spvReflectDestroyShaderModule(&also);assert(!memcmp(saved,code.words,bytes));
  free(saved);free(code.words); /* layouts must outlive all source/parser data */
  layout_same(&control.uniforms,&two.uniforms);layout_same(&control.push_constants,&two.push_constants);
  release(&one);release(&two);release(&control);assert(!allocations);cases++;
 }
 printf("PASS: %u real compiler/reflection cases: stages, uniforms, padding, arrays, push constants, descriptors and independent borrowed lifetimes; words unchanged\n",cases);
}
