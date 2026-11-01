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


enum {COUNT=256};
static float coords[COUNT][4],values[COUNT][4],parent[COUNT][4];
static int address(int x,int n,int mode){if(mode==0){x%=n;return x<0?x+n:x;}if(mode==1)return x<0?0:x>=n?n-1:x;if(mode==2){x%=2*n;if(x<0)x+=2*n;return x<n?x:2*n-1-x;}return x<0||x>=n?-1:x;}
static double tap(int x,int y,int w,int h,int u,int v,int c){x=address(x,w,u);y=address(y,h,v);return x<0||y<0?0:((x*73+y*131+c*47)%256)/255.0;}
static double sample(double ucoord,double vcoord,int w,int h,int u,int v,int c,bool linear){
 if(u==1||u==3)ucoord=fmax(-2,fmin(3,ucoord));
 if(v==1||v==3)vcoord=fmax(-2,fmin(3,vcoord));
 double px=ucoord*w-(linear?0.5:0),py=vcoord*h-(linear?0.5:0);int x=floor(px),y=floor(py);if(!linear)return tap(x,y,w,h,u,v,c);
 double fx=px-x,fy=py-y;return ((1-fx)*tap(x,y,w,h,u,v,c)+fx*tap(x+1,y,w,h,u,v,c))*(1-fy)+((1-fx)*tap(x,y+1,w,h,u,v,c)+fx*tap(x+1,y+1,w,h,u,v,c))*fy;
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


 GLuint shaders[2][2];char path[4096];for(int version=0;version<2;version++)for(int rect=0;rect<2;rect++){int n=snprintf(path,sizeof(path),"%s/%s-%d.comp",argv[1],version?"candidate":"parent",rect);assert(n>0&&(size_t)n<sizeof(path));shaders[version][rect]=program(path,GL_COMPUTE_SHADER);}
 GLuint buffers[2],tex;glGenBuffers(2,buffers);glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
 const GLenum modes[]={GL_REPEAT,GL_CLAMP_TO_EDGE,GL_MIRRORED_REPEAT,GL_CLAMP_TO_BORDER};const int widths[]={4,7,32,64},heights[]={3,5,16,1};const float qs[]={0.25f,0.5f,1,2,4,-1};unsigned checked=0,errors=0,unit=0;
 for(int shape=0;shape<4;shape++){
 int w=widths[shape],h=heights[shape];unsigned char data[64*16*4];for(int y=0;y<h;y++)for(int x=0;x<w;x++)for(int c=0;c<4;c++)data[(y*w+x)*4+c]=(x*73+y*131+c*47)%256;
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
 for(int rect=0;rect<2;rect++)for(int linear=0;linear<2;linear++){
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,linear?GL_LINEAR:GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,linear?GL_LINEAR:GL_NEAREST);
 for(int phase=0;phase<6;phase++){
 float q=qs[phase];for(unsigned i=0;i<COUNT;i++){
 float u=((int)(i%16)-4+0.375f)/w,v=((int)(i/16)-4+0.625f)/h;
 coords[i][0]=u*q*(rect?w:1);coords[i][1]=v*q*(rect?h:1);coords[i][2]=0;coords[i][3]=q;
 }
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[0]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(coords),coords,GL_STATIC_DRAW);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,buffers[0]);
 glBindBuffer(GL_SHADER_STORAGE_BUFFER,buffers[1]);glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(values),NULL,GL_DYNAMIC_READ);glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,buffers[1]);
 for(int u=0;u<4;u++)for(int v=0;v<4;v++){
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,modes[u]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,modes[v]);
 for(int version=0;version<2;version++){
 glUseProgram(shaders[version][rect]);glDispatchCompute(COUNT/64,1,1);glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(values),values);
 if(!version)memcpy(parent,values,sizeof(values));
 for(unsigned i=0;i<COUNT;i++)for(int c=0;c<4;c++){
 double divisor=q;
 double cu=coords[i][0]/(rect?w:1)/divisor,cv=coords[i][1]/(rect?h:1)/divisor,want=0;
 const int weights[]={1,2,1};for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)want+=sample(cu+(double)x/w,cv+(double)y/h,w,h,u,v,c,linear)*weights[x+1]*weights[y+1]/16.0;
 double tolerance=linear?1.0/255:0.000001;
 if(!isfinite(values[i][c])||fabs(values[i][c]-want)>tolerance){if(!version){errors++;continue;}fprintf(stderr,"shape %dx%d rect %d linear %d Q %g address %d/%d case %u c %d got %g expected %.9g\n",w,h,rect,linear,q,u,v,i,c,values[i][c],want);abort();}
 if(version){checked++;if(q==1){assert(fabs(values[i][c]-parent[i][c])<0.000001);unit++;}}
 }
 }
 }
 }
 }
 }
 assert(glGetError()==GL_NO_ERROR);assert(checked==1572864&&unit==262144);printf("PASS: %u production convolution components; %u Q=1 parent controls; %u parent scalar mismatches\n",checked,unit,errors);
 glDeleteBuffers(2,buffers);glDeleteTextures(1,&tex);for(int i=0;i<2;i++)for(int j=0;j<2;j++)glDeleteProgram(shaders[i][j]);
#ifdef _WIN32
 wglMakeCurrent(NULL,NULL);wglDeleteContext(context);ReleaseDC(hwnd,dc);DestroyWindow(hwnd);
#else
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglTerminate(display);
#endif
}
