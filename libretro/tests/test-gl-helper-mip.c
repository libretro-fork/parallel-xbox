/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#ifdef _WIN32
#include "ui/gl-dispatch.h"
#include "gloffscreen.h"
extern void libretro_gl_set_standalone_mode(void);
#else
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define REQUIRE(x) do{if(!(x)){fprintf(stderr,"helper mip setup failed line=%u: %s\n",__LINE__,#x);exit(1);}}while(0)
/* CONTEXT */
/* PROGRAM */
/* UNIFORMS */
#ifdef PGRAPH_GLSL_GL_DEFAULT_HELPERS
/* STATES */
static void source_equivalence(void){
 for(unsigned test=0;test<1308;test++){
  PshState keyed=state_for(test),plain=keyed;
  keyed.depth_format|=DEPTH_FORMAT_HELPER_LIVENESS;
  plain.depth_format&=~DEPTH_FORMAT_HELPER_LIVENESS;
  MString *a=pgraph_glsl_gen_psh(&keyed,(GenPshGlslOptions){.locations=true});
  MString *b=pgraph_glsl_gen_psh(&plain,(GenPshGlslOptions){.locations=true});
  REQUIRE(a&&b);
  if(strcmp(mstring_get_str(a),mstring_get_str(b))){fprintf(stderr,"MIP_SOURCE_FAIL case=%u\n",test);exit(1);}
  mstring_unref(a);mstring_unref(b);
 }
 puts("PASS default helper source: 1308 identical ordinary default/keyed sources");
}
#endif
static void check_pixel(const float actual[4],const float expected[4],unsigned path,unsigned masked){
 if(memcmp(actual,expected,4*sizeof(float))){
  fprintf(stderr,"MIP_ORACLE_FAIL path=%u masked=%u actual=%a,%a,%a,%a expected=%a,%a,%a,%a\n",path,masked,actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]);exit(1);
 }
}
/* Functional float32 positions/coordinates from a thin perspective triangle.
 * No disc image, savestate, guest texture pixels or other game assets. */
static const uint32_t clip_words[3][4]={
 {0x3f88f3b5u,0x3f56e036u,0x3f86ee82u,0x3fa087a0u},
 {0x3ff71d2au,0x3fda8992u,0x40230ed2u,0x402fdb10u},
 {0x3f1f9b95u,0x3f35be3du,0x3f862d12u,0x3f9fc630u}
};
static const uint32_t uv_words[3][4]={
 {0x3fc34131u,0x40675a86u,0x00000000u,0x3f800000u},
 {0x3fa530f0u,0x4035f20fu,0x00000000u,0x3f800000u},
 {0x3fd2f67bu,0x40684239u,0x00000000u,0x3f800000u}
};
static const uint32_t plane_words[3][4]={
 {0x4426c800u,0x43c85000u,0x4b6b96cdu,0x3fa087a0u},
 {0x44193c00u,0x43c29000u,0x4b76af51u,0x402fdb10u},
 {0x4406f400u,0x43bc4000u,0x4b6b7e18u,0x3f9fc630u}
};

static float unpack(uint32_t word){float value;memcpy(&value,&word,sizeof(value));return value;}
static double area2(const double a[2],const double b[2],const double c[2]){
 return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
}
/* Independent plane/coverage and projective-derivative oracle, not the
 * emulator's Kahan/float32 shader implementation. The margins are large. */
static void analytic_oracle(void){
 double p[3][4],screen[3][2],uv[4][2];unsigned rejected=0,covered=0;
 for(unsigned i=0;i<3;i++){
  for(unsigned j=0;j<4;j++)p[i][j]=unpack(plane_words[i][j]);
  screen[i][0]=(unpack(clip_words[i][0])/(double)unpack(clip_words[i][3])+1)*360;
  screen[i][1]=(unpack(clip_words[i][1])/(double)unpack(clip_words[i][3])+1)*240;
  REQUIRE(p[i][2]>0&&p[i][2]<16777215);
 }
 for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++){
  unsigned at=y*2+x;double point[2]={614.5+x,388.5+y},w[3],sum=0;
  for(unsigned i=0;i<3;i++){w[i]=area2(point,p[(i+1)%3],p[(i+2)%3]);sum+=w[i];}
  double z=p[0][2]+(w[1]/sum)*(p[1][2]-p[0][2])+(w[2]/sum)*(p[2][2]-p[0][2]);
  if(z<0||z>16777215)rejected|=1u<<at;
  sum=0;for(unsigned i=0;i<3;i++){w[i]=area2(point,screen[(i+1)%3],screen[(i+2)%3]);sum+=w[i];}
  if((w[0]>=0&&w[1]>=0&&w[2]>=0)||(w[0]<=0&&w[1]<=0&&w[2]<=0))covered|=1u<<at;
  double denominator=0;uv[at][0]=uv[at][1]=0;
  for(unsigned i=0;i<3;i++){double q=w[i]/sum/unpack(clip_words[i][3]);denominator+=q;for(unsigned j=0;j<2;j++)uv[at][j]+=q*unpack(uv_words[i][j]);}
  for(unsigned j=0;j<2;j++)uv[at][j]/=denominator;
 }
 REQUIRE(rejected==2&&covered==4&&!(rejected&covered));
 for(unsigned x=0;x<2;x++){
  double dx=(uv[x+2][0]-uv[x][0])*256,dy=(uv[x+2][1]-uv[x][1])*256;
  double lod=log2(sqrt(dx*dx+dy*dy))-0x1.7fp+0;
  REQUIRE(lod>8); /* Full-quad derivatives clamp to the last mip on either column. */
 }
 puts("PASS analytic mip oracle: covered mask=4, rejected helper mask=2, both vertical gradients exceed mip8");
}
static MString *corner_vertex(void){
 MString *s=mstring_from_str("#version 410\nout gl_PerVertex{vec4 gl_Position;float gl_PointSize;};\nuniform int fixtureShift;\n");
 pgraph_glsl_get_vtx_header(s,true,true,false,false,false);
 mstring_append(s,"void main(){vtxD0=vec4(1);vtxD1=vec4(0);vtxB0=vec4(0);vtxB1=vec4(0);vtxFog=1.0;vtxT1=vec4(0);vtxT2=vec4(0);vtxT3=vec4(0);gl_PointSize=1.0;\n");
 for(unsigned i=0;i<3;i++){
  mstring_append_fmt(s,"if(gl_VertexID==%u){gl_Position=uintBitsToFloat(uvec4(%uu,%uu,%uu,%uu));vtxT0=uintBitsToFloat(uvec4(%uu,%uu,%uu,%uu));}\n",i,clip_words[i][0],clip_words[i][1],clip_words[i][2],clip_words[i][3],uv_words[i][0],uv_words[i][1],uv_words[i][2],uv_words[i][3]);
 }
 for(unsigned i=0;i<3;i++)mstring_append_fmt(s,"vtxPos%u=uintBitsToFloat(uvec4(%uu,%uu,%uu,%uu));\n",i,plane_words[i][0],plane_words[i][1],plane_words[i][2],plane_words[i][3]);
 mstring_append(s,"triMZ=0.0;gl_Position.x+=float(fixtureShift)*8.0*gl_Position.w;\n}\n");return s;
}
int main(int argc,char **argv){
 unsigned fault=argc==2?(unsigned)atoi(argv[1]):0;REQUIRE(argc<=2&&fault<=4);
 PshState initial={.smooth_shading=true,.surface_zeta_format=2,.depth_clipping=true,.combiner_control=1,.shader_stage_program=1,.other_stage_input=2162688,.final_inputs_0=319554304,.final_inputs_1=7296,.dim_tex={2,0,0,0},.rgb_inputs={134479872,0},.alpha_inputs={8212,0},.rgb_outputs={3072,0},.alpha_outputs={3072,0}};

#ifdef PGRAPH_GLSL_GL_DEFAULT_HELPERS
 if(!fault)source_equivalence();
#endif
 analytic_oracle();
 signal(SIGABRT,quiet_abort);setvbuf(stdout,NULL,_IONBF,0);init_gl();
 GLuint fbo,rb,vao,pipe,tex;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA32F,720,480);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenProgramPipelines(1,&pipe);glBindProgramPipeline(pipe);glViewport(0,0,720,480);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
 glGenTextures(1,&tex);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,fault==3?0:8);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,-0x1.7fp+0f);
 static float pixels[256*256*4];
 for(unsigned level=0;level<9;level++){unsigned side=256>>level;for(unsigned i=0;i<side*side;i++){pixels[i*4]=(level+1)*0.0625f;pixels[i*4+1]=0.5f;pixels[i*4+2]=(9-level)*0.0625f;pixels[i*4+3]=1.0f;}glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA32F,side,side,0,GL_RGBA,GL_FLOAT,pixels);}
 MString *vs=corner_vertex();GLuint vp=program(GL_VERTEX_SHADER,vs);glProgramUniform1i(vp,glGetUniformLocation(vp,"fixtureShift"),fault==1);mstring_unref(vs);glUseProgramStages(pipe,GL_VERTEX_SHADER_BIT,vp);
 MString *ref=mstring_from_str("#version 410\nlayout(location=5) in vec4 vtxT0;\nlayout(location=0) out vec4 fragColor;uniform sampler2D image;\nvoid main(){fragColor=texture(image,vtxT0.xy/max(vtxT0.w,1e-15));}\n");GLuint oracle=program(GL_FRAGMENT_SHADER,ref);mstring_unref(ref);glProgramUniform1i(oracle,glGetUniformLocation(oracle,"image"),0);
 const float expected_mip8[]={0.5625f,0.5f,0.0625f,1.0f},cleared[4]={0};
 unsigned checks=0;float legacy[4]={0};
 for(unsigned masked=0;masked<2;masked++)for(unsigned path=0;path<(masked?3u:4u);path++){
  PshState s=initial;if(path!=0)s.depth_format|=DEPTH_FORMAT_HELPER_LIVENESS;
  if(masked&&fault==2)s.depth_clipping=false;
  MString *text=path==3?NULL:path==2?pgraph_glsl_gen_psh_ready(&s,(GenPshGlslOptions){.locations=true}):pgraph_glsl_gen_psh(&s,(GenPshGlslOptions){.locations=true});
  REQUIRE(path==3||text);GLuint fp=path==3?oracle:program(GL_FRAGMENT_SHADER,text);if(text)mstring_unref(text);
  if(path!=3){
   uniforms(fp,&s,0,path==2);float range[]={0,16777215,0,masked?16000000:16777215};GLint region[32];for(unsigned i=0;i<8;i++){region[i*4]=region[i*4+1]=0;region[i*4+2]=721;region[i*4+3]=481;}
   glProgramUniform4fv(fp,glGetUniformLocation(fp,"clipRange"),1,range);glProgramUniform4iv(fp,glGetUniformLocation(fp,"clipRegion"),8,region);glProgramUniform1f(fp,glGetUniformLocation(fp,"depthFactor"),0);glProgramUniform1f(fp,glGetUniformLocation(fp,"depthOffset"),0);glProgramUniform1ui(fp,glGetUniformLocation(fp,"twoSideSel"),0);
   if(path==2&&fault==4){uint32_t controls[8];REQUIRE(pgraph_glsl_psh_ready_controls(&s,controls));controls[1]=0;glProgramUniform4uiv(fp,glGetUniformLocation(fp,"fsControl[0]"),2,controls);}
  }
  float value[4];glUseProgramStages(pipe,GL_FRAGMENT_SHADER_BIT,fp);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(614,389,1,1,GL_RGBA,GL_FLOAT,value);REQUIRE(glGetError()==GL_NO_ERROR);
  if(!path&&!masked)memcpy(legacy,value,sizeof(legacy));
#ifdef PGRAPH_GLSL_GL_DEFAULT_HELPERS
  _Static_assert(PGRAPH_GLSL_GL_DEFAULT_HELPERS==1,"Advertised GL helper contract must be enabled");
  check_pixel(value,masked?cleared:expected_mip8,path,masked);checks+=4;
#else
  if(path||masked){check_pixel(value,masked?cleared:expected_mip8,path,masked);checks+=4;}

#endif
  if(path!=3)glDeleteProgram(fp);
 }
 printf("PASS helper mip corner: %u exact scalar checks, 256x256 procedural mip levels, far-helper and covered-fragment rejection; ordinary_default_red=%a\n",checks,legacy[0]);
 glDeleteProgram(oracle);glDeleteProgram(vp);glDeleteTextures(1,&tex);glDeleteProgramPipelines(1,&pipe);glDeleteVertexArrays(1,&vao);glDeleteRenderbuffers(1,&rb);glDeleteFramebuffers(1,&fbo);return 0;
}
