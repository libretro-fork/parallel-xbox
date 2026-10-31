/* SPDX-License-Identifier: GPL-2.0-or-later */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
enum {COUNT=4096};
typedef struct {float p[3][4],point[4];} Case;
static Case cases[COUNT];
static float outputs[2][COUNT][4];
static uint32_t rng=0x12f3a;
static unsigned next(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static GLuint program(const char *path,GLenum type) {
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);rewind(f);
    char *text=malloc(size+1);assert(text);assert(fread(text,1,size,f)==(size_t)size);fclose(f);text[size]=0;
    GLuint shader=glCreateShader(type);const char *source=text;glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok) {char log[4096];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}assert(ok);
    if(type==GL_FRAGMENT_SHADER) {free(text);return shader;}
    GLuint p=glCreateProgram();glAttachShader(p,shader);glLinkProgram(p);glGetProgramiv(p,GL_LINK_STATUS,&ok);assert(ok);
    glDeleteShader(shader);free(text);return p;
}
static double area(const float *point,double scale,const float *b,const float *c) {
    double x=point[0]/scale,y=point[1]/scale;
    return ((double)b[0]-x)*((double)c[1]-y)-((double)b[1]-y)*((double)c[0]-x);
}
int main(int argc,char **argv) {
    assert(argc==5);
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
    assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
    EGLConfig config;EGLint n;assert(eglChooseConfig(display,attributes,&config,1,&n) && n==1);
    EGLint context_attributes[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attributes);assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));puts((const char *)glGetString(GL_RENDERER));
    glDeleteShader(program(argv[3],GL_FRAGMENT_SHADER));glDeleteShader(program(argv[4],GL_FRAGMENT_SHADER));
    const int exponents[]={-10,0,10,30,55,60,61,62,63,64,65,80,100,120,125,126,127};
    for(unsigned i=0;i<COUNT;i++) {
        int exponent=exponents[i%17];
        float s=ldexpf(1,exponent),tx=((int)(next()%5)-2)*0.25f*s,ty=((int)(next()%5)-2)*0.25f*s;
        Case *c=&cases[i];float vertices[3][2]={{-s+tx,-s+ty},{s+tx,-s+ty},{tx,s+ty}};
        float b1=(next()%4+1)/16.0f,b2=(next()%4+1)/8.0f,b0=1-b1-b2;
        for(unsigned v=0;v<3;v++) {c->p[v][0]=vertices[v][0];c->p[v][1]=vertices[v][1];c->p[v][2]=(v+1)*0.25f;c->p[v][3]=ldexpf(1,(int)(next()%4)-1);}
        c->point[2]=exponent>125?1:(float)(1u<<(i%3));c->point[3]=i&1;
        c->point[0]=(b0*c->p[0][0]+b1*c->p[1][0]+b2*c->p[2][0])*c->point[2];
        c->point[1]=(b0*c->p[0][1]+b1*c->p[1][1]+b2*c->p[2][1])*c->point[2];
        if(exponent>=55 && i%4==0) {c->point[0]=640.5f;c->point[1]=480.5f;}
        /* Identical vertices exercise the existing degenerate fallback. */
        if(i>=COUNT-8) for(unsigned v=1;v<3;v++) memcpy(c->p[v],c->p[0],sizeof(c->p[0]));
    }
    GLuint buffers[2];glGenBuffers(2,buffers);glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(cases),cases,GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffers[0]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[1]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(outputs[0]),NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,buffers[1]);
    for(unsigned p=0;p<2;p++) {
        GLuint prog=program(argv[p+1],GL_COMPUTE_SHADER);glUseProgram(prog);glDispatchCompute(COUNT/64,1,1);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(outputs[p]),outputs[p]);glDeleteProgram(prog);
    }
    unsigned repaired=0,unchanged=0;
    for(unsigned i=0;i<COUNT;i++) {
        Case *c=&cases[i];double weights[3];
        for(unsigned v=0;v<3;v++) {weights[v]=area(c->point,c->point[2],c->p[(v+1)%3],c->p[(v+2)%3]);if(c->point[3]) weights[v]/=c->p[v][3];}
        double sum=weights[0]+weights[1]+weights[2],b1=sum?weights[1]/sum:0,b2=sum?weights[2]/sum:0;
        unsigned z=c->point[3]?3:2;double expected=c->p[0][z]+b1*(c->p[1][z]-c->p[0][z])+b2*(c->p[2][z]-c->p[0][z]);
        assert(isfinite(outputs[0][i][0]) && isfinite(outputs[0][i][1]) && isfinite(outputs[0][i][2]));
        if(fabs(outputs[0][i][0]-b1)>1e-5 || fabs(outputs[0][i][1]-b2)>1e-5 || fabs(outputs[0][i][2]-expected)>1e-5) {
            fprintf(stderr,"case %u: weights %g,%g wanted %g,%g depth %g wanted %g\n",i,outputs[0][i][0],outputs[0][i][1],b1,b2,outputs[0][i][2],expected);abort();
        }
        if(!outputs[1][i][3]) {
            if(memcmp(outputs[0][i],outputs[1][i],3*sizeof(float))) {
                fprintf(stderr,"ordinary case %u changed: %a %a %a -> %a %a %a\n",i,outputs[1][i][0],outputs[1][i][1],outputs[1][i][2],outputs[0][i][0],outputs[0][i][1],outputs[0][i][2]);abort();
            }
            unchanged++;
        } else repaired++;
    }
    assert(repaired>1000 && unchanged>1000);assert(glGetError()==GL_NO_ERROR);
    printf("barycentric depth: %u cases; %u overflowing results repaired, %u original results bit-identical; perspective/linear, scale 1/2/4 and degenerates passed\n",COUNT,repaired,unchanged);
    glDeleteBuffers(2,buffers);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglTerminate(display);
}
