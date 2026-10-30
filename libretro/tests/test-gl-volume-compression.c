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
#include "s3tc.h"
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define g_realloc realloc
typedef struct {
    bool s3tc_3d_native;
    uint8_t *tex_scratch[2];
    size_t tex_scratch_size[2];
} PGRAPHGLState;
/* PRODUCTION */

static bool has_extension(const char *extension)
{
    const char *p = (const char *)glGetString(GL_EXTENSIONS);
    size_t length = strlen(extension);
    while ((p = strstr(p, extension))) {
        if ((p[length] == ' ' || !p[length]) &&
            (p == (const char *)glGetString(GL_EXTENSIONS) || p[-1] == ' ')) {
            return true;
        }
        p += length;
    }
    return false;
}

/* Independent normalized-color oracle; preserve interpolation precision
 * until the final RGBA8 quantization, as the decoder's DirectXTex oracle. */
static void reference_pixel(const uint8_t *block, unsigned mode, unsigned p,
                            uint8_t *pixel)
{
    const uint8_t *color = block + (mode ? 8 : 0);
    unsigned a = color[0] | color[1] << 8, b = color[2] | color[3] << 8;
    unsigned index = (color[4 + p / 4] >> (2 * (p % 4))) & 3;
    bool transparent = !mode && a <= b;
    for (unsigned ch = 0; ch < 3; ch++) {
        unsigned shift = ch == 0 ? 11 : ch == 1 ? 5 : 0;
        unsigned mask = ch == 1 ? 63 : 31;
        double c0 = ((a >> shift) & mask) / (double)mask;
        double c1 = ((b >> shift) & mask) / (double)mask;
        double value = index == 0 ? c0 : index == 1 ? c1 :
            transparent ? (index == 2 ? (c0 + c1) / 2 : 0) :
            ((4 - index) * c0 + (index - 1) * c1) / 3;
        pixel[ch] = (uint8_t)floor(value * 255 + 0.5001);
    }
    double alpha = transparent && index == 3 ? 0 : 1;
    if (mode == 1) {
        alpha = ((block[p / 2] >> (4 * (p % 2))) & 15) / 15.0;
    } else if (mode == 2) {
        uint64_t selectors = 0;
        for (unsigned i = 0; i < 6; i++) selectors |= (uint64_t)block[2+i] << (8*i);
        unsigned ai = (selectors >> (3*p)) & 7;
        double c0 = block[0] / 255.0, c1 = block[1] / 255.0;
        alpha = ai == 0 ? c0 : ai == 1 ? c1 : c0 > c1 ?
            ((8-ai)*c0 + (ai-1)*c1)/7 : ai < 6 ?
            ((6-ai)*c0 + (ai-1)*c1)/5 : ai == 6 ? 0 : 1;
    }
    pixel[3] = (uint8_t)floor(alpha * 255 + 0.5001);
}

static void check_level(PGRAPHGLState *r, unsigned level, unsigned w,
                        unsigned h, unsigned depth, unsigned mode,
                        bool precision)
{
    unsigned bw = (w + 3) / 4, bh = (h + 3) / 4, bs = mode ? 16 : 8;
    size_t bytes = (size_t)bw * bh * depth * bs;
    uint8_t *blocks = calloc(1, bytes);
    uint8_t *expected = malloc((size_t)w * h * depth * 4);
    uint8_t *actual = malloc((size_t)w * h * depth * 4);
    const uint16_t colors[] = {0, 0xf800, 0x07e0, 0x001f,
                               0xffff, 0xffe0, 0xf81f, 0x07ff};
    assert(blocks && expected && actual);
    unsigned index = 0;
    for (unsigned z0 = 0; z0 < depth; z0 += 4)
    for (unsigned by = 0; by < bh; by++)
    for (unsigned bx = 0; bx < bw; bx++)
    for (unsigned z = 0; z < 4 && z0 + z < depth; z++) {
        unsigned code = bx + bw * (by + bh * (z0 + z));
        uint16_t rgb = colors[code % 8];
        uint8_t alpha = mode ? (code % 16) * 17 : 255;
        uint8_t *block = blocks + index++ * bs;
        unsigned offset = mode ? 8 : 0;
        block[offset] = rgb & 255;
        block[offset + 1] = rgb >> 8;
        if (mode == 1) memset(block, alpha, 8);
        if (mode == 2) block[0] = alpha;
        if (precision) {
            const uint16_t endpoints[] = {0x0841, 0x18e3, 0x39e7, 0x7bef,
                                         0xa534, 0xdead, 0x1234, 0xffff};
            uint16_t a = endpoints[code % 8];
            uint16_t b = endpoints[(code / 8) % 8];
            block[offset] = a; block[offset + 1] = a >> 8;
            block[offset + 2] = b; block[offset + 3] = b >> 8;
            memset(block + offset + 4, 0xe4, 4);
            if (mode == 1) {
                for (unsigned i = 0; i < 8; i++) block[i] = 2*i | ((2*i+1) << 4);
            } else if (mode == 2) {
                block[0] = code & 255;
                block[1] = code % 3 == 0 ? block[0] : (255 - code) & 255;
                uint64_t selectors = 0;
                for (unsigned i = 0; i < 16; i++) selectors |= (uint64_t)(i%8) << (3*i);
                for (unsigned i = 0; i < 6; i++) block[2+i] = selectors >> (8*i);
            }
        }
        for (unsigned y = by * 4; y < by * 4 + 4 && y < h; y++)
        for (unsigned x = bx * 4; x < bx * 4 + 4 && x < w; x++) {
            uint8_t *pixel = expected + (((z0 + z) * h + y) * w + x) * 4;
            pixel[0] = (rgb & 0xf800) ? 255 : 0;
            pixel[1] = (rgb & 0x07e0) ? 255 : 0;
            pixel[2] = (rgb & 31) ? 255 : 0;
            pixel[3] = alpha;
            if (precision) reference_pixel(block, mode, (y%4)*4 + x%4, pixel);
        }
    }
    GLenum formats[] = { GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
                         GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,
                         GL_COMPRESSED_RGBA_S3TC_DXT5_EXT };
    upload_compressed_volume(r, GL_TEXTURE_3D, level, formats[mode], blocks,
                              w, h, depth, bytes);
    assert(glGetError() == GL_NO_ERROR);
    GLint internal, width, height, layers;
    glGetTexLevelParameteriv(GL_TEXTURE_3D, level, GL_TEXTURE_INTERNAL_FORMAT, &internal);
    glGetTexLevelParameteriv(GL_TEXTURE_3D, level, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_3D, level, GL_TEXTURE_HEIGHT, &height);
    glGetTexLevelParameteriv(GL_TEXTURE_3D, level, GL_TEXTURE_DEPTH, &layers);
    assert((unsigned)width == w && (unsigned)height == h && (unsigned)layers == depth);
    assert((GLenum)internal == (r->s3tc_3d_native ? formats[mode] : GL_RGBA8));
    glGetTexImage(GL_TEXTURE_3D, level, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    assert(glGetError() == GL_NO_ERROR);
    assert(!memcmp(actual, expected, (size_t)w * h * depth * 4));
    if (r->s3tc_3d_native) assert(!r->tex_scratch[1]);
    free(actual);
    free(expected);
    free(blocks);
}

int main(void)
{
#ifdef _WIN32
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "vtc-oracle";
    assert(RegisterClassA(&wc));
    HWND hwnd = CreateWindowA(wc.lpszClassName, "vtc", WS_OVERLAPPEDWINDOW,
                             0, 0, 64, 64, NULL, NULL, wc.hInstance, NULL);
    assert(hwnd);
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
    EGLConfig config;
    EGLint count;
    assert(eglChooseConfig(display, attrs, &config, 1, &count) && count);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    assert(context != EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));
#endif
    bool vtc = has_extension("GL_NV_texture_compression_vtc");
    printf("GL volume oracle: %s; %s; VTC=%d S3TC=%d\n",
           glGetString(GL_RENDERER), glGetString(GL_VERSION), vtc,
           has_extension("GL_EXT_texture_compression_s3tc"));
    unsigned cases = 0;
    for (unsigned native = 0; native <= (unsigned)vtc; native++) {
        PGRAPHGLState r = { .s3tc_3d_native = native };
        GLuint texture;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_3D, texture);
        for (unsigned mode = 0; mode < 3; mode++) {
            for (unsigned d = 1; d <= 8; d *= 2) {
                check_level(&r, 0, 8, 8, d, mode, false); cases++;
            }
            for (unsigned level = 0; level < 4; level++) {
                check_level(&r, level, 16 >> level, 8 >> level,
                            MAX(8 >> level, 1u), mode, false); cases++;
            }
        }
        if (!native) {
            for (unsigned mode = 0; mode < 3; mode++) {
                check_level(&r, 0, 32, 32, 8, mode, true); cases++;
                check_level(&r, 0, 7, 5, 3, mode, true); cases++;
                for (unsigned level = 0; level < 5; level++) {
                    check_level(&r, level, MAX(16 >> level, 1u),
                                MAX(8 >> level, 1u), MAX(8 >> level, 1u),
                                mode, true); cases++;
                }
            }
            puts("fallback: 21 interpolated RGB/alpha levels exact against normalized oracle");
        }
        printf("native=%u: 24 volume levels exact, alpha and block/slice order preserved\n",
               native);
        glDeleteTextures(1, &texture);
        free(r.tex_scratch[1]);
    }
    printf("volume cases passed=%u\n", cases);
#ifdef _WIN32
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(context);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
#else
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
#endif
    return 0;
}
