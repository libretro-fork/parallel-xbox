/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/nv2a/pgraph/gl/ready-ff-state.h"
static unsigned rng=0x81742;
static unsigned next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void noise(VshState *s){
 s->z_perspective=next()&1;s->fog_enable=next()&1;s->fog_mode=next()%8;
 s->specular_enable=next()&1;s->separate_specular=next()&1;s->ignore_specular_alpha=next()&1;
 FixedFunctionVshState *f=&s->fixed_function;
 f->normalization=next()&1;f->foggen=next()%5;f->lighting=next()&1;f->light_dynamic=next()&1;
 f->local_eye=next()&1;f->two_sided=next()&1;
 f->emission_src=next()%3;f->ambient_src=next()%3;f->diffuse_src=next()%3;f->specular_src=next()%3;
 for(unsigned i=0;i<8;i++)f->light[i]=next()%4;
 for(unsigned i=0;i<4;i++){f->texture_matrix_enable[i]=next()&1;for(unsigned j=0;j<4;j++)f->texgen[i][j]=next()%6;}
 for(unsigned i=0;i<NV2A_MAX_TRANSFORM_PROGRAM_LENGTH;i++)for(unsigned j=0;j<4;j++)s->programmable.program_data[i][j]=next();
 s->programmable.program_length=next()%NV2A_MAX_TRANSFORM_PROGRAM_LENGTH;
}
int main(void){
 MString *bank[GL_READY_FF_STAGE_COUNT];unsigned checks=0;
 for(unsigned i=0;i<GL_READY_FF_STAGE_COUNT;i++){
  VshState state;ready_ff_state_init(&state,i);
  GenVshGlslOptions opts={.locations=true,.prefix_outputs=i&1};
  assert(ready_ff_state_index(&state,i&1)==(int)i);
  bank[i]=pgraph_glsl_gen_vsh(&state,opts);
  for(unsigned k=0;k<256;k++){
   noise(&state);assert(ready_ff_state_index(&state,i&1)==(int)i);
   MString *actual=pgraph_glsl_gen_vsh(&state,opts);
   if(strcmp(mstring_get_str(actual),mstring_get_str(bank[i]))){fprintf(stderr,"SOURCE_MISMATCH index=%u sample=%u\n",i,k);return 1;}
   checks++;mstring_unref(actual);
  }
  state.compressed_attrs=1;assert(ready_ff_state_index(&state,i&1)<0);state.compressed_attrs=0;
  state.uniform_attrs=1;assert(ready_ff_state_index(&state,i&1)<0);state.uniform_attrs=0;
  state.swizzle_attrs=1;assert(ready_ff_state_index(&state,i&1)<0);state.swizzle_attrs=0;
  state.const_attrs_dynamic=true;assert(ready_ff_state_index(&state,i&1)<0);state.const_attrs_dynamic=false;
  state.is_fixed_function=false;assert(ready_ff_state_index(&state,i&1)<0);state.is_fixed_function=true;
  state.fog_dynamic=false;assert(ready_ff_state_index(&state,i&1)<0);state.fog_dynamic=true;
  state.fixed_function.csv0c_dynamic=false;assert(ready_ff_state_index(&state,i&1)<0);state.fixed_function.csv0c_dynamic=true;
  state.fixed_function.texgen_dynamic=false;assert(ready_ff_state_index(&state,i&1)<0);state.fixed_function.texgen_dynamic=true;
  state.fixed_function.skinning=7;assert(ready_ff_state_index(&state,i&1)<0);
 }
 for(unsigned i=0;i<GL_READY_FF_STAGE_COUNT;i++)for(unsigned j=0;j<i;j++)
  assert(strcmp(mstring_get_str(bank[i]),mstring_get_str(bank[j])));
 for(unsigned i=0;i<GL_READY_FF_STAGE_COUNT;i++)mstring_unref(bank[i]);
 printf("PASS FF source projection: 56 distinct ordinary sources, %u exact comparisons, every unsupported boundary rejected\n",checks);
}
