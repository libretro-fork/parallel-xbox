/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
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
  GenPshGlslOptions opts={.vulkan=api,.ubo_binding=0,.tex_binding=1};
  MString *text=pgraph_glsl_gen_psh(&state,opts);char path[4096];
  int length=snprintf(path,sizeof(path),"%s/%u-%u-%u-%u-%u.frag",argv[1],api,mode,mapping,n,cube);assert(length>0&&(size_t)length<sizeof(path));
  FILE *f=fopen(path,"wb");assert(f);size_t bytes=strlen(mstring_get_str(text));assert(fwrite(mstring_get_str(text),1,bytes,f)==bytes);assert(!fclose(f));
  if(api){GlslcResult result={0};GlslcOptions options={0};
   if(!glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(text),&options,&result)){fprintf(stderr,"%s: %s\n",path,result.error);abort();}
   length=snprintf(path,sizeof(path),"%s/%u-%u-%u-%u-%u.spv",argv[1],api,mode,mapping,n,cube);assert(length>0&&(size_t)length<sizeof(path));
   f=fopen(path,"wb");assert(f);assert(fwrite(result.words,sizeof(uint32_t),result.num_words,f)==result.num_words);assert(!fclose(f));free(result.words);
  }
  mstring_unref(text);count++;
 }
 assert(count==448);printf("PASS: %u full fragment variants, including look-ahead dot mapping; Vulkan variants compile\n",count);
}
