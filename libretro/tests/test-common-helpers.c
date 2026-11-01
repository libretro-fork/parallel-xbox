/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
static void emit(const char *directory,unsigned api,unsigned kind,unsigned id,const PshState *state) {
  GenPshGlslOptions opts={.vulkan=api,.ubo_binding=0,.tex_binding=1};
  MString *text=pgraph_glsl_gen_psh(state,opts);char path[4096];
  int length=snprintf(path,sizeof(path),"%s/%u-%u-%u.frag",directory,api,kind,id);assert(length>0&&(size_t)length<sizeof(path));
  FILE *f=fopen(path,"wb");assert(f);size_t bytes=strlen(mstring_get_str(text));assert(fwrite(mstring_get_str(text),1,bytes,f)==bytes);assert(!fclose(f));
  if(api){GlslcResult result={0};GlslcOptions options={0};
   if(!glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(text),&options,&result)){fprintf(stderr,"%s: %s\n",path,result.error);abort();}
   length=snprintf(path,sizeof(path),"%s/%u-%u-%u.spv",directory,api,kind,id);assert(length>0&&(size_t)length<sizeof(path));
   f=fopen(path,"wb");assert(f);assert(fwrite(result.words,sizeof(uint32_t),result.num_words,f)==result.num_words);assert(!fclose(f));free(result.words);
  }
  mstring_unref(text);
}
int main(int argc,char **argv) {
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
 assert(count==772);printf("PASS: %u full fragment variants, including signed/cube helper dependencies; Vulkan variants compile\n",count);
}
