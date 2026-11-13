/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real WGL contexts: frontend resolution and native-only Vulkan setup. */
#include "ui/gl-dispatch.h"
#include "gloffscreen.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern void libretro_gl_prepare(void);
extern void libretro_gl_set_standalone_mode(void);
static unsigned resolutions;
static rglgen_func_t resolve(const char *name)
{
    resolutions++;
    return (rglgen_func_t)wglGetProcAddress(name);
}
static rglgen_func_t forbidden(const char *name)
{
    (void)name;
    assert(!"dispatch rewritten after publication");
    return NULL;
}
int main(int argc, char **argv)
{
    bool frontend = argc > 1 && !strcmp(argv[1], "frontend");
    HWND window = NULL;
    HDC dc = NULL;
    HGLRC primary = NULL;
    if (frontend) {
        WNDCLASSA wc = {0};
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "XemuDispatchTest";
        assert(RegisterClassA(&wc));
        window = CreateWindowA(wc.lpszClassName,"",0,0,0,1,1,NULL,NULL,wc.hInstance,NULL);
        assert(window);
        dc = GetDC(window);
        PIXELFORMATDESCRIPTOR pfd = {0};
        pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32;
        int pf = ChoosePixelFormat(dc,&pfd);
        assert(pf && SetPixelFormat(dc,pf,&pfd));
        HGLRC bootstrap = wglCreateContext(dc);
        assert(bootstrap && wglMakeCurrent(dc,bootstrap));
        typedef HGLRC (WINAPI *Create)(HDC,HGLRC,const int*);
        Create create = (Create)wglGetProcAddress("wglCreateContextAttribsARB");
        const int attrs[] = {0x2091,4,0x2092,0,0x9126,1,0};
        assert(create);
        primary = create(dc,NULL,attrs);
        assert(primary && wglMakeCurrent(dc,primary));
        wglDeleteContext(bootstrap);
        assert(xemu_gl_load(resolve) && resolutions > 0);
        libretro_gl_prepare();
    } else {
        /* No frontend GL resolver/context is available on this path. */
        assert(!xemu_gl_load(NULL));
        libretro_gl_set_standalone_mode();
    }
    GloContext *first = glo_context_create();
    assert(first);
    glo_set_current(first);
    assert(xemu_gl_version() >= 40);
    if (xemu_gl_version() >= 43) {
        assert(glDispatchCompute && glBindImageTexture && glMemoryBarrier);
    }
    printf("selected_gl=%d mode=%s\n",xemu_gl_version(),frontend?"frontend":"native");
    assert(xemu_gl_load(forbidden));
    assert(xemu_gl_has_extension("GL_ARB_vertex_array_object"));
    assert(!xemu_gl_has_extension("GL_ARB_vertex_array"));
    assert(!xemu_gl_has_extension("GL_ARB_vertex_array_object extra"));
    assert(!xemu_gl_has_extension("") && !xemu_gl_has_extension(NULL));
    GLuint buffer = 0;
    glGenBuffers(1,&buffer); glBindBuffer(GL_ARRAY_BUFFER,buffer);
    int value = 0x12345678, readback = 0;
    glBufferData(GL_ARRAY_BUFFER,sizeof(value),&value,GL_STATIC_DRAW);
    assert(glIsBuffer(buffer));
    GloCurrent saved;
    glo_current_save(&saved);
    GloContext *second = glo_context_create();
    assert(second); glo_set_current(second);
    assert(glIsBuffer(buffer));
    glBindBuffer(GL_ARRAY_BUFFER,buffer);
    glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(readback),&readback);
    assert(readback == value);
    glo_current_restore(&saved);
    glDeleteBuffers(1,&buffer);
    assert(glGetError() == GL_NO_ERROR);
    glo_context_destroy(second); glo_context_destroy(first);
    if (!frontend) {
        /* The standalone share root must not outlive its owned context. */
        for (unsigned cycle=0;cycle<3;cycle++) {
            GloContext *restarted=glo_context_create();assert(restarted);
            glo_set_current(restarted);assert(xemu_gl_version()>=40);
            GLuint object=0;glGenBuffers(1,&object);glBindBuffer(GL_ARRAY_BUFFER,object);
            glBufferData(GL_ARRAY_BUFFER,sizeof(value),&value,GL_STATIC_DRAW);
            glGetBufferSubData(GL_ARRAY_BUFFER,0,sizeof(readback),&readback);
            assert(readback==value && glGetError()==GL_NO_ERROR);
            glDeleteBuffers(1,&object);glo_context_destroy(restarted);
        }
        puts("native-only: 3 complete share-root destroy/recreate lifecycles PASS");
    }
    if (frontend) {
        wglMakeCurrent(NULL,NULL); wglDeleteContext(primary);
        ReleaseDC(window,dc); DestroyWindow(window);
    }
    printf("%s: GL dispatch, extension boundaries, immutable reload, shared buffers/context restore PASS\n", frontend?"frontend":"native-only");
    return 0;
}
