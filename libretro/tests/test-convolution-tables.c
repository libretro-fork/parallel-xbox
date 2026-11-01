/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
int main(int argc,char **argv) {
 assert(argc==2);unsigned count=0;
 for(unsigned api=0;api<2;api++)for(unsigned mode=0;mode<3;mode++)
 for(unsigned rect=0;rect<2;rect++)for(unsigned kernel=1;kernel<=2;kernel++)
 for(unsigned mask=0;mask<16;mask++) {
  PshState state={0};state.depth_format=DEPTH_FORMAT_D24;state.smooth_shading=true;
  state.final_inputs_0=mode?PS_REGISTER_T0:PS_REGISTER_V0;
  for(unsigned i=0;i<4;i++) {
   state.dim_tex[i]=2;state.rect_tex[i]=rect;
   if(mode)state.shader_stage_program|=PS_TEXTUREMODES_PROJECT2D<<(i*5);
   if(mask&(1u<<i))state.conv_tex[i]=kernel==1?CONVOLUTION_FILTER_QUINCUNX:CONVOLUTION_FILTER_GAUSSIAN;
   state.shadow_map[i]=mode==2;state.shadow_depth_func=SHADOW_DEPTH_FUNC_ALWAYS;
  }
  GenPshGlslOptions opts={.vulkan=api,.ubo_binding=0,.tex_binding=1};
  MString *s=pgraph_glsl_gen_psh(&state,opts);char path[4096];
  int n=snprintf(path,sizeof(path),"%s/%u-%u-%u-%u-%u.frag",argv[1],api,mode,rect,kernel,mask);assert(n>0&&(size_t)n<sizeof(path));
  FILE *f=fopen(path,"wb");assert(f);size_t length=strlen(mstring_get_str(s));assert(fwrite(mstring_get_str(s),1,length,f)==length);assert(!fclose(f));
  if(api){GlslcResult result={0};GlslcOptions options={0};if(!glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(s),&options,&result)){fprintf(stderr,"%s: %s\n",path,result.error);abort();}n=snprintf(path,sizeof(path),"%s/%u-%u-%u-%u-%u.spv",argv[1],api,mode,rect,kernel,mask);assert(n>0&&(size_t)n<sizeof(path));
   f=fopen(path,"wb");assert(f);assert(fwrite(result.words,sizeof(uint32_t),result.num_words,f)==result.num_words);assert(!fclose(f));free(result.words);}
  mstring_unref(s);count++;
 }
 printf("PASS: %u production fragment generators; Vulkan variants compile with the core compiler\n",count);
}
