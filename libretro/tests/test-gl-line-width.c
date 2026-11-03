/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#ifdef REAL_GL
#include <EGL/egl.h>
#include <GL/gl.h>
#else
typedef float GLfloat;
#endif
typedef struct {bool line_smooth_on,wide_lines;GLfloat draw_line_width;
    GLfloat supported_smooth_line_width_range[2],supported_aliased_line_width_range[2];
    struct {bool valid;}draw_state_key;} PGRAPHGLState;
typedef struct {unsigned line_width,surface_scale_factor;} PGRAPHState;
static unsigned emissions;
static float driver_width;
static void test_line_width(float width){
    assert(width>0);emissions++;
#ifdef REAL_GL
    glLineWidth(width);glGetFloatv(GL_LINE_WIDTH,&driver_width);
    assert(glGetError()==GL_NO_ERROR);
#else
    driver_width=width;
#endif
}
static void disturb_width(void){
#ifdef REAL_GL
    glLineWidth(1.0f);glGetFloatv(GL_LINE_WIDTH,&driver_width);
#else
    driver_width=2.5f;
#endif
}
#define glLineWidth test_line_width
/* PRODUCTION */
int main(void)
{
#ifdef REAL_GL
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(display!=EGL_NO_DISPLAY);
    assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(display,attrs,&config,1,&count)&&count==1);
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,NULL);assert(context!=EGL_NO_CONTEXT);
    assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
#endif
    unsigned cases=0,expected_calls=0;bool known=false;float previous=0;
    PGRAPHGLState r={.supported_smooth_line_width_range={1,4},
                    .supported_aliased_line_width_range={1,8}};
#ifdef REAL_GL
    glGetFloatv(GL_SMOOTH_LINE_WIDTH_RANGE,r.supported_smooth_line_width_range);
    glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE,r.supported_aliased_line_width_range);
#endif
    /* A clamped driver value is the oracle: distinct guest values can name
     * identical host state. Intervening own draws invalidate the mirror. */
    for(unsigned width=0;width<512;width++)for(unsigned scale=1;scale<=8;scale++)
    for(unsigned smooth=0;smooth<2;smooth++)for(unsigned reset=0;reset<2;reset++){
        PGRAPHState pg={width,scale};r.line_smooth_on=smooth;
        if(reset){disturb_width();pgraph_gl_draw_state_invalidate(&r);known=false;assert(!r.draw_state_key.valid);}
        const float *range=smooth?r.supported_smooth_line_width_range:r.supported_aliased_line_width_range;
        float value=width*scale/8.0f;
        float expected=value<range[0]?range[0]:value>range[1]?range[1]:value;
        if(!known||expected!=previous)expected_calls++;
        update(&pg,&r);assert(driver_width==expected&&emissions==expected_calls);
        /* Repeated draws must retain the same driver state with no call. */
        update(&pg,&r);assert(driver_width==expected&&emissions==expected_calls);
        previous=expected;known=true;cases++;
    }
#ifdef REAL_GL
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context);eglTerminate(display);
#endif
    printf("PASS: %u GL width schedules; clamps, scale/smooth transitions, invalidation and repeated driver state; %u emissions\n",cases,emissions);
}
