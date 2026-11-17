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
/* CONTEXT */

int main(void){
 signal(SIGABRT,quiet_abort);setvbuf(stdout,NULL,_IONBF,0);init_gl();
 GLuint framebuffer,renderbuffer;glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glGenRenderbuffers(1,&renderbuffer);glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,1,1);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,renderbuffer);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 GLuint vao,buffer;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,16,NULL,GL_STREAM_READ);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);glEnable(GL_RASTERIZER_DISCARD);
 const char *conditions[]={/* ACTUAL_RANGE_GUARD */,/* STATIC_RANGE_GUARD */,
  "d > radiusValue",
  "isnan(d) || isnan(radiusValue) || d >= radiusValue",
  "isnan(radiusValue) || d > radiusValue",
  "isnan(d) || d > radiusValue", "false"};
 float samples[]={-INFINITY,-2,-1,-0.0f,0,.5f,1,2,INFINITY,NAN,0x1p-100f,0x1.fffffep127f};
 for(unsigned mode=0;mode<7;mode++){
 char text[2048];snprintf(text,sizeof(text),"#version 410\nuniform float distanceValue;uniform float radiusValue;out vec4 truth;void main(){float d=distanceValue;gl_Position=vec4(0,0,0,1);if(%s)truth=vec4(%d);else truth=vec4(%d);}",conditions[mode],mode==1?1:0,mode==1?0:1);
 const char *s=text;GLuint shader=glCreateShader(GL_VERTEX_SHADER);glShaderSource(shader,1,&s,NULL);glCompileShader(shader);GLint good;glGetShaderiv(shader,GL_COMPILE_STATUS,&good);assert(good);GLuint p=glCreateProgram();glAttachShader(p,shader);const char *varying="truth";glTransformFeedbackVaryings(p,1,&varying,GL_INTERLEAVED_ATTRIBS);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&good);assert(good);glDeleteShader(shader);glUseProgram(p);unsigned differences=0,checked=0;
 for(unsigned a=0;a<12;a++)for(unsigned b=0;b<12;b++){
 float d=samples[a],radius=samples[b];int expected=!isnan(d)&&!isnan(radius)&&(d<radius||d==radius);
 glUniform1f(glGetUniformLocation(p,"distanceValue"),d);glUniform1f(glGetUniformLocation(p,"radiusValue"),radius);glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,1);glEndTransformFeedback();float value[4];glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,16,value);assert(glGetError()==GL_NO_ERROR);
 for(unsigned k=0;k<4;k++){checked++;if(value[k]!=(float)expected){if(differences++<4)fprintf(stderr,"range difference mode=%u d=%a r=%a got=%a expected=%d\n",mode,d,radius,value[k],expected);}}
 }
 printf("RANGE mode=%u checked=%u differences=%u\n",mode,checked,differences);
 assert(checked==576);if(mode<2)assert(!differences);else assert(differences);glDeleteProgram(p);
 }
 glUseProgram(0);glDeleteBuffers(1,&buffer);glDeleteVertexArrays(1,&vao);glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&framebuffer);glDeleteRenderbuffers(1,&renderbuffer);return 0;
}
