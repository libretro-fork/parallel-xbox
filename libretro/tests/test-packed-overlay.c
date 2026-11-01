/* SPDX-License-Identifier: GPL-2.0-or-later */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* PRODUCTION */
static uint8_t clip(int v){return v<0?0:v>255?255:v;}
/* Independent reference retained from the old CPU-converted texture path. */
static void old_rgba(uint8_t *dst,const uint8_t *src,unsigned w,unsigned h,unsigned pitch)
{
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        const uint8_t *p=src+y*pitch+(x/2)*4;
        int c=p[(x&1)*2]-16,d=p[1]-128,e=p[3]-128;
        uint8_t *out=dst+4*(y*w+x);
        out[0]=clip((298*c+409*e+128)>>8);out[1]=clip((298*c-100*d-208*e+128)>>8);
        out[2]=clip((298*c+516*d+128)>>8);out[3]=255;
    }
}

static unsigned cases; static size_t checked;
static void run_case(GLuint program,unsigned w,unsigned h,unsigned pad,unsigned seed)
{
 size_t row=(w+1)/2*4,pitch=row+pad,bytes=(size_t)w*h*4;
 size_t out_bytes=(size_t)(w+1)*(h+1)*4;
 uint8_t *input=malloc(pitch*h),*packed=malloc(row*h+32),*rgba=malloc(bytes),*actual=malloc(out_bytes);
 assert(input&&packed&&rgba&&actual);memset(packed,0xA5,row*h+32);memset(actual,0xAB,out_bytes);
 uint32_t random=seed;
 for(size_t i=0;i<pitch*h;i++){random=random*1664525u+1013904223u;input[i]=random>>24;}
 if(row>=4){input[0]=0;input[1]=255;input[2]=255;input[3]=0;}
 pvideo_pack_rows(packed+16,input,w,h,pitch);
 for(unsigned y=0;y<h;y++)assert(!memcmp(packed+16+y*row,input+y*pitch,row));
 for(unsigned i=0;i<16;i++){assert(packed[i]==0xA5);assert(packed[16+row*h+i]==0xA5);}
 old_rgba(rgba,input,w,h,pitch);
 GLuint buffers[2],texture;glGenBuffers(2,buffers);glGenTextures(1,&texture);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);glBufferData(GL_SHADER_STORAGE_BUFFER,row*h+32,packed,GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffers[0]);
 glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w+1,h+1,0,GL_RGBA,GL_UNSIGNED_BYTE,actual);
 glBindImageTexture(1,texture,0,GL_FALSE,0,GL_WRITE_ONLY,GL_RGBA8);
 uint32_t push[]={4,0,w,h};glBindBuffer(GL_UNIFORM_BUFFER,buffers[1]);glBufferData(GL_UNIFORM_BUFFER,sizeof(push),push,GL_STATIC_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,2,buffers[1]);
 glUseProgram(program);glDispatchCompute((((size_t)(w+1)/2)*h+63)/64,1,1);glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
 glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,actual);
 for(unsigned y=0;y<=h;y++)for(unsigned x=0;x<=w;x++)for(unsigned c=0;c<4;c++){
  uint8_t want=x<w&&y<h?rgba[(y*w+x)*4+c]:0xAB;
  if(actual[(y*(w+1)+x)*4+c]!=want){fprintf(stderr,"case %u pixel %u,%u channel %u differs\n",cases,x,y,c);abort();}
 }
 assert(glGetError()==GL_NO_ERROR);glDeleteBuffers(2,buffers);glDeleteTextures(1,&texture);
 checked+=(size_t)w*h;cases++;free(input);free(packed);free(rgba);free(actual);
}

int main(void)
{
 EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE},n;
 EGLConfig config;assert(eglChooseConfig(display,attrs,&config,1,&n)&&n);
 EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,NULL);assert(context!=EGL_NO_CONTEXT);assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
 GLuint shader=glCreateShader(GL_COMPUTE_SHADER);const char *source=pvideo_convert_glsl;
 glShaderSource(shader,1,&source,NULL);glCompileShader(shader);GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
 if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"%s\n",log);abort();}
 GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glGetProgramiv(program,GL_LINK_STATUS,&ok);assert(ok);glDeleteShader(shader);
 unsigned widths[]={1,2,3,7,16,63,64,65,640,641,1920};
 for(unsigned w=0;w<sizeof(widths)/sizeof(*widths);w++)for(unsigned pad=0;pad<=32;pad+=8)for(unsigned seed=1;seed<=4;seed++)run_case(program,widths[w],13,pad,seed);
 run_case(program,1920,1080,32,0xABCD);run_case(program,641,479,8,0x1234);
 printf("%u packed-row/GPU conversion cases, %zu RGBA pixels byte-exact against old CPU path; offsets, padding, odd widths and tail guards PASS\n",cases,checked);
 glDeleteProgram(program);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglTerminate(display);return 0;
}
