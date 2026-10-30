/* SPDX-License-Identifier: GPL-2.0-or-later */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef struct {
    GLenum gl_internal_format, gl_format, gl_type, gl_attachment;
} Format;
typedef struct {
    bool color;
    unsigned width, height, pitch;
    GLuint gl_buffer;
    Format fmt;
} SurfaceBinding;
typedef struct { GLuint gl_texture; } TextureBinding;
typedef struct {
    GLuint gl_fb_util, fb_bound, s2t_byte_pbo;
    GLuint dl_shrink_fbo, dl_shrink_tex, dl_zshrink_fbo, dl_zshrink_tex;
    unsigned dl_shrink_w, dl_shrink_h, dl_zshrink_w, dl_zshrink_h;
    GLint dl_shrink_fmt, dl_zshrink_fmt;
} PGRAPHGLState;
typedef struct {
    PGRAPHGLState *gl_renderer_state;
    unsigned surface_scale_factor;
} PGRAPHState;
typedef struct { PGRAPHState pgraph; } NV2AState;
enum { NV2A_PROF_SURF_TO_TEX_REINTERPRET };
static unsigned conversions;
static void nv2a_profile_inc_counter(int id) { (void)id; conversions++; }
static void pgraph_gl_draw_state_invalidate(PGRAPHGLState *r) { (void)r; }
static void pgraph_gl_tex_shadow_invalidate(PGRAPHGLState *r) { (void)r; }
static void pgraph_apply_scaling_factor(PGRAPHState *pg, unsigned *w, unsigned *h)
{ *w *= pg->surface_scale_factor; *h *= pg->surface_scale_factor; }
static void fb_bind(PGRAPHGLState *r, GLuint fbo)
{ glBindFramebuffer(GL_FRAMEBUFFER, fbo); r->fb_bound = fbo; }
static void bind_current_surface(NV2AState *d)
{ fb_bind(d->pgraph.gl_renderer_state, 0); }
/* PRODUCTION */

int main(void)
{
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
    printf("GL oracle: %s; %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    PGRAPHGLState r = {0};
    NV2AState d = { .pgraph.gl_renderer_state = &r };
    SurfaceBinding surface = {
        .color = true, .width = 1024, .height = 32, .pitch = 4096,
        .fmt = { GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                 GL_COLOR_ATTACHMENT0 }
    };
    TextureBinding binding;
    glGenFramebuffers(1, &r.gl_fb_util);
    glGenTextures(1, &surface.gl_buffer);
    glGenTextures(1, &binding.gl_texture);
    uint16_t expected[65536], actual[65536];
    const GLenum stores[] = {
        GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS,
        GL_PACK_SKIP_ROWS, GL_PACK_SWAP_BYTES,
        GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS,
        GL_UNPACK_SKIP_ROWS, GL_UNPACK_SWAP_BYTES
    };
    const GLint values[] = {8, 27, 3, 2, 1, 8, 29, 2, 3, 1};
    GLuint sentinels[2];
    glGenBuffers(2, sentinels);
    for (unsigned scale = 1; scale <= 4; scale *= 2) {
        d.pgraph.surface_scale_factor = scale;
        for (unsigned pass = 0; pass < 2; pass++) {
            for (unsigned i = 0; i < ARRAY_SIZE(expected); i++) {
                expected[i] = i ^ (pass ? 0x79a3 : 0);
            }
            uint8_t *pixels = malloc(sizeof(expected) * scale * scale);
            assert(pixels);
            for (unsigned y = 0; y < surface.height * scale; y++) {
                for (unsigned x = 0; x < surface.width * scale; x++) {
                    memcpy(pixels + (y * surface.width * scale + x) * 4,
                           (uint8_t *)expected +
                               ((y / scale) * surface.width + x / scale) * 4, 4);
                }
            }
            glBindTexture(GL_TEXTURE_2D, surface.gl_buffer);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, surface.width * scale,
                         surface.height * scale, 0, GL_BGRA,
                         GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
            free(pixels);
            glBindTexture(GL_TEXTURE_2D, binding.gl_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, surface.width * 2,
                         surface.height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
            for (unsigned i = 0; i < ARRAY_SIZE(stores); i++) {
                glPixelStorei(stores[i], values[i]);
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, sentinels[0]);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, sentinels[1]);
            pgraph_gl_reinterpret_surface_to_texture(&d, &surface, &binding);
            assert(glGetError() == GL_NO_ERROR);
            for (unsigned i = 0; i < ARRAY_SIZE(stores); i++) {
                GLint value;
                glGetIntegerv(stores[i], &value);
                assert(value == values[i]);
                glPixelStorei(stores[i], (i == 0 || i == 5) ? 1 : 0);
            }
            GLint value;
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &value);
            assert((GLuint)value == sentinels[0]);
            glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &value);
            assert((GLuint)value == sentinels[1]);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, actual);
            assert(glGetError() == GL_NO_ERROR);
            assert(!memcmp(actual, expected, sizeof(actual)));
            printf("scale=%u pass=%u: all 65536 RGB565 words exact; state restored\n",
                   scale, pass);
        }
    }
    assert(conversions == 6);
    glDeleteBuffers(1, &r.s2t_byte_pbo);
    glDeleteBuffers(2, sentinels);
    glDeleteTextures(1, &surface.gl_buffer);
    glDeleteTextures(1, &binding.gl_texture);
    glDeleteTextures(1, &r.dl_shrink_tex);
    glDeleteFramebuffers(1, &r.dl_shrink_fbo);
    glDeleteFramebuffers(1, &r.gl_fb_util);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglTerminate(display);
    return 0;
}
