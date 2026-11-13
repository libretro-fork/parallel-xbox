/* SPDX-License-Identifier: GPL-2.0-or-later */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef struct { bool initialized; GLuint gl_program, gl_pipeline; } ShaderBinding;
typedef struct {
    GLuint touch_prog, touch_vao, fresh_tex[4];
    unsigned fresh_tex_r, fresh_tex_w;
    GLuint sampled_texture_pool;
    ShaderBinding *shader_binding;
} PGRAPHGLState;
static bool sso;
static unsigned compiles, links, vaos, draws;
static bool gl_sso_enabled(void) { return sso; }
static bool sampled_texture_contains(const GLuint *sampled, GLuint tex)
{
    return *sampled == tex;
}
static void counted_compile(GLuint shader) { compiles++; glCompileShader(shader); }
static void counted_link(GLuint program) { links++; glLinkProgram(program); }
static void counted_vaos(GLsizei n, GLuint *names) { vaos += n; glGenVertexArrays(n, names); }
static void counted_draw(GLenum mode, GLint first, GLsizei count)
{
    assert(mode == GL_POINTS && first == 0 && count == 1);
    draws++;
    glDrawArrays(mode, first, count);
}
#define glCompileShader counted_compile
#define glLinkProgram counted_link
#define glGenVertexArrays counted_vaos
#define glDrawArrays counted_draw
/* PRODUCTION */

static void expect_int(GLenum name, GLint expected)
{
    GLint value;
    glGetIntegerv(name, &value);
    assert(value == expected);
}
int main(void)
{
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(display != EGL_NO_DISPLAY && eglInitialize(display, NULL, NULL));
    assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig config;
    EGLint count;
    assert(eglChooseConfig(display, attrs, &config, 1, &count) && count == 1);
    EGLint ca[] = { EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3,
                    EGL_NONE };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, ca);
    assert(context != EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));

    GLuint textures[3], fbo, prior_vao, pipeline;
    glGenTextures(3, textures);
    for (unsigned i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 2, 2);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        const uint8_t texels[16] = { 192,64,32,255, 192,64,32,255,
                                    192,64,32,255, 192,64,32,255 };
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA,
                        GL_UNSIGNED_BYTE, texels);
    }
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, textures[0], 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    /* Put the utility's point inside its 1x1 scissor, so a broken write
     * mask changes real pixels instead of passing with an empty draw. */
    glViewport(0, 0, 1, 1);
    glClearColor(0.25, 0.5, 0.75, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
    uint8_t before[16], after[16];
    glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, before);
    glGenVertexArrays(1, &prior_vao);
    glGenProgramPipelines(1, &pipeline);
    for (unsigned generation = 0; generation < 3; generation++) {
        PGRAPHGLState r = {0};
        unsigned old_compiles = compiles, old_links = links, old_vaos = vaos;
        init_texture_touch(&r);
        assert(compiles == old_compiles + 2 && links == old_links + 1 &&
               vaos == old_vaos + 1);
        GLint linked;
        glGetProgramiv(r.touch_prog, GL_LINK_STATUS, &linked);
        assert(linked && glIsVertexArray(r.touch_vao));
        GLint sampler = -1;
        glGetUniformiv(r.touch_prog, glGetUniformLocation(r.touch_prog, "s"), &sampler);
        assert(sampler == 0);
        old_compiles = compiles; old_links = links; old_vaos = vaos;
        for (unsigned mode = 0; mode < 2; mode++) {
            sso = mode;
            ShaderBinding live = { true, r.touch_prog, pipeline };
            r.shader_binding = &live;
            glBindProgramPipeline(sso ? pipeline : 0);
            glUseProgram(sso ? 0 : live.gl_program);
            glBindVertexArray(prior_vao);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, textures[0]);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, textures[2]);
            glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
            glDepthMask(GL_FALSE);
            glStencilMask(0xa4);
            glEnable(GL_SCISSOR_TEST);
            glScissor(1, 0, 1, 2);
            unsigned old_draws = draws;
            r.fresh_tex_r = 0; r.fresh_tex_w = 2;
            r.fresh_tex[0] = textures[1]; r.fresh_tex[1] = textures[2];
            r.sampled_texture_pool = textures[2];
            touch_fresh_textures(&r, 1);
            assert(r.fresh_tex_r == 1 && draws == old_draws + 1);
            touch_fresh_textures(&r, 1);
            touch_fresh_textures(&r, 1); /* empty queue */
            assert(r.fresh_tex_r == 2 && draws == old_draws + 1);
            assert(compiles == old_compiles && links == old_links && vaos == old_vaos);
            expect_int(GL_VERTEX_ARRAY_BINDING, prior_vao);
            expect_int(GL_DRAW_FRAMEBUFFER_BINDING, fbo);
            expect_int(GL_ACTIVE_TEXTURE, GL_TEXTURE3);
            expect_int(GL_TEXTURE_BINDING_2D, textures[2]);
            glActiveTexture(GL_TEXTURE0);
            expect_int(GL_TEXTURE_BINDING_2D, textures[0]);
            expect_int(GL_CURRENT_PROGRAM, sso ? 0 : live.gl_program);
            expect_int(GL_PROGRAM_PIPELINE_BINDING, sso ? pipeline : 0);
            expect_int(GL_STENCIL_WRITEMASK, 0xa4);
            GLboolean mask[4], depth;
            glGetBooleanv(GL_COLOR_WRITEMASK, mask);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depth);
            assert(mask[0] && !mask[1] && mask[2] && !mask[3] && !depth);
            GLint box[4]; glGetIntegerv(GL_SCISSOR_BOX, box);
            assert(glIsEnabled(GL_SCISSOR_TEST) && box[0] == 1 && box[1] == 0 &&
                   box[2] == 1 && box[3] == 2);
            glReadPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, after);
            assert(!memcmp(before, after, sizeof(before)));
            assert(glGetError() == GL_NO_ERROR);
        }
        glUseProgram(0); glBindProgramPipeline(0); glBindVertexArray(prior_vao);
        GLuint retired_program = r.touch_prog, retired_vao = r.touch_vao;
        finalize_texture_touch(&r);
        assert(!r.touch_prog && !r.touch_vao);
        assert(!glIsProgram(retired_program) && !glIsVertexArray(retired_vao));
    }
    glDeleteProgramPipelines(1, &pipeline);
    glDeleteVertexArrays(1, &prior_vao);
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(3, textures);
    assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT));
    assert(eglDestroyContext(display, context)); eglTerminate(display);
    puts("Texture touch: startup compilation only, fixed quotas, sampled skips, unchanged pixels/state in monolithic/SSO modes and three renderer owners PASS");
}
