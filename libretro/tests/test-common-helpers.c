/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
static unsigned convolution_equivalence_checks, clip_equivalence_checks, shadow_equivalence_checks, shaderctl_equivalence_checks;
static void emit(const char *directory,unsigned api,unsigned kind,unsigned id,const PshState *state) {
  GenPshGlslOptions opts={.vulkan=api,.ubo_binding=0,.tex_binding=1};
  MString *text=pgraph_glsl_gen_psh(state,opts);char path[4096];
  for(unsigned bit=0;bit<=32;bit++) {
    uint32_t ignored=0xFF00F000u;
    uint32_t change=bit==32?ignored:((1u<<bit)&ignored);
    if(!change)continue;
    PshState variant;memcpy(&variant,state,sizeof(variant));variant.other_stage_input^=change;
    MString *other=pgraph_glsl_gen_psh(&variant,opts);
    assert(!strcmp(mstring_get_str(text),mstring_get_str(other)));
    mstring_unref(other);shaderctl_equivalence_checks++;
  }
  bool shadow_comparison=false;
  for(unsigned i=0;i<4;i++) {
    unsigned mode=(state->shader_stage_program>>(i*5))&31;
    shadow_comparison|=state->shadow_map[i]&&
      (mode==PS_TEXTUREMODES_PROJECT2D||mode==PS_TEXTUREMODES_PROJECT3D);
  }
  if(!shadow_comparison)for(unsigned func=0;func<8;func++) {
    PshState variant;memcpy(&variant,state,sizeof(variant));variant.shadow_depth_func=func;
    MString *other=pgraph_glsl_gen_psh(&variant,opts);
    assert(!strcmp(mstring_get_str(text),mstring_get_str(other)));
    mstring_unref(other);shadow_equivalence_checks++;
  }
  for(unsigned i=0;i<4;i++) {
    unsigned mode=(state->shader_stage_program>>(i*5))&31;
    if(mode==PS_TEXTUREMODES_CLIPPLANE)continue;
    const unsigned masks[]={0,1,2,4,8,15};
    for(unsigned k=0;k<ARRAY_SIZE(masks);k++) {
      PshState variant;memcpy(&variant,state,sizeof(variant));
      for(unsigned j=0;j<4;j++)variant.compare_mode[i][j]=(masks[k]>>j)&1;
      MString *other=pgraph_glsl_gen_psh(&variant,opts);
      assert(!strcmp(mstring_get_str(text),mstring_get_str(other)));
      mstring_unref(other);clip_equivalence_checks++;
    }
  }
  /* Unused convolution settings must not change any generated source byte.
   * Cover each stage independently, including mixed active/unused stages. */
  for (unsigned i=0;i<4;i++) {
    unsigned mode=(state->shader_stage_program>>(i*5))&31;
    if (mode==PS_TEXTUREMODES_PROJECT2D&&!state->shadow_map[i])continue;
    const enum ConvolutionFilter kernels[]={CONVOLUTION_FILTER_DISABLED,
      CONVOLUTION_FILTER_QUINCUNX,CONVOLUTION_FILTER_GAUSSIAN};
    for(unsigned k=0;k<ARRAY_SIZE(kernels);k++) {
      PshState variant;memcpy(&variant,state,sizeof(variant));
      variant.conv_tex[i]=kernels[k];
      MString *other=pgraph_glsl_gen_psh(&variant,opts);
      assert(!strcmp(mstring_get_str(text),mstring_get_str(other)));
      mstring_unref(other);convolution_equivalence_checks++;
    }
  }
  int length=snprintf(path,sizeof(path),"%s/%u-%u-%u.frag",directory,api,kind,id);assert(length>0&&(size_t)length<sizeof(path));
  FILE *f=fopen(path,"wb");assert(f);size_t bytes=strlen(mstring_get_str(text));assert(fwrite(mstring_get_str(text),1,bytes,f)==bytes);assert(!fclose(f));
  if(api){GlslcResult result={0};GlslcOptions options={0};
   if(!glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(text),&options,&result)){fprintf(stderr,"%s: %s\n",path,result.error);abort();}
   length=snprintf(path,sizeof(path),"%s/%u-%u-%u.spv",directory,api,kind,id);assert(length>0&&(size_t)length<sizeof(path));
   f=fopen(path,"wb");assert(f);assert(fwrite(result.words,sizeof(uint32_t),result.num_words,f)==result.num_words);assert(!fclose(f));free(result.words);
  }
  mstring_unref(text);
}
static void check_active_clip(void) {
 for(unsigned api=0;api<2;api++)for(unsigned i=0;i<4;i++) {
  PshState state={0};state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=true;
  state.shader_stage_program=PS_TEXTUREMODES_CLIPPLANE<<(i*5);
  state.final_inputs_0=PS_REGISTER_V0;
  GenPshGlslOptions opts={.vulkan=api,.ubo_binding=0,.tex_binding=1};
  MString *baseline=pgraph_glsl_gen_psh(&state,opts);
  for(unsigned mask=0;mask<16;mask++) {
   for(unsigned j=0;j<4;j++)state.compare_mode[i][j]=(mask>>j)&1;
   MString *text=pgraph_glsl_gen_psh(&state,opts);
   assert((strcmp(mstring_get_str(baseline),mstring_get_str(text))!=0)==(mask!=0));
   if(api) {
    GlslcOptions options={0};GlslcResult result={0};
    assert(glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(text),&options,&result));
    free(result.words);
   }
   mstring_unref(text);
  }
  mstring_unref(baseline);
 }
 printf("PASS: 128 active clip comparison programs; 64 Vulkan variants compile\n");
}
static void check_active_shadow(void) {
 unsigned count=0;
 for(unsigned api=0;api<2;api++)for(unsigned project=0;project<2;project++)
 for(unsigned floating=0;floating<2;floating++)for(unsigned i=0;i<4;i++) {
  PshState state={0};state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=true;
  state.shader_stage_program=(project?PS_TEXTUREMODES_PROJECT3D:PS_TEXTUREMODES_PROJECT2D)<<(i*5);
  state.dim_tex[i]=2;state.shadow_map[i]=floating?18:1;state.final_inputs_0=PS_REGISTER_T0+i;
  GenPshGlslOptions opts={.vulkan=api,.ubo_binding=0,.tex_binding=1};
  MString *baseline=pgraph_glsl_gen_psh(&state,opts);
  for(unsigned func=0;func<8;func++) {
   state.shadow_depth_func=func;
   MString *text=pgraph_glsl_gen_psh(&state,opts);
   assert((strcmp(mstring_get_str(baseline),mstring_get_str(text))!=0)==(func!=0));
   if(api) {
    GlslcOptions options={0};GlslcResult result={0};
    assert(glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(text),&options,&result));
    free(result.words);
   }
   mstring_unref(text);count++;
  }
  mstring_unref(baseline);
 }
 assert(count==256);printf("PASS: 256 active shadow programs; 128 Vulkan variants compile\n");
}
static void check_active_input_selectors(void) {
 unsigned count=0;
 for(unsigned api=0;api<2;api++)for(unsigned gb=0;gb<2;gb++)for(unsigned stage=2;stage<4;stage++) {
  PshState state={0};state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=true;
  for(unsigned i=0;i<=stage;i++) {
   state.dim_tex[i]=2;
   state.shader_stage_program|=(i==stage?(gb?PS_TEXTUREMODES_DPNDNT_GB:PS_TEXTUREMODES_DPNDNT_AR):PS_TEXTUREMODES_PROJECT2D)<<(i*5);
  }
  state.final_inputs_0=PS_REGISTER_T0+stage;
  GenPshGlslOptions opts={.vulkan=api,.ubo_binding=0,.tex_binding=1};
  MString *baseline=pgraph_glsl_gen_psh(&state,opts);
  for(unsigned input=0;input<stage;input++) {
   state.other_stage_input=input<<(16+4*(stage-2));
   MString *text=pgraph_glsl_gen_psh(&state,opts);
   assert((strcmp(mstring_get_str(baseline),mstring_get_str(text))!=0)==(input!=0));
   if(api) {
    GlslcOptions options={0};GlslcResult result={0};
    assert(glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(text),&options,&result));free(result.words);
   }
   mstring_unref(text);count++;
  }
  mstring_unref(baseline);
 }
 assert(count==20);printf("PASS: 20 active stage-input programs; 10 Vulkan variants compile\n");
}
int main(int argc,char **argv) {
 check_active_input_selectors();
 check_active_shadow();
 check_active_clip();
 assert(argc==2);unsigned count=0;
 for(unsigned api=0;api<2;api++)for(unsigned mode=0;mode<11;mode++)
 for(unsigned mapping=0;mapping<8;mapping++)
 for(unsigned n=0;n<(mode==6?8u:1u);n++)
 for(unsigned cube=0;cube<((mode==6||mode==7||mode==9)?2u:1u);cube++) {
  unsigned next=mode==6?n:(mapping+1)%8,third=(mapping+2)%8;
  if(mode==3||mode==4||mode==5||mode==6||mode==8)third=next=mapping;
  if(mode==6)third=n;
  PshState state={0};state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=true;
  unsigned modes[4]={0};for(unsigned i=0;i<4;i++)state.dim_tex[i]=2;
  if(mode==1||mode==2){for(unsigned i=0;i<4;i++)modes[i]=PS_TEXTUREMODES_PROJECT2D;}
  if(mode>=3)modes[0]=PS_TEXTUREMODES_PROJECT2D;
  if(mode>=3&&mode<=9)modes[1]=PS_TEXTUREMODES_DOTPRODUCT;
  if(mode==3||mode==7||mode==8||mode==9)modes[2]=PS_TEXTUREMODES_DOTPRODUCT;
  if(mode==4)modes[2]=PS_TEXTUREMODES_DOT_ST;
  if(mode==5)modes[2]=PS_TEXTUREMODES_DOT_ZW;
  if(mode==6)modes[2]=PS_TEXTUREMODES_DOT_RFLCT_DIFF;
  if(mode==7)modes[3]=PS_TEXTUREMODES_DOT_RFLCT_SPEC;
  if(mode==8){modes[3]=PS_TEXTUREMODES_DOT_STR_3D;state.dim_tex[3]=3;}
  if(mode==9)modes[3]=PS_TEXTUREMODES_DOT_STR_CUBE;
  if(mode==10)modes[3]=PS_TEXTUREMODES_DOT_RFLCT_SPEC_CONST;
  if(mode==6)state.tex_cubemap[2]=cube;
  if(mode==7||mode==9)state.tex_cubemap[3]=cube;
  if(mode==4)state.conv_tex[0]=CONVOLUTION_FILTER_GAUSSIAN;
  state.other_stage_input=mapping|(next<<4)|(third<<8);
  for(unsigned i=0;i<4;i++){state.shader_stage_program|=modes[i]<<(i*5);state.shadow_map[i]=mode==2;}
  state.shadow_depth_func=SHADOW_DEPTH_FUNC_ALWAYS;
  unsigned last=mode==0?0:mode<=2?0:mode<=6?2:3;
  state.final_inputs_0=mode==0?PS_REGISTER_V0:PS_REGISTER_T0+last;
  emit(argv[1],api,0,count++,&state);
 }

 for(unsigned api=0;api<2;api++) {
  const unsigned filters[]={0,16,64|32,64|16,128|16,128|16|64};
  for(unsigned k=0;k<6;k++)for(unsigned signs=1;signs<16;signs++)
  for(unsigned rect=0;rect<((filters[k]&128)?1u:2u);rect++) {
   PshState state={0};state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=true;
   state.dim_tex[0]=2;state.rect_tex[0]=rect;state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D;
   state.final_inputs_0=PS_REGISTER_T0;state.g8b8_signed[0]=signs|filters[k];
   emit(argv[1],api,1,count++,&state);
  }
  for(unsigned lum=0;lum<2;lum++)for(unsigned gb=0;gb<2;gb++)for(unsigned dim=2;dim<=3;dim++) {
   PshState state={0};state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=true;
   state.dim_tex[0]=2;state.dim_tex[1]=dim;
   state.shader_stage_program=PS_TEXTUREMODES_PROJECT2D|((lum?PS_TEXTUREMODES_BUMPENVMAP_LUM:PS_TEXTUREMODES_BUMPENVMAP)<<5);
   state.g8b8_signed[0]=gb?19:0;state.final_inputs_0=PS_REGISTER_T0+1;
   emit(argv[1],api,2,count++,&state);
  }
  for(unsigned project=0;project<2;project++)for(unsigned cube=0;cube<2;cube++) {
   PshState state={0};state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=true;
   state.dim_tex[0]=2;state.tex_cubemap[0]=cube;
   state.shader_stage_program=project?PS_TEXTUREMODES_PROJECT2D:PS_TEXTUREMODES_CUBEMAP;
   state.final_inputs_0=PS_REGISTER_T0;emit(argv[1],api,3,count++,&state);
  }
 }
 assert(shaderctl_equivalence_checks==10036);
 printf("PASS: %u byte-identical ignored-SHADERCTL source comparisons\n",shaderctl_equivalence_checks);
 printf("PASS: %u byte-identical unused-shadow-function source comparisons\n",shadow_equivalence_checks);
 assert(clip_equivalence_checks==18528);
 printf("PASS: %u byte-identical unused-clip source comparisons\n",clip_equivalence_checks);
 assert(convolution_equivalence_checks==6912);
 printf("PASS: %u byte-identical unused-convolution source comparisons\n",convolution_equivalence_checks);
 assert(count==772);printf("PASS: %u full fragment variants, including signed/cube helper dependencies; Vulkan variants compile\n",count);
}
