/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef _WIN32
#include <windows.h>
#include <epoxy/gl.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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

enum {COUNT=512};
static float coords[COUNT][4],values[COUNT][2];
static int address(int x,int size,int mode) {
 if(mode==0){x%=size;return x<0?x+size:x;}
 if(mode==1)return x<0?0:x>=size?size-1:x;
 if(mode==2){x%=2*size;if(x<0)x+=2*size;return x<size?x:2*size-1-x;}
 return x<0||x>=size?-1:x;
}
static float tap(int x,int y,int w,int h,int u,int v,int axis) {
 x=address(x,w,u);y=address(y,h,v);if(x<0||y<0)return 0;
 int b=(x*(axis?131:73)+y*(axis?73:131)+axis*19)%256;
 return (b>=128?b-256:b)/127.0f;
}
int main(int argc,char **argv) {
 assert(argc==3);
#ifdef _WIN32
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "upload-oracle";
    assert(RegisterClassA(&wc));
    HWND hwnd = CreateWindowA(wc.lpszClassName, "upload", WS_OVERLAPPEDWINDOW,
                             0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32;
    assert(SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd));
    HGLRC context = wglCreateContext(dc);
    assert(context && wglMakeCurrent(dc, context));
puts((const char *)glGetString(GL_RENDERER));
#else
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
    assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
    EGLConfig config;EGLint n;assert(eglChooseConfig(display,attributes,&config,1,&n) && n==1);
    EGLint context_attributes[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,5,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attributes);assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));puts((const char *)glGetString(GL_RENDERER));
#endif

 GLuint shaders[2]={program(argv[1],GL_COMPUTE_SHADER),program(argv[2],GL_COMPUTE_SHADER)};
 GLuint buffers[2],tex;glGenBuffers(2,buffers);glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
 const GLenum modes[]={GL_REPEAT,GL_CLAMP_TO_EDGE,GL_MIRRORED_REPEAT,GL_CLAMP_TO_BORDER};
 const int widths[]={1,2,4,7,64};unsigned checked=0,parent_errors=0;
 for(unsigned shape=0;shape<5;shape++){
 int w=widths[shape],h=widths[4-shape];unsigned char data[64*64*4];
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
 unsigned char *p=data+(y*w+x)*4;p[0]=p[2]=(x*73+y*131)%256;p[1]=p[3]=(x*131+y*73+19)%256;
 }
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
 for(unsigned i=0;i<COUNT;i++){
 int micro=(int)(i%8)-4;if(micro>=0)micro++;
 coords[i][0]=((int)(i/8%8)-2+0.5f+micro/65536.0f)/w;
 coords[i][1]=((int)(i/64%8)-2+0.5f-micro/65536.0f)/h;
 coords[i][2]=0;coords[i][3]=1;
 }
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(coords),coords,GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffers[0]);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[1]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(values),NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,buffers[1]);
 for(int u=0;u<4;u++)for(int v=0;v<4;v++){
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,modes[u]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,modes[v]);
 for(int version=0;version<2;version++){
 glUseProgram(shaders[version]);glDispatchCompute(COUNT/64,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(values),values);
 for(unsigned i=0;i<COUNT;i++){
 float px=coords[i][0]*w-0.5f,py=coords[i][1]*h-0.5f;int x=floorf(px),y=floorf(py);float fx=px-x,fy=py-y;
 for(int axis=0;axis<2;axis++){
 double a=tap(x,y,w,h,u,v,axis),b=tap(x+1,y,w,h,u,v,axis),c=tap(x,y+1,w,h,u,v,axis),d=tap(x+1,y+1,w,h,u,v,axis);
 double want=((1-fx)*a+fx*b)*(1-fy)+((1-fx)*c+fx*d)*fy;
 if(!isfinite(values[i][axis]) || fabs(values[i][axis]-want)>0.00001){
 if(!version){parent_errors++;continue;}
 fprintf(stderr,"shape %dx%d address %d/%d case %u axis %d got %g expected %.9g\n",w,h,u,v,i,axis,values[i][axis],want);abort();
 }
 if(version)checked++;
 }
 }
 }
 }
 }
 assert(glGetError()==GL_NO_ERROR);printf("PASS: %u production signed components against scalar 2D oracle; parent mismatches %u\n",checked,parent_errors);
 glDeleteBuffers(2,buffers);glDeleteTextures(1,&tex);for(int i=0;i<2;i++)glDeleteProgram(shaders[i]);
#ifdef _WIN32
 wglMakeCurrent(NULL,NULL);wglDeleteContext(context);ReleaseDC(hwnd,dc);DestroyWindow(hwnd);
#else
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglTerminate(display);
#endif
}
