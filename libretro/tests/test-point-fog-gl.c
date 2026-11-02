/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef _WIN32
#include <windows.h>
#include <epoxy/gl.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static GLuint compile(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}assert(ok);return shader;
}
int main(int argc,char **argv)
{
    assert(argc>=3 && argc%2==1);
#ifdef _WIN32
    WNDCLASSA wc={0};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(NULL);wc.lpszClassName="gamma-oracle";assert(RegisterClassA(&wc));
    HWND window=CreateWindowA(wc.lpszClassName,"gamma",WS_OVERLAPPEDWINDOW,0,0,64,64,NULL,NULL,wc.hInstance,NULL);
    HDC dc=GetDC(window);PIXELFORMATDESCRIPTOR pfd={0};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;
    assert(SetPixelFormat(dc,ChoosePixelFormat(dc,&pfd),&pfd));HGLRC context=wglCreateContext(dc);assert(context&&wglMakeCurrent(dc,context));
#else
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE},n;EGLConfig config;assert(eglChooseConfig(display,attrs,&config,1,&n)&&n);
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,NULL);EGLint dims[]={EGL_WIDTH,48,EGL_HEIGHT,48,EGL_NONE};EGLSurface surface=eglCreatePbufferSurface(display,config,dims);assert(context!=EGL_NO_CONTEXT&&surface!=EGL_NO_SURFACE&&eglMakeCurrent(display,surface,surface,context));
#endif
    printf("Point/fog GL GPU: %s\n",glGetString(GL_RENDERER));
    GLuint vao;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    GLuint buffer;glGenBuffers(1,&buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,16,NULL,GL_STREAM_READ);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,buffer);
    glEnable(GL_RASTERIZER_DISCARD);unsigned count=0;
    for(int file=1;file<argc;file+=2) {
        FILE *f=fopen(argv[file],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
        char *text=calloc(n+1,1);assert(text&&fread(text,1,n,f)==(size_t)n);fclose(f);
        GLuint vs=compile(GL_VERTEX_SHADER,text),program=glCreateProgram();free(text);glAttachShader(program,vs);
        const char *varying="result";glTransformFeedbackVaryings(program,1,&varying,GL_INTERLEAVED_ATTRIBS);glLinkProgram(program);
        GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);assert(ok);glUseProgram(program);
        GLint loc=glGetUniformLocation(program,"probe");assert(loc>=0);
        f=fopen(argv[file+1],"rb");assert(f);float values[20];
        while(fread(values,sizeof(values),1,f)==1) {
            glUniform4fv(loc,4,values);glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,1);glEndTransformFeedback();
            float actual[4];glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,16,actual);assert(glGetError()==GL_NO_ERROR);
            for(unsigned k=0;k<4;k++)if(!(fabsf(actual[k]-values[16+k])<=0.00005f*fmaxf(1,fabsf(values[16+k])))) {
                fprintf(stderr,"%s case %u lane %u: %.9g != %.9g\n",argv[file],count,k,actual[k],values[16+k]);return 1;
            }
            count++;
        }
        fclose(f);glDeleteProgram(program);glDeleteShader(vs);
    }
    /* SPRITE */
    glDeleteBuffers(1,&buffer);glDeleteVertexArrays(1,&vao);
    printf("PASS: %u production point-size/fog shader probes\n",count);
    return 0;
}
