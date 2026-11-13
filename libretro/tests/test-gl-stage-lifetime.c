/* SPDX-License-Identifier: GPL-2.0-or-later */
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
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef struct Lru {unsigned marker;} Lru;
typedef struct LruNode {unsigned marker;} LruNode;
typedef struct ShaderModuleCacheEntry {LruNode node;GLuint gl_shader,gl_stage_program;bool locs_pending;} ShaderModuleCacheEntry;
typedef struct PGRAPHGLState {unsigned uni_cache;Lru shader_module_cache;} PGRAPHGLState;
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
static PGRAPHGLState renderer;
static GLuint forgotten;
static void uniform_cache_forget(unsigned *cache,uintptr_t key){assert(cache==&renderer.uni_cache&&key);(*cache)++;forgotten=key;}
/* PRODUCTION */
static GLuint stage(GLenum type,const char *source){GLuint p=glCreateShaderProgramv(type,1,&source);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char log[2048];glGetProgramInfoLog(p,sizeof(log),NULL,log);fprintf(stderr,"stage %x failed: %s\n",type,log);}assert(ok);return p;}
static void pixel(GLuint pipeline,GLuint fragment,const float *color){
 glUseProgram(0);glBindProgramPipeline(pipeline);glProgramUniform4fv(fragment,glGetUniformLocation(fragment,"color"),1,color);
 glDrawArrays(GL_TRIANGLES,0,3);unsigned char value[4];glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,value);
 for(unsigned i=0;i<4;i++){int expected=(int)(color[i]*255.0f+0.5f);assert(value[i]==expected);}
 assert(glGetError()==GL_NO_ERROR);
}
int main(void){
#ifdef _WIN32
 libretro_gl_set_standalone_mode();
#else
 EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(d!=EGL_NO_DISPLAY);assert(eglInitialize(d,NULL,NULL)&&eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};EGLConfig cfg;EGLint n;assert(eglChooseConfig(d,attrs,&cfg,1,&n)&&n==1);
 EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
#endif
 const char *vs="#version 430\nout gl_PerVertex { vec4 gl_Position; };void main(){vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexID],0,1);}";
 const char *fs="#version 430\nuniform vec4 color;layout(location=0)out vec4 outputColor;void main(){outputColor=color;}";
 unsigned cases=0;
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++){
#ifdef _WIN32
  GloContext *c=glo_context_create();assert(c);glo_set_current(c);assert(xemu_gl_version()>=43);
#else
  EGLContext c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ca);assert(c!=EGL_NO_CONTEXT);assert(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,c));
#endif
  if(!lifecycle)printf("GL stage lifetime device=%s version=%s\n",(const char*)glGetString(GL_RENDERER),(const char*)glGetString(GL_VERSION));
  GLuint fbo,tex,vao;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,1,1);glGenVertexArrays(1,&vao);glBindVertexArray(vao);
  for(unsigned trial=0;trial<32;trial++){
   GLuint vertex=stage(GL_VERTEX_SHADER,vs),fragment=stage(GL_FRAGMENT_SHADER,fs),pipes[2];
   glGenProgramPipelines(2,pipes);for(unsigned i=0;i<2;i++){glBindProgramPipeline(pipes[i]);glUseProgramStages(pipes[i],GL_VERTEX_SHADER_BIT,vertex);glUseProgramStages(pipes[i],GL_FRAGMENT_SHADER_BIT,fragment);}
   const float before[4]={0,1,0,1},after[4]={1,0,1,1};pixel(pipes[0],fragment,before);
   ShaderModuleCacheEntry module={.gl_stage_program=fragment};unsigned prior=renderer.uni_cache;
   shader_module_cache_entry_post_evict(&renderer.shader_module_cache,&module.node);
   assert(!module.gl_stage_program&&!module.locs_pending&&renderer.uni_cache==prior+1&&forgotten==fragment);
   GLint deleted;glGetProgramiv(fragment,GL_DELETE_STATUS,&deleted);assert(deleted&&glIsProgram(fragment));
   pixel(pipes[1],fragment,after);pixel(pipes[0],fragment,before);
   glDeleteProgramPipelines(1,&pipes[0]);assert(glIsProgram(fragment));pixel(pipes[1],fragment,after);
   glUseProgramStages(pipes[1],GL_FRAGMENT_SHADER_BIT,0);assert(!glIsProgram(fragment));
   glDeleteProgramPipelines(1,&pipes[1]);glDeleteProgram(vertex);
   /* Unpublished separable link: no status/completion query in eviction. */
   GLuint shader=glCreateShader(GL_FRAGMENT_SHADER);glShaderSource(shader,1,&fs,NULL);glCompileShader(shader);
   GLuint pending=glCreateProgram();glProgramParameteri(pending,GL_PROGRAM_SEPARABLE,GL_TRUE);glAttachShader(pending,shader);glLinkProgram(pending);
   module=(ShaderModuleCacheEntry){.gl_stage_program=pending,.gl_shader=shader,.locs_pending=true};prior=renderer.uni_cache;
   shader_module_cache_entry_post_evict(&renderer.shader_module_cache,&module.node);
   assert(!module.gl_stage_program&&!module.gl_shader&&!module.locs_pending&&renderer.uni_cache==prior+1&&forgotten==pending);
   assert(!glIsProgram(pending)&&!glIsShader(shader));
   GLuint raw=glCreateShader(GL_FRAGMENT_SHADER);module=(ShaderModuleCacheEntry){.gl_shader=raw};prior=renderer.uni_cache;
   shader_module_cache_entry_post_evict(&renderer.shader_module_cache,&module.node);assert(!glIsShader(raw)&&renderer.uni_cache==prior);
   assert(glGetError()==GL_NO_ERROR);cases++;
  }
  glDeleteVertexArrays(1,&vao);glDeleteTextures(1,&tex);glDeleteFramebuffers(1,&fbo);
#ifdef _WIN32
  glo_context_destroy(c);
#else
  assert(eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT));assert(eglDestroyContext(d,c));
#endif
 }
#ifndef _WIN32
 eglTerminate(d);
#endif
 printf("GL stage lifetime: %u two-pipeline retained renders, final release, pending links, shader-only eviction, uniform memo invalidation and 3 context lifecycles PASS\n",cases);
}
