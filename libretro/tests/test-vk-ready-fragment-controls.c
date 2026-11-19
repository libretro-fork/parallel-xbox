/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Admission, final-slice packing, source identity and SPIR-V proof.
 * The separate numerical fixture is required for rendered correctness. */
#ifndef TEST_HAVE_BASELINE
#define TEST_HAVE_BASELINE 0
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
#include "hw/xbox/nv2a/pgraph/vk/glsl.h"
bool pgraph_glsl_psh_ready_vk2d_controls(const PshState*,uint32_t[8]);
MString *pgraph_glsl_gen_psh_ready_vk2d(const PshState*,GenPshGlslOptions);
MString *reference_gen_psh(const PshState*,GenPshGlslOptions);
MString *reference_gen_psh_ready(const PshState*,GenPshGlslOptions);
static PshState eligible(unsigned smooth,unsigned helper){PshState s={.smooth_shading=smooth,.depth_format=helper?DEPTH_FORMAT_HELPER_LIVENESS:0,.surface_zeta_format=NV097_SET_SURFACE_FORMAT_ZETA_Z24S8,.depth_clipping=true,.final_inputs_0=0x200c0000,.final_inputs_1=0x1c00};return s;}
static void drop(GlslcResult*r){glslc_result_free_words(r);free(r->uniforms.members);free(r->push_constants.members);}
static int member(const GlslcLayout*l,const char*n){for(size_t i=0;i<l->count;i++)if(!strcmp(l->members[i].name,n))return (int)i;return -1;}
int main(int argc,char**argv){
 assert(argc==2||argc==3);const char*outdir=argv[1];MString*bank[4]={0};unsigned cases=0,declined=0;
 GenPshGlslOptions vk={.vulkan=true,.ubo_binding=1,.tex_binding=2};
 for(unsigned smooth=0;smooth<2;smooth++)for(unsigned helper=0;helper<2;helper++)for(unsigned mask=0;mask<16;mask++)for(unsigned rect=0;rect<16;rect++){
  PshState s=eligible(smooth,helper);unsigned idx=smooth|(helper<<1);for(unsigned i=0;i<4;i++){s.shader_stage_program|=((mask>>i)&1)<<(i*5);s.dim_tex[i]=(mask>>i)&1?2:0;s.rect_tex[i]=((mask&rect)>>i)&1;s.alphakill[i]=((mask+i)&1);}
  s.alpha_test=rect&1;PshState original=s;uint32_t controls[8];memset(controls,0xa5,sizeof(controls));assert(pgraph_glsl_psh_ready_vk2d_controls(&s,controls));assert(!memcmp(&s,&original,sizeof(s)));assert(controls[0]==0&&controls[1]==s.shader_stage_program&&controls[2]==0&&controls[3]==0);for(unsigned i=0;i<4;i++)assert(controls[4+i]==s.rect_tex[i]);
  MString*g=pgraph_glsl_gen_psh_ready_vk2d(&s,vk);assert(g);if(!bank[idx]){bank[idx]=g;}else{assert(!strcmp(mstring_get_str(g),mstring_get_str(bank[idx])));mstring_unref(g);}
  /* Prospective mode must have NO effect on the ordinary or GL-ready APIs. */
  for(unsigned mode=0;mode<4;mode++){
   GenPshGlslOptions options=vk;options.vulkan=mode&1;options.locations=mode&2;MString*a=pgraph_glsl_gen_psh(&s,options),*b=reference_gen_psh(&s,options);assert(!strcmp(mstring_get_str(a),mstring_get_str(b)));mstring_unref(a);mstring_unref(b);
  }
  PshState gl=s;gl.depth_format|=DEPTH_FORMAT_HELPER_LIVENESS;GenPshGlslOptions options={.locations=true};MString*a=pgraph_glsl_gen_psh_ready(&gl,options),*b=reference_gen_psh_ready(&gl,options);assert(a&&b&&!strcmp(mstring_get_str(a),mstring_get_str(b)));mstring_unref(a);mstring_unref(b);cases++;
 }
 for(unsigned test=0;test<28;test++){
  PshState s=eligible(1,0);s.shader_stage_program=1;s.dim_tex[0]=2;
  switch(test){case 0:s.depth_format=DEPTH_FORMAT_D16;break;case 1:s.depth_format|=DEPTH_FORMAT_UI_CLAMP;break;case 2:s.surface_zeta_format=1;break;case 3:s.depth_clipping=false;break;case 4:s.z_perspective=true;break;case 5:s.window_clip_exclusive=true;break;case 6:s.point_sprite=true;break;case 7:s.combiner_control=9;break;case 8:s.final_inputs_0=s.final_inputs_1=0;break;case 9:s.shader_stage_program|=1u<<20;break;case 10:s.g8b8_mip_flags=1;break;case 11:s.g8b8_mip_clamp_u=1;break;case 12:s.g8b8_mip_clamp_v=1;break;case 13:s.g8b8_mip_mirror=1;break;case 14:s.shader_stage_program=PS_TEXTUREMODES_PROJECT3D;break;case 15:s.shader_stage_program=PS_TEXTUREMODES_CUBEMAP;break;case 16:s.shader_stage_program=PS_TEXTUREMODES_PASSTHRU;break;case 17:s.shader_stage_program=PS_TEXTUREMODES_CLIPPLANE;break;case 18:s.tex_cubemap[0]=true;break;case 19:s.shadow_map[0]=1;break;case 20:s.tex_x8y24[0]=true;break;case 21:s.g8b8_signed[0]=1;break;case 22:s.conv_tex[0]=CONVOLUTION_FILTER_GAUSSIAN;break;case 23:s.border_adjust[0]=true;break;case 24:s.colorkey_mode[0]=1;break;case 25:s.dim_tex[0]=3;break;case 26:s.rect_tex[3]=true;break;case 27:s.dim_tex[3]=3;break;}
  PshState original=s;uint32_t controls[8],before[8];memset(controls,0xa5,sizeof(controls));memcpy(before,controls,sizeof(before));assert(!pgraph_glsl_psh_ready_vk2d_controls(&s,controls));assert(!memcmp(before,controls,sizeof(before))&&!memcmp(&s,&original,sizeof(s)));assert(!pgraph_glsl_gen_psh_ready_vk2d(&s,vk));declined++;
 }
 unsigned raw_declined=0,raw_accepted=0;
 for(unsigned count=0;count<=8;count++)for(unsigned stage=0;stage<8;stage++)for(unsigned family=0;family<2;family++)for(unsigned byte=0;byte<4;byte++)for(unsigned reg_index=0;reg_index<4;reg_index++){
  const unsigned unsupported[]={6,7,14,15};unsigned reg=unsupported[reg_index];
  PshState raw=eligible(1,0);raw.combiner_control=count;uint32_t*word=family?raw.alpha_inputs:raw.rgb_inputs;word[stage]=reg<<(8*byte);PshState before=raw;uint32_t ctl[8],sentinel[8];memset(ctl,0xa5,32);memcpy(sentinel,ctl,32);bool accepted=pgraph_glsl_psh_ready_vk2d_controls(&raw,ctl);assert(pgraph_glsl_psh_ready_vk2d_combiner_eligible(raw.combiner_control,raw.rgb_inputs,raw.alpha_inputs)==(stage>=count));assert(accepted==(stage>=count));assert(!memcmp(&before,&raw,sizeof(raw)));if(stage<count){assert(!memcmp(ctl,sentinel,32));raw_declined++;}else raw_accepted++;
 }
 assert(raw_declined==1152&&raw_accepted==1152);
 {PshState raw=eligible(1,0);raw.combiner_dynamic=true;uint32_t ctl[8],before[8];memset(ctl,0xa5,32);memcpy(before,ctl,32);assert(!pgraph_glsl_psh_ready_vk2d_controls(&raw,ctl));assert(!memcmp(before,ctl,32));}
 /* Static-family admission must ignore stale unsafe program metadata, while
  * full raw admission and every-draw live admission still reject it. */
 {PshState raw=eligible(1,0);uint32_t expected[8],actual[8];assert(pgraph_glsl_psh_ready_vk2d_static_controls(&raw,expected));raw.combiner_dynamic=true;raw.combiner_control=255;raw.final_inputs_0=raw.final_inputs_1=0;memset(raw.rgb_inputs,0xff,sizeof(raw.rgb_inputs));memset(raw.alpha_inputs,0xff,sizeof(raw.alpha_inputs));memset(raw.rgb_outputs,0xff,sizeof(raw.rgb_outputs));memset(raw.alpha_outputs,0xff,sizeof(raw.alpha_outputs));assert(pgraph_glsl_psh_ready_vk2d_static_controls(&raw,actual));assert(!memcmp(expected,actual,32));assert(!pgraph_glsl_psh_ready_vk2d_program_eligible(raw.combiner_control,raw.rgb_inputs,raw.alpha_inputs,raw.rgb_outputs,raw.alpha_outputs,raw.final_inputs_0,raw.final_inputs_1));}
 unsigned output_declined=0,output_accepted=0;
 for(unsigned count=0;count<=8;count++)for(unsigned stage=0;stage<8;stage++)for(unsigned family=0;family<2;family++)for(unsigned nibble=0;nibble<3;nibble++)for(unsigned reg_index=0;reg_index<6;reg_index++){
  const unsigned unsupported[]={1,2,6,7,14,15};PshState raw=eligible(1,0);raw.combiner_control=count;uint32_t*word=family?raw.alpha_outputs:raw.rgb_outputs;word[stage]=unsupported[reg_index]<<(4*nibble);PshState before=raw;uint32_t ctl[8],sentinel[8];memset(ctl,0xa5,32);memcpy(sentinel,ctl,32);bool accepted=pgraph_glsl_psh_ready_vk2d_controls(&raw,ctl);assert(accepted==(stage>=count));assert(!memcmp(&before,&raw,sizeof(raw)));if(stage<count){assert(!memcmp(ctl,sentinel,32));output_declined++;}else output_accepted++;
 }
 assert(output_declined==1296&&output_accepted==1296);
 unsigned final_declined=0,final_accepted=0;
 for(unsigned input=0;input<7;input++)for(unsigned reg=0;reg<16;reg++)for(unsigned alpha=0;alpha<2;alpha++){
  PshState raw=eligible(1,0);unsigned shift=input<4?(3-input)*8:(7-input)*8;uint32_t*word=input<4?&raw.final_inputs_0:&raw.final_inputs_1;*word=(*word&~(255u<<shift))|((reg|(alpha?16u:0u))<<shift);
  bool expected=reg!=6&&reg!=7;
  if(input==0&&reg==14)expected=false;
  if(input<4&&reg>=14&&alpha)expected=false;
  if(input>=4&&reg>=14)expected=false;
  PshState before=raw;uint32_t ctl[8],sentinel[8];memset(ctl,0xa5,32);memcpy(sentinel,ctl,32);assert(pgraph_glsl_psh_ready_vk2d_controls(&raw,ctl)==expected);assert(!memcmp(&before,&raw,sizeof(raw)));if(expected)final_accepted++;else{assert(!memcmp(ctl,sentinel,32));final_declined++;}
 }
 assert(final_declined==49&&final_accepted==175);
 printf("RAW_PROGRAM_ADMISSION output_declined=%u output_accepted=%u final_declined=%u final_accepted=%u\n",output_declined,output_accepted,final_declined,final_accepted);
 printf("RAW_INPUT_ADMISSION active_unsupported_declined=%u inactive_unsupported_accepted=%u already_dynamic_declined=1\n",raw_declined,raw_accepted);
 unsigned compiled=0;
 for(unsigned idx=0;idx<4;idx++){
  const char*text=mstring_get_str(bank[idx]);assert(strstr(text,"cregs[12] = vec4(0.0, 0.0, 0.0, t0.a);")&&strstr(text,"vec4 t0=mix(vec4(0.0,0.0,0.0,1.0),vkSample0,bvec4(vkMode0==1u));"));assert(!!strstr(text,"bool fsKill;")==!!(idx&2));
  PshState ordinary=eligible(idx&1,!!(idx&2));ordinary.combiner_dynamic=ordinary.alpha_dynamic=true;ordinary.final_inputs_0=ordinary.final_inputs_1=0;MString*normal=pgraph_glsl_gen_psh(&ordinary,vk);
  for(unsigned version=0;version<2;version++){
   GlslcOptions options={.spirv_1_6=version,.emit_layout=true,.module_prefix_words=256};GlslcResult a={0},n={0};assert(glslc_compile(GLSLC_STAGE_FRAGMENT,text,&options,&a));assert(glslc_compile(GLSLC_STAGE_FRAGMENT,mstring_get_str(normal),&options,&n));
   assert(n.uniforms.size==1616&&a.uniforms.size==1648&&a.uniforms.count==n.uniforms.count+1);int loc=member(&a.uniforms,"fsControl");assert(loc>=0);const GlslcUniform*u=&a.uniforms.members[loc];assert(u->offset==1616&&u->stride==16&&u->dim_v==4&&u->dim_a==2);
   for(size_t i=0;i<n.uniforms.count;i++){const GlslcUniform*old=&n.uniforms.members[i];int j=member(&a.uniforms,old->name);assert(j>=0);const GlslcUniform*cur=&a.uniforms.members[j];assert(old->offset==cur->offset&&old->stride==cur->stride&&old->dim_v==cur->dim_v&&old->dim_a==cur->dim_a);}
   uint8_t storage[2048];memset(storage,0xa5,sizeof(storage));ShaderUniformLayout final={.uniforms=a.uniforms.members,.num_uniforms=a.uniforms.count,.total_size=a.uniforms.size,.allocation=storage+16};uint32_t controls[8]={0,0x8421,0,0,1,0,1,0};uniform_update(&final,loc+1,controls,sizeof(controls),true);assert(!memcmp(storage+16+1616,controls,32));for(unsigned i=0;i<16+1616;i++)assert(storage[i]==0xa5);for(unsigned i=16+1648;i<sizeof(storage);i++)assert(storage[i]==0xa5);
   char path[4096];assert(snprintf(path,sizeof(path),"%s/ready2d-%u-spv%u.spv",outdir,idx,version)>0);FILE*f=fopen(path,"wb");assert(f&&fwrite(a.words,4,a.num_words,f)==a.num_words);assert(!fclose(f));assert(snprintf(path,sizeof(path),"%s/ready2d-%u.glsl",outdir,idx)>0);f=fopen(path,"wb");assert(f&&fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));printf("READY2D_COMPILE variant=%u SPV=%u words=%zu uniform_bytes=%zu controls_offset=%zu\n",idx,version,a.num_words,a.uniforms.size,u->offset);drop(&a);drop(&n);compiled++;
  }
  mstring_unref(normal);mstring_unref(bank[idx]);
 }
 if(argc==3){FILE*seed=fopen(argv[2],"rb");assert(seed);uint32_t hdr[4];assert(fread(hdr,16,1,seed)==1&&hdr[0]==0x44455358);unsigned raw_records=0,covered_records=0,unique=0,covered_unique=0;static PshState keys[256];
 for(unsigned sec=0;sec<hdr[2];sec++){uint32_t h[4];assert(fread(h,16,1,seed)==1);if(h[0]==3){assert(h[1]==sizeof(ShaderState)&&h[2]<=256);for(unsigned i=0;i<h[2];i++){ShaderState s;assert(fread(&s,sizeof(s),1,seed)==1);uint32_t controls[8];bool covered=pgraph_glsl_psh_ready_vk2d_controls(&s.psh,controls);raw_records++;covered_records+=covered;unsigned j=0;for(;j<unique;j++)if(!memcmp(&keys[j],&s.psh,sizeof(s.psh)))break;if(j==unique){keys[unique++]=s.psh;covered_unique+=covered;}}}else assert(!fseek(seed,(long)h[1]*h[2],SEEK_CUR));}fclose(seed);assert(raw_records==64&&unique==35&&covered_unique==27);
 printf("CAPTURED_SEED recipes=%u declined=%u modules=%u seed_records=%u covered_records=%u exact_FS=%u covered_exact_FS=%u\n",cases,declined,compiled,raw_records,covered_records,unique,covered_unique);}
 printf("READY2D_CPU_PASS recipes=%u declined=%u raw_declined=%u raw_accepted=%u modules=%u GL_source_identity=%s GPU_NUMERICAL=SEPARATE_REQUIRED\n",cases,declined,raw_declined,raw_accepted,compiled,TEST_HAVE_BASELINE?"baseline_byte_identical":"UNQUALIFIED_no_baseline");return 0;
}
