/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real query-object ownership and sample-count check, not a timing fixture. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#ifdef _WIN32
#include "ui/gl-dispatch.h"
#include "gloffscreen.h"
extern void libretro_gl_set_standalone_mode(void);
#else
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include "qemu/queue.h"
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
/* CONTEXT */
static size_t live;
static void *g_malloc(size_t n){void *p=malloc(n);assert(p);live++;return p;}
static void *g_malloc_n(size_t n,size_t s){assert(!s||n<=SIZE_MAX/s);return g_malloc(n*s);}
static void *g_realloc_n(void *p,size_t n,size_t s){assert(p&&n<=SIZE_MAX/s);p=realloc(p,n*s);assert(p);return p;}
static void g_free(void *p){if(p){assert(live);live--;free(p);}}
/* POOL */
/* REPORT TYPE */
typedef struct {
    ObjPool report_pool;
    QSIMPLEQ_HEAD(, QueryReport) report_queue;
    QueryReport *zpass_report;
    bool gl_zpass_query_open, work_since_flush;
    uint32_t zpass_pixel_count_result;
} PGRAPHGLState;
typedef struct {PGRAPHGLState *gl_renderer_state;unsigned surface_scale_factor;bool reports_pending;} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
#define NV097_GET_REPORT_TYPE 0
#define NV097_GET_REPORT_TYPE_ZPASS_PIXEL_CNT 1
#define GET_MASK(value,mask) ((value)>>24)
static unsigned generated,deleted,writes;
static GLuint names[32768];
static uint32_t last_result;
static void counted_gen(GLsizei n,GLuint *out)
{
    glGenQueries(n,out);
    for(GLsizei i=0;i<n;i++){
        assert(out[i]&&generated<ARRAY_SIZE(names));names[generated++]=out[i];
    }
}
static void counted_delete(GLsizei n,const GLuint *ids)
{
    glDeleteQueries(n,ids);deleted+=(unsigned)n;
}
static void pgraph_write_zpass_pixel_cnt_report(NV2AState *d,uint32_t parameter,uint32_t result)
{
    (void)d;assert(parameter>>24==1);writes++;last_result=result;
}
#undef glGenQueries
#undef glDeleteQueries
#define glGenQueries counted_gen
#define glDeleteQueries counted_delete
/* PRODUCTION */
#undef glGenQueries
#undef glDeleteQueries
static GLuint shader(GLenum kind,const char *text)
{
    GLuint s=glCreateShader(kind);glShaderSource(s,1,&text,NULL);glCompileShader(s);
    GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);assert(ok);return s;
}
static GLuint draw_query(NV2AState *d,unsigned repeats,bool leave_open)
{
    GLuint q=pgraph_gl_alloc_zpass_query(d);
    PGRAPHGLState *r=d->pgraph.gl_renderer_state;QueryReport *report;
    for(unsigned i=0;i+1<r->zpass_report->query_count;i++)assert(r->zpass_report->queries[i]!=q);
    QSIMPLEQ_FOREACH(report,&r->report_queue,entry)
        for(unsigned i=0;i<report->query_count;i++)assert(report->queries[i]!=q);
    assert(!r->gl_zpass_query_open);glBeginQuery(GL_SAMPLES_PASSED,q);
    for(unsigned i=0;i<repeats;i++)glDrawArrays(GL_TRIANGLES,0,3);
    if(leave_open)r->gl_zpass_query_open=true;else glEndQuery(GL_SAMPLES_PASSED);
    assert(glGetError()==GL_NO_ERROR);return q;
}
static void completed_report(NV2AState *d,uint32_t expected)
{
    unsigned before=writes;
    pgraph_gl_get_report(d,0x01000000u+writes);
    glFinish(); /* Test-only deterministic completion, not gameplay pacing. */
    pgraph_gl_process_pending_reports(d);
    assert(writes==before+1&&last_result==expected&&!d->pgraph.reports_pending);
    assert(glGetError()==GL_NO_ERROR);
}
static void all_names_released(void)
{
    glFinish();
    assert(!live&&deleted==generated);
    for(unsigned i=0;i<generated;i++)assert(!glIsQuery(names[i]));
    assert(glGetError()==GL_NO_ERROR);
}
int main(void)
{
    signal(SIGABRT,quiet_abort);setvbuf(stdout,NULL,_IONBF,0);init_gl();
    GLuint fb,rb,vao;glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);
    glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,8,8);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glViewport(0,0,8,8);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 410\nvoid main(){vec2 p=gl_VertexID==0?vec2(-1,-1):gl_VertexID==1?vec2(3,-1):vec2(-1,3);gl_Position=vec4(p,0,1);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 410\nlayout(location=0)out vec4 color;void main(){color=vec4(1);}");
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);
    glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);assert(ok);
    glUseProgram(program);
    PGRAPHGLState r={0};NV2AState d={{&r,1,false}};pgraph_gl_init_reports(&d);
    uint32_t running=0;unsigned overflow_names=0,inline_high_water=0;
    const unsigned counts[]={0,1,8,17,3};
    for(unsigned cycle=0;cycle<100;cycle++){
        unsigned count=counts[cycle%ARRAY_SIZE(counts)];
        d.pgraph.surface_scale_factor=1+cycle%4;
        unsigned divisor=d.pgraph.surface_scale_factor*d.pgraph.surface_scale_factor;
        for(unsigned i=0;i<count;i++){
            unsigned repeats=1+(cycle+i)%3;draw_query(&d,repeats,false);
            running+=(64*repeats)/divisor;
        }
        overflow_names+=count>8?count-8:0;
        inline_high_water=inline_high_water>count?inline_high_water:count;
        if(inline_high_water>8)inline_high_water=8;
        completed_report(&d,running);
        if(cycle%5==0){
            GLuint discarded=draw_query(&d,4,true);
            pgraph_gl_clear_report_value(&d);
            pgraph_gl_process_pending_reports(&d);assert(!r.zpass_pixel_count_result);
            GLuint fresh=draw_query(&d,1,false);
#ifdef GL_REPORT_REUSE
            assert(discarded==fresh);
#else
            (void)discarded;(void)fresh;
#endif
            if(inline_high_water<1)inline_high_water=1;
            running=64/divisor;completed_report(&d,running);
        }
#ifdef GL_REPORT_REUSE
        assert(generated==inline_high_water+overflow_names);
#endif
    }
    /* Keep every pooled owner queued, then exercise a genuine fallback with
     * an overflow vector. All nine names must retire when that owner does. */
    d.pgraph.surface_scale_factor=1;
    for(unsigned i=0;i<1024;i++)pgraph_gl_get_report(&d,0x01010000u+i);
    unsigned before=generated,retired=deleted;
    for(unsigned i=0;i<9;i++){draw_query(&d,2,false);running+=128;}
    pgraph_gl_get_report(&d,0x01020000);
    pgraph_gl_drain_pending_reports(&d);
    assert(last_result==running&&generated-before==9&&deleted-retired==9&&live==2);
    /* Discard queued and active overflows, including a still-open interval.
     * Cached names unused by the last interval must also be destroyed. */
    for(unsigned i=0;i<19;i++)draw_query(&d,1,false);
    pgraph_gl_get_report(&d,0x01030000);
    for(unsigned i=0;i<19;i++)draw_query(&d,2,false);
    draw_query(&d,3,true);
    pgraph_gl_finalize_reports(&d.pgraph);all_names_released();
    pgraph_gl_init_reports(&d);draw_query(&d,1,false);running+=64;
    completed_report(&d,running);pgraph_gl_finalize_reports(&d.pgraph);all_names_released();
    glDeleteProgram(program);glDeleteShader(vs);glDeleteShader(fs);
    glDeleteVertexArrays(1,&vao);glDeleteRenderbuffers(1,&rb);glDeleteFramebuffers(1,&fb);
    printf("Real GL reports: %u exact report writes, %u native query lifetimes; fresh results, scale, open-query CLEAR, overflow, fallback, teardown and reinit PASS\n",writes,generated);
    return 0;
}
