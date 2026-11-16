/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "hw/xbox/nv2a/pgraph/gl/inline-stream.h"
#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL stream owner line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef unsigned GLuint,GLenum,GLbitfield;
typedef int GLint;
typedef intptr_t GLsizeiptr;
enum { GL_ARRAY_BUFFER=1,GL_ARRAY_BUFFER_BINDING=2,GL_STREAM_DRAW=3,GL_MAP_WRITE_BIT=4,GL_MAP_PERSISTENT_BIT=8,GL_MAP_COHERENT_BIT=16,GL_NO_ERROR=0 };
#define NV2A_VERTEXSHADER_ATTRIBUTES 16
#define NV2A_DPRINTF(...) ((void)0)
#define nv2a_profile_inc_counter(...) ((void)0)
typedef struct PGRAPHGLState {
    GLuint gl_inline_array_buffer;
    struct { uint64_t completed; } sampled_texture_pool;
    /* FIELDS */
} PGRAPHGLState;
typedef struct VertexAttribute { unsigned count,inline_array_offset; } VertexAttribute;
typedef struct PGRAPHState {
    PGRAPHGLState *gl_renderer_state;
    VertexAttribute vertex_attributes[16];
    unsigned inline_array_length;
    unsigned char data[GL_INLINE_STREAM_SLICE_BYTES+512];
} PGRAPHState;
typedef struct NV2AState { PGRAPHState pgraph; } NV2AState;
static unsigned pgraph_inline_attr_size(VertexAttribute *a){return a->count*4;}
static void *pgraph_inline_array_data(PGRAPHState *pg){return pg->data;}
static bool capability=true,alive,mapped,storage;
static unsigned fail,generated,deleted,maps,unmaps,orphans,uploads,bindings;
static GLenum error_code;
static GLuint bound=77;
static unsigned char gpu[GL_INLINE_STREAM_BYTES+32],fallback[GL_INLINE_STREAM_SLICE_BYTES+512];
static bool glo_check_extension(const char *s){assert(!strcmp(s,"GL_ARB_buffer_storage"));return capability;}
static void glGetIntegerv(GLenum target,GLint *value){assert(target==GL_ARRAY_BUFFER_BINDING);*value=bound;}
static GLenum glGetError(void){GLenum result=error_code;error_code=0;return result;}
static void glGenBuffers(unsigned n,GLuint *value){assert(n==1&&!alive);generated++;*value=fail==1?0:66;alive=*value!=0;if(fail==1||fail==2)error_code=99;}
static void glBindBuffer(GLenum target,GLuint value){assert(target==GL_ARRAY_BUFFER);assert(value==0||value==9||value==77||(value==66&&alive));bound=value;}
static void fake_storage(GLenum target,GLsizeiptr size,const void *data,GLbitfield flags)
{
    assert(target==GL_ARRAY_BUFFER&&bound==66&&alive&&!storage&&!data&&size==GL_INLINE_STREAM_BYTES);
    assert(flags==(GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT));
    if(fail==3){error_code=99;return;}storage=true;
}
static void (*glBufferStorage)(GLenum,GLsizeiptr,const void*,GLbitfield)=fake_storage;
static void *glMapBufferRange(GLenum target,GLsizeiptr offset,GLsizeiptr size,GLbitfield flags)
{
    assert(target==GL_ARRAY_BUFFER&&bound==66&&storage&&offset==0&&size==GL_INLINE_STREAM_BYTES);
    assert(flags==(GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT));maps++;
    if(fail==4||fail==5)error_code=99;
    if(fail==4)return NULL;
    mapped=true;return gpu+16;
}
static bool glUnmapBuffer(GLenum target){assert(target==GL_ARRAY_BUFFER&&bound==66&&mapped);mapped=false;unmaps++;return true;}
static void glDeleteBuffers(unsigned n,const GLuint *value){assert(n==1&&*value==66&&alive&&!mapped);alive=storage=false;deleted++;if(bound==66)bound=0;}
static void glBufferData(GLenum target,GLsizeiptr size,const void *data,GLenum usage){assert(target==GL_ARRAY_BUFFER&&bound==9&&!data&&usage==GL_STREAM_DRAW&&size<=(GLsizeiptr)sizeof(fallback));memset(fallback,0xcc,size);orphans++;}
static void glBufferSubData(GLenum target,GLsizeiptr offset,GLsizeiptr size,const void *data){assert(target==GL_ARRAY_BUFFER&&bound==9&&!offset&&size<=(GLsizeiptr)sizeof(fallback));memcpy(fallback,data,size);uploads++;}
static void pgraph_gl_bind_vertex_attributes(NV2AState *d,unsigned first,unsigned last,bool inlined,unsigned stride,unsigned provoking,bool basevertex)
{
    PGRAPHState *pg=&d->pgraph;PGRAPHGLState *r=pg->gl_renderer_state;
    assert(!first&&inlined&&stride==32&&provoking==last&&!basevertex);
    assert(pg->vertex_attributes[0].inline_array_offset==0&&pg->vertex_attributes[1].inline_array_offset==16);
    size_t bytes=(last+1)*stride;
    if(r->inline_stream.draw_buffer==66){assert(alive&&mapped);assert(!memcmp(gpu+16+r->inline_stream.draw_offset,pg->data,bytes));}
    else{assert(r->inline_stream.draw_buffer==9&&!r->inline_stream.draw_offset);assert(!memcmp(fallback,pg->data,bytes));}
    bindings++;
}
/* PRODUCTION */
static void reset(PGRAPHGLState *r)
{
    assert(!alive&&!mapped);memset(r,0,sizeof(*r));r->gl_inline_array_buffer=9;
    fail=generated=deleted=maps=unmaps=orphans=uploads=bindings=0;bound=77;error_code=0;
    capability=true;glBufferStorage=fake_storage;memset(gpu,0xa5,sizeof(gpu));
}
int main(void)
{
    PGRAPHGLState r;reset(&r);
    init_inline_stream(&r);assert(alive&&mapped&&generated==1&&maps==1&&bound==77);
    for(unsigned i=0;i<GL_INLINE_STREAM_BYTES;i++)assert(gpu[16+i]==0);
    NV2AState d={.pgraph={.gl_renderer_state=&r,.vertex_attributes={{.count=4},{.count=4}},.inline_array_length=32}};
    for(unsigned repeat=0;repeat<50;repeat++) {
        memset(d.pgraph.data,repeat,128);
        assert(pgraph_gl_bind_inline_array(&d)==4&&r.inline_stream.draw_buffer==66);
        assert(r.inline_stream.draw_offset==repeat*128&&!orphans&&!uploads);
    }
    r.inline_stream.cursor.used=GL_INLINE_STREAM_SLICE_BYTES-64;
    size_t before=r.inline_stream.cursor.used;
    assert(pgraph_gl_bind_inline_array(&d)==4&&r.inline_stream.draw_buffer==9&&orphans==1&&uploads==1);
    assert(r.inline_stream.cursor.used==before);
    gl_inline_stream_seal(&r.inline_stream.cursor,1);
    assert(pgraph_gl_bind_inline_array(&d)==4&&r.inline_stream.draw_buffer==66&&r.inline_stream.draw_offset==GL_INLINE_STREAM_SLICE_BYTES);
    d.pgraph.inline_array_length=(GL_INLINE_STREAM_SLICE_BYTES+32)/4;
    assert(pgraph_gl_bind_inline_array(&d)==8193&&r.inline_stream.draw_buffer==9&&orphans==2&&uploads==2);
    for(unsigned i=0;i<16;i++)assert(gpu[i]==0xa5&&gpu[sizeof(gpu)-1-i]==0xa5);
    bound=66;finalize_inline_stream(&r);assert(!alive&&!mapped&&deleted==1&&unmaps==1&&bound==0&&!r.inline_stream.mapping);
    finalize_inline_stream(&r);assert(deleted==1);
    reset(&r);capability=false;init_inline_stream(&r);assert(!generated&&!alive);
    reset(&r);glBufferStorage=NULL;init_inline_stream(&r);assert(!generated&&!alive);
    for(fail=1;fail<=5;fail++) {
        unsigned current=fail;reset(&r);fail=current;
        init_inline_stream(&r);assert(!alive&&!mapped&&!r.inline_stream.buffer&&!r.inline_stream.mapping&&bound==77&&!error_code);
        assert(deleted==(fail!=1));assert(unmaps==(fail==5));
        d.pgraph.inline_array_length=32;assert(pgraph_gl_bind_inline_array(&d)==4&&orphans==1&&uploads==1);
        finalize_inline_stream(&r);assert(deleted==(fail!=1));
    }
    puts("Mapped stream owner: coherent final-store writes, exact published ranges, 5 partial-init failures, no-wait pressure/oversize fallbacks, inactive capability and idempotent teardown PASS");
    return 0;
}
