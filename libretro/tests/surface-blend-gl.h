/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <EGL/egl.h>
#include <math.h>
static GLuint shader(GLenum type,const char *source) {
    GLuint s=glCreateShader(type);glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);assert(ok);return s;
}
static float factor(unsigned f,unsigned fmt,unsigned alpha) {
    float a=fmt==4?1.0f:alpha/255.0f;
    switch(f) {case 0:return 0;case 1:return 1;case 6:return a;case 7:return 1-a;default:assert(false);return 0;}
}
int main(void) {
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
    assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
    EGLConfig config;EGLint n;assert(eglChooseConfig(display,attributes,&config,1,&n) && n==1);
    EGLint context_attributes[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attributes);assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 430\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2-1,0,1);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 430\nlayout(location=0)out vec4 color;void main(){color=vec4(0.25,0.5,0.75,0.5);}");
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);assert(ok);glUseProgram(program);
    GLuint vao,texture,fbo;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,1,1);glDisable(GL_DITHER);
    static PGRAPHState pg;PGRAPHVkState r={0};pg.vk_renderer_state=&r;pg.surface_scale_factor=1;
    pg.surface_binding_dim.width=pg.surface_binding_dim.height=1;unsigned cases=0;
    const unsigned factors[]={0,1,6,7},alphas[]={0,64,255};
    for(unsigned fmt_index=0;fmt_index<2;fmt_index++) for(unsigned a=0;a<3;a++)
    for(unsigned s=0;s<4;s++) for(unsigned d=0;d<4;d++) {
        unsigned fmt=fmt_index?8:4,alpha=alphas[a],sf=factors[s],df=factors[d];
        glClearColor(32/255.0f,64/255.0f,96/255.0f,alpha/255.0f);glClear(GL_COLOR_BUFFER_BIT);
        pg.surface_shape.color_format=fmt;pg.regs[NV_PGRAPH_BLEND]=2|8|(sf<<4)|(df<<8);
        apply_gl_draw_state(&pg);glDrawArrays(GL_TRIANGLES,0,3);uint8_t pixel[4];glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        for(unsigned c=0;c<3;c++) {
            float expected=fminf(1,0.25f*(c+1)*factor(sf,fmt,alpha)+(32.0f*(c+1)/255)*factor(df,fmt,alpha));
            int error=(int)pixel[c]-(int)lroundf(expected*255);assert(error>=-1 && error<=1);
        }
        assert(glGetError()==GL_NO_ERROR);cases++;
    }
    printf("GL surface blend: %u real draws, XRGB/ARGB, both factors and stored alpha 0/64/255 passed\n",cases);
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);glDeleteVertexArrays(1,&vao);
    glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglTerminate(display);
}
