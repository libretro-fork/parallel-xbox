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
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define g_realloc realloc
typedef struct {
    unsigned color_format, width, height, depth, pitch, levels;
    bool border, cubemap;
} TextureShape;
typedef struct {
    unsigned bytes_per_pixel;
    bool linear;
    GLenum gl_internal_format, gl_format, gl_type;
} ColorFormatInfo;
/* Host transfer formats, independent of guest format decoding. */
static const ColorFormatInfo kelvin_color_format_gl_map[] = {
    {1, false, GL_R8, GL_RED, GL_UNSIGNED_BYTE},
    {2, false, GL_RG8, GL_RG, GL_UNSIGNED_BYTE},
    {2, false, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5},
    {2, false, GL_RGB8_SNORM, GL_RGB, GL_BYTE}, /* converted R6G5B5 output */
    {4, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE},
    {1, true, GL_R8, GL_RED, GL_UNSIGNED_BYTE},
    {1, false, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE}, /* converted palette */
};
typedef struct {
    bool s3tc_native;
    uint8_t *tex_scratch[2];
    size_t tex_scratch_size[2];
} PGRAPHGLState;
static struct { struct { PGRAPHGLState *gl_renderer_state; } pgraph; } state;
#define g_nv2a (&state)
#define NV2A_PROF_TEX_UPLOAD 0
static void nv2a_profile_inc_counter(int counter) { (void)counter; }
/* These tests supply post-unswizzle/post-conversion bytes directly. */
static const uint8_t *unswizzle_level(PGRAPHGLState *r, const TextureShape *s,
    const uint8_t *data, const uint8_t *palette, unsigned w, unsigned h,
    unsigned d, unsigned bpp)
{
    (void)r; (void)s; (void)palette; (void)w; (void)h; (void)d; (void)bpp;
    return data;
}
static size_t pgraph_converted_texture_size(TextureShape s, unsigned w,
                                             unsigned h, unsigned d)
{
    return s.color_format == 3 ? (size_t)w * h * d * 3 :
           s.color_format == 6 ? (size_t)w * h * d * 4 : 0;
}
#define pgraph_convert_texture_data_into(...) abort()
#define s3tc_decompress_2d_into(...) abort()
#define upload_compressed_volume(...) abort()
/* PRODUCTION */

static void check_upload(unsigned format, unsigned width, unsigned height,
                          unsigned depth, unsigned levels, unsigned alignment)
{
    const ColorFormatInfo *f = &kelvin_color_format_gl_map[format];
    unsigned bpp = format == 3 ? 3 : f->bytes_per_pixel;
    TextureShape shape = { .color_format = format, .width = width,
        .height = height, .depth = depth, .levels = levels,
        .pitch = f->linear ? width + 2 : 0 };
    uint8_t data[8192], expected[8192], actual[8192];
    memset(data, 0x5a, sizeof(data));
    unsigned offset = 0, w = width, h = height, d = depth;
    for (unsigned level = 0; level < levels; level++) {
        unsigned pitch = f->linear ? shape.pitch : w * bpp;
        for (unsigned z = 0; z < d; z++)
        for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w * bpp; x++) {
            data[offset + (z * h + y) * pitch + x] =
                (level * 13 + z * 29 + y * 7 + x * 3 + 1) % 127;
        }
        offset += pitch * h * d;
        w = MAX(w / 2, 1u); h = MAX(h / 2, 1u); d = MAX(d / 2, 1u);
    }
    PGRAPHGLState r = {0};
    state.pgraph.gl_renderer_state = &r;
    GLenum target = depth > 1 ? GL_TEXTURE_3D : GL_TEXTURE_2D;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(target, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    upload_gl_texture(target, shape, data, NULL);
    assert(glGetError() == GL_NO_ERROR);
    GLint restored;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &restored);
    assert((unsigned)restored == alignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    offset = 0; w = width; h = height; d = depth;
    for (unsigned level = 0; level < levels; level++) {
        unsigned pitch = f->linear ? shape.pitch : w * bpp;
        for (unsigned z = 0; z < d; z++)
        for (unsigned y = 0; y < h; y++) {
            memcpy(expected + (z * h + y) * w * bpp,
                   data + offset + (z * h + y) * pitch, w * bpp);
        }
        glGetTexImage(target, level, f->gl_format, f->gl_type, actual);
        assert(glGetError() == GL_NO_ERROR);
        if (memcmp(actual, expected, (size_t)w * h * d * bpp)) {
            fprintf(stderr, "upload mismatch format=%u size=%ux%ux%u level=%u alignment=%u\n",
                    format, width, height, depth, level, alignment);
            abort();
        }
        offset += pitch * h * d;
        w = MAX(w / 2, 1u); h = MAX(h / 2, 1u); d = MAX(d / 2, 1u);
    }
    glDeleteTextures(1, &texture);
    free(r.tex_scratch[0]); free(r.tex_scratch[1]);
}

static void check_converted_cube(unsigned format)
{
    unsigned bpp = format == 3 ? 3 : 4;
    uint8_t data[16 * 16 * 4], expected[4 * 4 * 4], actual[4 * 4 * 4];
    for (unsigned y = 0; y < 16; y++)
    for (unsigned x = 0; x < 16 * bpp; x++) {
        data[y * 16 * bpp + x] = (y * 17 + x * 3 + 1) % 127;
    }
    for (unsigned y = 0; y < 4; y++) {
        memcpy(expected + y * 4 * bpp, data + ((y + 4) * 16 + 4) * bpp, 4 * bpp);
    }
    PGRAPHGLState r = {0};
    state.pgraph.gl_renderer_state = &r;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texture);
    TextureShape s = { .color_format = format, .width = 4, .height = 4,
        .depth = 1, .levels = 1, .border = true, .cubemap = true };
    upload_gl_texture(GL_TEXTURE_CUBE_MAP_POSITIVE_X, s, data, NULL);
    glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X, 0,
        kelvin_color_format_gl_map[format].gl_format,
        kelvin_color_format_gl_map[format].gl_type, actual);
    assert(glGetError() == GL_NO_ERROR);
    if (memcmp(actual, expected, 4 * 4 * bpp)) {
        fprintf(stderr, "converted cube crop mismatch format=%u\n", format);
        abort();
    }
    glDeleteTextures(1, &texture);
    free(r.tex_scratch[0]); free(r.tex_scratch[1]);
}

int main(void)
{
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
#else
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(eglInitialize(display, NULL, NULL));
    assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                       EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig config; EGLint count;
    assert(eglChooseConfig(display, attrs, &config, 1, &count) && count);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    assert(context != EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
#endif
    printf("GL upload oracle: %s; %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    unsigned cases = 0;
    for (unsigned alignment = 1; alignment <= 8; alignment *= 2) {
        for (unsigned format = 0; format < 6; format++) {
            check_upload(format, format == 5 ? 3 : 1, 8, 1,
                         format == 3 || format == 5 ? 1 : 4, alignment); cases++;
            if (format != 5) {
                check_upload(format, 2, 4, 4, format == 3 ? 1 : 3, alignment); cases++;
            }
        }
    }
    check_converted_cube(3); check_converted_cube(6); cases += 2;
    printf("upload cases passed=%u: row/slice/mip bytes exact; unpack alignment restored\n", cases);
#ifdef _WIN32
    wglMakeCurrent(NULL, NULL); wglDeleteContext(context);
    ReleaseDC(hwnd, dc); DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
#else
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context); eglTerminate(display);
#endif
    return 0;
}
