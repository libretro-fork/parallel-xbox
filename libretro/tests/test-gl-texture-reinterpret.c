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
    unsigned bytes_per_pixel;
} Format;
typedef struct {
    bool color;
    unsigned width, height, pitch;
    GLuint gl_buffer;
    Format fmt;
    GLuint dl_pbo;
    size_t dl_pbo_size;
    GLsync dl_fence;
    int dl_draw_time, draw_time;
    bool dl_native, dl_swz;
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
enum { NV2A_PROF_SURF_TO_TEX_REINTERPRET, NV2A_PROF_SURF_DOWNLOAD };
static unsigned conversions;
static void nv2a_profile_inc_counter(int id)
{ if (id == NV2A_PROF_SURF_TO_TEX_REINTERPRET) conversions++; }
static void pgraph_gl_draw_state_invalidate(PGRAPHGLState *r) { (void)r; }
static void pgraph_gl_tex_shadow_invalidate(PGRAPHGLState *r) { (void)r; }
static void pgraph_apply_scaling_factor(PGRAPHState *pg, unsigned *w, unsigned *h)
{ *w *= pg->surface_scale_factor; *h *= pg->surface_scale_factor; }
static void fb_bind(PGRAPHGLState *r, GLuint fbo)
{ glBindFramebuffer(GL_FRAMEBUFFER, fbo); r->fb_bound = fbo; }
static void bind_current_surface(NV2AState *d)
{ fb_bind(d->pgraph.gl_renderer_state, 0); }
/* Swizzled rendering is covered by the guest GPU fixture. This oracle
 * exercises linear color and depth download sizing with real GL transfers. */
static bool surface_swizzle_on_gpu(NV2AState *d, SurfaceBinding *surface)
{ (void)d; (void)surface; return false; }
/* PRODUCTION */

static void check_download_storage(NV2AState *d)
{
    PGRAPHGLState *r = d->pgraph.gl_renderer_state;
    for (unsigned color = 0; color <= 1; color++) {
        SurfaceBinding s = {
            .color = color, .width = 32, .height = 16,
            .pitch = color ? 128 : 64,
            .fmt = color ? (Format){ GL_RGBA8, GL_BGRA,
                GL_UNSIGNED_INT_8_8_8_8_REV, GL_COLOR_ATTACHMENT0, 4 } :
                (Format){ GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT,
                          GL_UNSIGNED_SHORT, GL_DEPTH_ATTACHMENT, 2 }
        };
        glGenTextures(1, &s.gl_buffer);
        /* Grow a reused PBO, then return to native scale. Native-converted
         * depth must never allocate scale-squared storage. */
        const unsigned scales[] = {1, 2, 4, 1};
        for (unsigned pass = 0; pass < ARRAY_SIZE(scales); pass++) {
            unsigned scale = scales[pass], bpp = color ? 4 : 2;
            d->pgraph.surface_scale_factor = scale;
            unsigned w = s.width * scale, h = s.height * scale;
            uint8_t *pixels = malloc((size_t)w * h * bpp);
            assert(pixels);
            for (unsigned y = 0; y < h; y++) {
                for (unsigned x = 0; x < w; x++) {
                    unsigned value = (x / scale) + (y / scale) * s.width;
                    for (unsigned c = 0; c < bpp; c++) {
                        pixels[(y * w + x) * bpp + c] =
                            (value * 71 + c * 39 + pass * 17) & 255;
                    }
                }
            }
            glBindTexture(GL_TEXTURE_2D, s.gl_buffer);
            glTexImage2D(GL_TEXTURE_2D, 0, s.fmt.gl_internal_format, w, h,
                         0, s.fmt.gl_format, s.fmt.gl_type, pixels);
            /* A host rewrite stands in for a new emulated draw. Otherwise
             * the pending snapshot is correctly reused as still current. */
            ++s.draw_time;
            surface_download_issue(d, &s);
            GLsync pending = s.dl_fence;
            surface_download_issue(d, &s);
            assert(s.dl_fence == pending);
            GLenum result = glClientWaitSync(s.dl_fence,
                GL_SYNC_FLUSH_COMMANDS_BIT, 5000000000ULL);
            assert(result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED);
            assert(glGetError() == GL_NO_ERROR);
            unsigned rs = color ? scale : 1;
            size_t expected_size = (size_t)rs * rs * s.pitch * s.height;
            if (color && pass == 3) expected_size *= 16;
            glBindBuffer(GL_PIXEL_PACK_BUFFER, s.dl_pbo);
            GLint64 actual_size;
            glGetBufferParameteri64v(GL_PIXEL_PACK_BUFFER, GL_BUFFER_SIZE, &actual_size);
            assert((size_t)actual_size == expected_size);
            assert(s.dl_pbo_size == expected_size);
            const uint8_t *mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER,
                0, s.dl_pbo_size, GL_MAP_READ_BIT);
            assert(mapped);
            for (unsigned y = 0; y < s.height * rs; y++) {
                for (unsigned x = 0; x < s.width * rs; x++) {
                    const uint8_t *want = pixels +
                        (((color ? y : y * scale) * w) +
                         (color ? x : x * scale)) * bpp;
                    assert(!memcmp(mapped + (y * s.width * rs + x) * bpp,
                                   want, bpp));
                }
            }
            assert(glUnmapBuffer(GL_PIXEL_PACK_BUFFER));
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            free(pixels);
            printf("download color=%u scale=%u: storage=%zu bytes; pixels exact\n",
                   color, scale, s.dl_pbo_size);
        }
        glDeleteSync(s.dl_fence);
        glDeleteBuffers(1, &s.dl_pbo);
        glDeleteTextures(1, &s.gl_buffer);
    }
    glDeleteTextures(1, &r->dl_zshrink_tex);
    glDeleteFramebuffers(1, &r->dl_zshrink_fbo);
}


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
                 GL_COLOR_ATTACHMENT0, 4 }
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
    check_download_storage(&d);
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
