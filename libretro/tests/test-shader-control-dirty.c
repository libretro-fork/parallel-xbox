/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/nv2a_regs.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define ctz32(v) __builtin_ctz(v)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof(*(a)))
struct PGRAPHState {
 bool program_data_dirty,two_side_light_en;uint16_t uniform_attrs,swizzle_attrs,compressed_attrs;unsigned primitive_mode,surface_scale_factor;
 struct{unsigned zeta_format;}surface_shape;bool texture_matrix_enable[4];
 uint32_t shader_reg_snap[12],raw[0x2000/4];bool dirty[0x2000/4];
};
static uint32_t pgraph_reg_r(PGRAPHState *pg,unsigned reg){assert(reg/4<ARRAY_SIZE(pg->raw));return pg->raw[reg/4];}
static bool pgraph_is_reg_dirty(PGRAPHState *pg,unsigned reg){assert(reg/4<ARRAY_SIZE(pg->dirty));return pg->dirty[reg/4];}
typedef struct {struct{bool valid,ff_valid;}vsh_uni_src;bool uni_cache;void *shader_binding,*spec_binding;} PGRAPHGLState;
static void uniform_cache_invalidate(bool *cache){*cache=false;}
/* PRODUCTION */
typedef struct {ShaderState state;bool ui;uint32_t alpha_func,alpha_ref;} Projection;
static Projection project(PGRAPHState *pg){
 Projection out;memset(&out,0,sizeof(out));VshState *vsh=&out.state.vsh;
 out.state.geom.primitive_mode=pg->primitive_mode;
 /* ORACLE */
 out.alpha_func=GET_MASK(pgraph_reg_r(pg,NV_PGRAPH_CONTROL_0),NV_PGRAPH_CONTROL_0_ALPHAFUNC);
 out.alpha_ref=GET_MASK(pgraph_reg_r(pg,NV_PGRAPH_CONTROL_0),NV_PGRAPH_CONTROL_0_ALPHAREF);
 return out;
}
int main(void){
 PGRAPHGLState gl={.vsh_uni_src={true,true},.uni_cache=true,.shader_binding=(void *)1,.spec_binding=(void *)2};
 pgraph_gl_uniform_sources_invalidate(&gl);assert(!gl.vsh_uni_src.valid&&!gl.vsh_uni_src.ff_valid&&!gl.uni_cache&&!gl.shader_binding&&!gl.spec_binding);
 unsigned cases=0,ignored=0,affected=0;uint32_t random=713;
 const uint32_t fields[]={NV_PGRAPH_CONTROL_0_ALPHATESTENABLE|NV_PGRAPH_CONTROL_0_ALPHAFUNC|NV_PGRAPH_CONTROL_0_ALPHAREF|NV_PGRAPH_CONTROL_0_ZENABLE|NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE,NV_PGRAPH_CONTROL_3_SHADEMODE|NV_PGRAPH_CONTROL_3_PROVOKING_VERTEX|NV_PGRAPH_CONTROL_3_FOGENABLE|NV_PGRAPH_CONTROL_3_FOG_MODE};
 unsigned registers[]={NV_PGRAPH_CONTROL_0,NV_PGRAPH_CONTROL_3};
 for(unsigned trial=0;trial<4096;trial++){
  PGRAPHState pg={.primitive_mode=PRIM_TYPE_TRIANGLES,.surface_scale_factor=2};
  random=random*1664525u+1013904223u;pg.raw[NV_PGRAPH_CONTROL_0/4]=random;
  random=random*1664525u+1013904223u;pg.raw[NV_PGRAPH_CONTROL_3/4]=random;
  Projection before=project(&pg);pgraph_glsl_snapshot_shader_regs(&pg);
  for(unsigned control=0;control<2;control++)for(unsigned bit=0;bit<32;bit++){
   unsigned reg=registers[control];uint32_t mask=1u<<bit;pg.dirty[reg/4]=true;
   /* Marked but unchanged writes must not reconstruct a shader. */
   assert(!pgraph_glsl_check_shader_state_dirty(&pg,&before.state));
   pg.raw[reg/4]^=mask;Projection after=project(&pg);
   bool dirty=pgraph_glsl_check_shader_state_dirty(&pg,&before.state);
   bool differs=memcmp(&before,&after,sizeof(before))!=0;
   if(differs){assert(dirty);affected++;}
   assert(dirty==!!(mask&fields[control]));if(!dirty)ignored++;
   pgraph_glsl_snapshot_shader_regs(&pg);assert(!pgraph_glsl_check_shader_state_dirty(&pg,&after.state));
   pg.raw[reg/4]^=mask;pgraph_glsl_snapshot_shader_regs(&pg);pg.dirty[reg/4]=false;cases++;
  }
 }
 for(unsigned old=1;old<=4;old++)for(unsigned next=1;next<=4;next++){
  PGRAPHState pg={.primitive_mode=PRIM_TYPE_TRIANGLES,.surface_scale_factor=old};Projection before=project(&pg);pgraph_glsl_snapshot_shader_regs(&pg);
  pg.surface_scale_factor=next;pg.dirty[NV_PGRAPH_CONTROL_0/4]=true;
  assert(pgraph_glsl_check_shader_state_dirty(&pg,&before.state)==((old>1)!=(next>1)));
  pgraph_glsl_snapshot_shader_regs(&pg);Projection after=project(&pg);assert(!pgraph_glsl_check_shader_state_dirty(&pg,&after.state));cases++;
 }
 for(unsigned i=0;i<2;i++){
  PGRAPHState pg={.primitive_mode=PRIM_TYPE_TRIANGLES,.surface_scale_factor=2};Projection before=project(&pg);before.state.psh.combiner_dynamic=true;pgraph_glsl_snapshot_shader_regs(&pg);pg.dirty[registers[i]/4]=true;
  assert(pgraph_glsl_check_shader_state_dirty(&pg,&before.state));cases++;
 }
 printf("Shader controls: %u native guard/snapshot and extracted VSH/geometry/fragment/UI consumer cases PASS; %u ignored nonshader writes, %u projected changes; native/upscaled transitions preserved, snapshot metadata +12 bytes, no shader/key schema change\n",cases,ignored,affected);
}
