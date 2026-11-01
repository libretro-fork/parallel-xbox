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


enum { COUNT=512, LEVELS=7 };
static float coords[COUNT][4],values[COUNT][2];
static int dimension(int base,int level) { int n=base>>level;return n?n:1; }
static int address(int x,int size,int mode) {
 if(mode==1){x%=size;return x<0?x+size:x;}
 if(mode==3)return x<0?0:x>=size?size-1:x;
 x%=2*size;if(x<0)x+=2*size;return x<size?x:2*size-1-x;
}
static unsigned byte_value(int x,int y,int level,int axis) {
 return (x*(axis?131:73)+y*(axis?73:131)+axis*19+level*(axis?79:43))%256;
}
static double tap(int x,int y,int level,int u,int v,int axis) {
 x=address(x,dimension(64,level),u);y=address(y,dimension(32,level),v);
 int b=byte_value(x,y,level,axis);return (b>=128?b-256:b)/127.0;
}
static double sample(float ucoord,float vcoord,int level,int u,int v,int axis,bool linear) {
 float px=ucoord*dimension(64,level)-(linear?0.5f:0),py=vcoord*dimension(32,level)-(linear?0.5f:0);
 int x=floorf(px),y=floorf(py);if(!linear)return tap(x,y,level,u,v,axis);
 double fx=px-x,fy=py-y;
 return ((1-fx)*tap(x,y,level,u,v,axis)+fx*tap(x+1,y,level,u,v,axis))*(1-fy)+
        ((1-fx)*tap(x,y+1,level,u,v,axis)+fx*tap(x+1,y+1,level,u,v,axis))*fy;
}
int main(int argc,char **argv) {
 assert(argc==2);
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


 GLuint buffers[2],tex;glGenBuffers(2,buffers);glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_LINEAR);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,LEVELS-1);
 for(int level=0;level<LEVELS;level++) {
  int w=dimension(64,level),h=dimension(32,level);unsigned char data[64*32*4];
  for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
   unsigned char *p=data+(y*w+x)*4;p[0]=p[2]=byte_value(x,y,level,0);p[1]=p[3]=byte_value(x,y,level,1);
  }
  glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
 }
 char path[4096],stem[128];int path_length=snprintf(path,sizeof(path),"%s/manifest.txt",argv[1]);assert(path_length>0&&(size_t)path_length<sizeof(path));
 FILE *manifest=fopen(path,"r");assert(manifest);unsigned checked=0,shaders=0;
 unsigned api,minimum,filter,mixed,u,v;
 while(fscanf(manifest,"%127s %u %u %u %u %u %u",stem,&api,&minimum,&filter,&mixed,&u,&v)==7) {
  path_length=snprintf(path,sizeof(path),"%s/%s.comp",argv[1],stem);assert(path_length>0&&(size_t)path_length<sizeof(path));
  GLuint prog=program(path,GL_COMPUTE_SHADER);glUseProgram(prog);
  GLint query=glGetUniformLocation(prog,"probeQuery");assert(query>=0);
  const float footprints[]={-1,0,0.25f,1,1.25f,4.25f,5.75f,6};
  for(unsigned phase=0;phase<sizeof(footprints)/sizeof(footprints[0]);phase++) {
   float footprint=footprints[phase],q=fmaxf(0,footprint);
   bool trilinear=filter>=5,min_linear=filter==4||filter==6;
   if(!trilinear)q=floorf(q+0.5f);
   float lod=fmaxf(minimum,q);int lo=floorf(lod),hi=trilinear?ceilf(lod):lo;
   float weight=trilinear?lod-lo:0;
   bool linear=mixed?(footprint>0?min_linear:!min_linear):min_linear;
   for(unsigned i=0;i<COUNT;i++) {
    int micro=(int)(i%8)-4;if(micro>=0)micro++;
    coords[i][0]=((int)(i/8%8)-2+0.5f+micro/65536.0f)/dimension(64,lo);
    coords[i][1]=((int)(i/64%8)-2+0.5f-micro/65536.0f)/dimension(32,lo);
    coords[i][2]=0;coords[i][3]=1;
   }
   glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(coords),coords,GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,buffers[0]);
   glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[1]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(values),NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,buffers[1]);
   glUniform2f(query,q,footprint);glDispatchCompute(COUNT/64,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(values),values);
   for(unsigned i=0;i<COUNT;i++)for(int axis=0;axis<2;axis++) {
    double a=sample(coords[i][0],coords[i][1],lo,u,v,axis,linear),b=sample(coords[i][0],coords[i][1],hi,u,v,axis,linear);
    double want=a*(1-weight)+b*weight;
    if(!isfinite(values[i][axis])||fabs(values[i][axis]-want)>0.00001) {
     fprintf(stderr,"%s phase %u case %u axis %d got %g expected %.9g\n",stem,phase,i,axis,values[i][axis],want);abort();
    }
    checked++;
   }
  }
  assert(glGetError()==GL_NO_ERROR);glDeleteProgram(prog);shaders++;
 }
 assert(feof(manifest));fclose(manifest);assert(shaders==162&&checked==1327104);
 printf("PASS: %u production mip shader variants, %u signed components; GL/Vulkan generation, controlled LOD, address pairs, lower clamps and mip tails\n",shaders,checked);
 glDeleteBuffers(2,buffers);glDeleteTextures(1,&tex);
#ifdef _WIN32
 wglMakeCurrent(NULL,NULL);wglDeleteContext(context);ReleaseDC(hwnd,dc);DestroyWindow(hwnd);
#else
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglTerminate(display);
#endif
}
