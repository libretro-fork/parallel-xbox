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
#include "display-gamma-fixture.h"
static GLuint compile(GLenum type,const char *source)
{
    GLuint shader=glCreateShader(type);glShaderSource(shader,1,&source,NULL);glCompileShader(shader);
    GLint ok;glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[8192];glGetShaderInfoLog(shader,sizeof(log),NULL,log);fputs(log,stderr);}assert(ok);return shader;
}
int main(int argc,char **argv)
{
    assert(argc==2);
#ifdef _WIN32
    WNDCLASSA wc={0};wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(NULL);wc.lpszClassName="gamma-oracle";assert(RegisterClassA(&wc));
    HWND window=CreateWindowA(wc.lpszClassName,"gamma",WS_OVERLAPPEDWINDOW,0,0,64,64,NULL,NULL,wc.hInstance,NULL);
    HDC dc=GetDC(window);PIXELFORMATDESCRIPTOR pfd={0};pfd.nSize=sizeof(pfd);pfd.nVersion=1;pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;pfd.iPixelType=PFD_TYPE_RGBA;pfd.cColorBits=32;
    assert(SetPixelFormat(dc,ChoosePixelFormat(dc,&pfd),&pfd));HGLRC context=wglCreateContext(dc);assert(context&&wglMakeCurrent(dc,context));
#else
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE},n;EGLConfig config;assert(eglChooseConfig(display,attrs,&config,1,&n)&&n);
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,NULL);assert(context!=EGL_NO_CONTEXT&&eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
#endif
    printf("Gamma GL GPU: %s\n",glGetString(GL_RENDERER));
    FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long bytes=ftell(f);rewind(f);char *source=calloc(bytes+1,1);assert(source&&fread(source,1,bytes,f)==(size_t)bytes);fclose(f);
    const char *vertex="#version 330\nvoid main(){float x=-1.0+float((gl_VertexID&1)<<2);float y=-1.0+float((gl_VertexID&2)<<1);gl_Position=vec4(x,y,0,1);}";
    GLuint vs=compile(GL_VERTEX_SHADER,vertex),fs=compile(GL_FRAGMENT_SHADER,source),program=glCreateProgram();free(source);
    glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);assert(ok);glDeleteShader(vs);glDeleteShader(fs);
    GLuint textures[3],fbo,vao;glGenTextures(3,textures);glGenFramebuffers(1,&fbo);glGenVertexArrays(1,&vao);glBindVertexArray(vao);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glBindTexture(GL_TEXTURE_2D,textures[2]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,GAMMA_WIDTH,GAMMA_HEIGHT,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[2],0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    for(unsigned unit=0;unit<2;unit++) {
        glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,textures[unit]);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    const uint8_t video[]={20,100,210,77};glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,video);
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"tex"),0);glUniform1i(glGetUniformLocation(program,"pvideo_tex"),1);
    glUniform2f(glGetUniformLocation(program,"display_size"),GAMMA_WIDTH,GAMMA_HEIGHT);glUniform1f(glGetUniformLocation(program,"line_offset"),1);
    glUniform2f(glGetUniformLocation(program,"pvideo_in_pos"),0,0);glUniform4f(glGetUniformLocation(program,"pvideo_pos"),0,0,GAMMA_WIDTH,GAMMA_HEIGHT);glUniform3f(glGetUniformLocation(program,"pvideo_scale"),1,1,1);glUniform3f(glGetUniformLocation(program,"pvideo_color_key"),0,0,0);
    glViewport(0,0,GAMMA_WIDTH,GAMMA_HEIGHT);glDisable(GL_DITHER);glDisable(GL_BLEND);glDisable(GL_FRAMEBUFFER_SRGB);
    uint8_t input[GAMMA_WIDTH*GAMMA_HEIGHT*4],actual[sizeof(input)],baseline[sizeof(input)];uint32_t table[256];unsigned cases=0;
    for(unsigned format=0;format<3;format++) {
        source_pixels(format,input);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,textures[0]);
        const GLint internal[]={GL_RGBA8,GL_RGB565,GL_RGB5_A1};const GLenum types[]={GL_UNSIGNED_BYTE,GL_UNSIGNED_SHORT_5_6_5,GL_UNSIGNED_SHORT_1_5_5_5_REV};const GLenum formats[]={GL_RGBA,GL_RGB,GL_BGRA};
        glTexImage2D(GL_TEXTURE_2D,0,internal[format],GAMMA_WIDTH,GAMMA_HEIGHT,0,formats[format],types[format],input);
        for(unsigned curve=0;curve<5;curve++)for(unsigned overlay=0;overlay<3;overlay++)for(unsigned enabled=0;enabled<2;enabled++) {
            gamma_table(table,curve);glUniform4uiv(glGetUniformLocation(program,"gamma_lut"),64,table);glUniform1i(glGetUniformLocation(program,"gamma_enable"),enabled);
            glUniform1i(glGetUniformLocation(program,"pvideo_enable"),overlay!=0);glUniform1i(glGetUniformLocation(program,"pvideo_color_key_enable"),overlay==2);
            glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(0,0,GAMMA_WIDTH,GAMMA_HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,actual);assert(glGetError()==GL_NO_ERROR);
            /* Driver expansion of normalized 565/1555 can differ by one byte.
             * Compare LUT application to that driver's uncorrected presentation. */
            if(!enabled)memcpy(baseline,actual,sizeof(baseline));
            gamma_check(actual,format,curve,overlay,enabled,true,format?baseline:NULL);cases++;
        }
    }
    printf("PASS: %u actual GL display shader cases, %u RGBA components; identity bypass, channel curves, alpha, overlay key ordering and RGB565/1555\n",cases,cases*GAMMA_WIDTH*GAMMA_HEIGHT*4);
    glDeleteTextures(3,textures);glDeleteFramebuffers(1,&fbo);glDeleteVertexArrays(1,&vao);glDeleteProgram(program);
#ifdef _WIN32
    wglMakeCurrent(NULL,NULL);wglDeleteContext(context);ReleaseDC(window,dc);DestroyWindow(window);
#else
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglTerminate(display);
#endif
}
