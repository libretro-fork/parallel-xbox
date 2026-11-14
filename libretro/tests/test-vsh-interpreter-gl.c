/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vulkan/vulkan.h>
#include <spirv/unified1/spirv.h>
#include "hw/xbox/nv2a/pgraph/spv/vsh.h"
static unsigned constant_checks;
/* TOKEN_HELPERS */
/* KEY_HELPERS */
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

static GLuint compile_program(MString *text)
{
    GLuint shader=glCreateShader(GL_VERTEX_SHADER),program=glCreateProgram();
    const char *source=mstring_get_str(text);
    glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint okay;glGetShaderiv(shader,GL_COMPILE_STATUS,&okay);
    if(!okay){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"GL compile: %s\n%s\n",log,source);abort();}
    glAttachShader(program,shader);
    const char *varyings[]={"vtxD0","vtxD1","vtxB0","vtxB1","vtxFog","gl_SkipComponents3","vtxT0","vtxT1","vtxT2","vtxT3","vtxPos0","vtxPos1","vtxPos2","triMZ","gl_SkipComponents3","gl_Position","gl_PointSize","gl_SkipComponents3"};
    glTransformFeedbackVaryings(program,ARRAY_SIZE(varyings),varyings,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&okay);
    if(!okay){char log[8192];glGetProgramInfoLog(program,sizeof(log),NULL,log);fprintf(stderr,"GL link: %s\n",log);abort();}
    glDeleteShader(shader);
    return program;
}

static void upload_uniforms(GLuint program,const VshUniformValues *values)
{
    for(unsigned i=0;i<VshUniform__COUNT;i++){
        const UniformInfo *info=&VshUniformInfo[i];
        GLint loc=glGetUniformLocation(program,info->name);
        if(loc<0)continue;
        const void *data=(const uint8_t *)values+info->val_offs;
        switch(info->type){
        case UniformElementType_float:glUniform1fv(loc,info->count,data);break;
        case UniformElementType_uint:glUniform1uiv(loc,info->count,data);break;
        case UniformElementType_int:glUniform1iv(loc,info->count,data);break;
        case UniformElementType_vec2:glUniform2fv(loc,info->count,data);break;
        case UniformElementType_vec3:glUniform3fv(loc,info->count,data);break;
        case UniformElementType_vec4:glUniform4fv(loc,info->count,data);break;
        case UniformElementType_ivec2:glUniform2iv(loc,info->count,data);break;
        case UniformElementType_ivec4:glUniform4iv(loc,info->count,data);break;
        default:assert(false);
        }
    }
}

static struct {MString *source;GLuint program;} ready[128];
static unsigned ready_count,ready_reuses;
#ifdef _WIN32
static GloContext *context;
#else
static EGLDisplay display;
static EGLContext context;
#endif

static void init_gl(void)
{
#ifdef _WIN32
    libretro_gl_set_standalone_mode();
    context=glo_context_create_4_1();assert(context);glo_set_current(context);
#else
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(display!=EGL_NO_DISPLAY && eglInitialize(display,NULL,NULL));
    assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
    EGLConfig config;EGLint count;
    assert(eglChooseConfig(display,attrs,&config,1,&count) && count==1);
    EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,1,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,ca);
    assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
#endif
    printf("Actual GL device=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
}

int main(int argc,char **argv){
 assert(argc==3 || argc==5);unsigned first=strtoul(argv[1],NULL,10),limit=strtoul(argv[2],NULL,10);
 assert(first<limit && limit<=42512);printf("Actual GL fixture [%u,%u)\n",first,limit);
 init_gl();
 GLuint framebuffer,renderbuffer;glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
 glGenRenderbuffers(1,&renderbuffer);glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,1,1);
 glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,renderbuffer);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 glViewport(0,0,1,1);
 GLuint vao,buffers[3];glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(3,buffers);
 enum{VERTICES=32};
 uint8_t input_data[VERTICES*256],output_data[VERTICES*240];
 struct{uint8_t *mapped;}input={input_data},output={output_data};
 glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glBufferData(GL_ARRAY_BUFFER,sizeof(input_data),NULL,GL_STREAM_DRAW);
 glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffers[1]);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,sizeof(output_data),NULL,GL_STREAM_READ);
 glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffers[1]);
 glBindBuffer(GL_UNIFORM_BUFFER,buffers[2]);glBufferData(GL_UNIFORM_BUFFER,136*16,NULL,GL_STREAM_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,0,buffers[2]);
 glEnable(GL_RASTERIZER_DISCARD);
 VkPhysicalDeviceProperties props={0};props.limits.maxPushConstantsSize=256;
 uint32_t reference[VERTICES*60];unsigned compared=0,failures=0;
/* CORPUS */

  GenVshGlslOptions glopts={.locations=true};
  MString *text[2]={pgraph_glsl_gen_vsh(&state,glopts),pgraph_glsl_gen_vsh_interpreter(&state,glopts)};
  /* Source must not depend on the uploaded program. Change only one token
   * and its length, then restore the final recipe before any consumer. */
  uint32_t saved_token[4];memcpy(saved_token,state.programmable.program_data[0],sizeof(saved_token));
  int saved_length=state.programmable.program_length;
  token(state.programmable.program_data[0],MAC_MOV,ILU_NOP,15,0,false,true);
  state.programmable.program_length=1;
  MString *interpreted=text[1],*different=pgraph_glsl_gen_vsh_interpreter(&state,glopts);
  assert(!strcmp(mstring_get_str(interpreted),mstring_get_str(different)));
  mstring_unref(different);
  memcpy(state.programmable.program_data[0],saved_token,sizeof(saved_token));state.programmable.program_length=saved_length;
  unsigned ri=0;for(;ri<ready_count;ri++)if(!strcmp(mstring_get_str(ready[ri].source),mstring_get_str(text[1])))break;
  if(ri==ready_count){assert(ready_count<128);ready[ri].source=text[1];ready[ri].program=compile_program(text[1]);ready_count++;}
  else{mstring_unref(text[1]);ready_reuses++;}
  GLuint program[2]={compile_program(text[0]),ready[ri].program};mstring_unref(text[0]);
  GLuint block=glGetUniformBlockIndex(program[1],"XvsCode");assert(block!=GL_INVALID_INDEX);glUniformBlockBinding(program[1],block,0);
  GLint code_size=0;glGetActiveUniformBlockiv(program[1],block,GL_UNIFORM_BLOCK_DATA_SIZE,&code_size);assert(code_size==136*16);
/* UNIFORM_VALUES */

  memcpy(values.vpCode,state.programmable.program_data,sizeof(values.vpCode));
  if(state.programmable.program_length<136){uint32_t poison[4];token(poison,MAC_MOV,ILU_NOP,15,9,false,true);memcpy(values.vpCode[135],poison,16);}
  glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(input_data),input_data);
  for(unsigned a=0;a<16;a++){
   if(state.compressed_attrs&(1u<<a))glVertexAttribIPointer(a,1,GL_INT,256,(void*)(uintptr_t)(a*16));
   else glVertexAttribPointer(a,4,GL_FLOAT,GL_FALSE,256,(void*)(uintptr_t)(a*16));
   glEnableVertexAttribArray(a);
  }
  glBindBuffer(GL_UNIFORM_BUFFER,buffers[2]);glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(values.vpCode),values.vpCode);
  for(unsigned path=0;path<2;path++){
   assert(prepared_hash==fixture_key_hash(&state,&opts));
   glUseProgram(program[path]);upload_uniforms(program[path],&values);
   glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,VERTICES);glEndTransformFeedback();
   GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
   GLenum result=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,10000000000ull);
   assert(result==GL_ALREADY_SIGNALED || result==GL_CONDITION_SATISFIED);glDeleteSync(fence);
   glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffers[1]);glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,sizeof(output_data),output_data);
   GLenum error=glGetError();if(error){fprintf(stderr,"GL error %x case%u path%u\n",error,test,path);abort();}
/* COMPARE */

  }
  glDeleteProgram(program[0]);
 }
 glUseProgram(0);for(unsigned i=0;i<ready_count;i++){glDeleteProgram(ready[i].program);mstring_unref(ready[i].source);}
 glDeleteBuffers(3,buffers);glDeleteVertexArrays(1,&vao);
 glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&framebuffer);glDeleteRenderbuffers(1,&renderbuffer);
 assert(glGetError()==GL_NO_ERROR);
#ifdef _WIN32
 glo_context_destroy(context);
#else
 assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));
 assert(eglDestroyContext(display,context));assert(eglTerminate(display));
#endif
 printf("Actual GL interpreter: %u scalar results, %u differences; %u ready programs, %u reuses; relative constants %u\n",compared,failures,ready_count,ready_reuses,constant_checks);
 return failures?1:0;
}
