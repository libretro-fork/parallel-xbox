/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Appended after existing GL context/compile helpers by the Python driver. */
#include "hw/xbox/nv2a/pgraph/gl/inline-stream.h"
#define NV2A_DPRINTF(...) ((void)0)
#define NV2A_GL_DGROUP_BEGIN(...) ((void)0)
#define NV2A_GL_DGROUP_END() ((void)0)
#define nv2a_profile_inc_counter(...) ((void)0)
#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif
typedef uint64_t hwaddr;
typedef struct VertexAttribute {
    unsigned count,size,format,stride,inline_array_offset;
    unsigned dma_select,offset;
    float inline_value[4];
} VertexAttribute;
typedef struct PGRAPHGLState {
    GLuint gl_inline_array_buffer,gl_memory_buffer;
    GLint draw_basevertex;
    struct {uint64_t completed;} sampled_texture_pool;
    /* FIELDS */
} PGRAPHGLState;
struct PGRAPHState {
    PGRAPHGLState *gl_renderer_state;
    VertexAttribute vertex_attributes[16];
    unsigned inline_array_length,compressed_attrs;
    uint32_t dma_vertex_a,dma_vertex_b;
    uint8_t data[GL_INLINE_STREAM_SLICE_BYTES+1024];
};
typedef struct NV2AState {PGRAPHState pgraph;uint8_t *vram_ptr;} NV2AState;
static unsigned pgraph_inline_attr_size(const VertexAttribute *a){return (a->size*a->count+3)&~3u;}
static void *pgraph_inline_array_data(PGRAPHState *pg){return pg->data;}
static void *nv_dma_map(NV2AState *d,uint32_t object,hwaddr *length){(void)d;(void)object;(void)length;abort();}
static void update_memory_buffer(NV2AState *d,hwaddr addr,hwaddr bytes,bool old){(void)d;(void)addr;(void)bytes;(void)old;abort();}
static uint64_t pgraph_vertex_fetch_span(unsigned n,size_t stride,size_t size){return n?(uint64_t)(n-1)*stride+size:0;}
static bool attr_displacement(NV2AState *d,PGRAPHState *pg,PGRAPHGLState *r,unsigned first,int64_t *k){(void)d;(void)pg;(void)r;(void)first;(void)k;abort();}
bool glo_check_extension(const char *name)
{
    GLint count;glGetIntegerv(GL_NUM_EXTENSIONS,&count);
    for(GLint i=0;i<count;i++)if(!strcmp((const char*)glGetStringi(GL_EXTENSIONS,i),name))return true;
    return false;
}
/* PRODUCTION */
static void wait_gpu(void)
{
    GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    GLenum result=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,10000000000ull);
    assert(result==GL_ALREADY_SIGNALED||result==GL_CONDITION_SATISFIED);glDeleteSync(fence);
}
static size_t make_data(PGRAPHState *pg,unsigned format,unsigned count,unsigned vertices,unsigned tag,uint32_t *expected)
{
    VertexAttribute *a=&pg->vertex_attributes[0],*b=&pg->vertex_attributes[1];
    *a=(VertexAttribute){.format=format,.count=count,.size=format==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F||format==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP?4:format==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1||format==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K?2:1};
    *b=(VertexAttribute){.format=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,.count=4,.size=4};
    unsigned first=pgraph_inline_attr_size(a),stride=first+16;
    assert(vertices*stride<=sizeof(pg->data));memset(pg->data,0xa5,vertices*stride);
    for(unsigned v=0;v<vertices;v++) {
        float values[4]={0,0,0,1};uint8_t *data=pg->data+v*stride;
        if(format==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP) {
            uint32_t raw=0x80000400u^(v*0x12345u);memcpy(data,&raw,4);
            uint32_t result[4]={raw,0,0,1};memcpy(expected+v*8,result,16);
        } else {
            for(unsigned k=0;k<count;k++) {
                if(a->size==1){data[k]=(v+k+tag)%2?255:0;values[k]=data[k]?1:0;}
                else if(a->size==2){int16_t raw=(int16_t)((v+k+tag)%3==0?-32768:(v+k+tag)%3==1?0:32767);memcpy(data+k*2,&raw,2);values[k]=format==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1?(raw<0?-1:raw>0?1:0):raw;}
                else{values[k]=(float)((int)(v%31)*4+(int)k-(int)(tag%7));memcpy(data+k*4,&values[k],4);}
            }
            if(format==NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D){float swap=values[0];values[0]=values[2];values[2]=swap;}
            memcpy(expected+v*8,values,16);
        }
        float marker[4]={(float)v,(float)tag,(float)count,1};memcpy(data+first,marker,16);memcpy(expected+v*8+4,marker,16);
    }
    pg->inline_array_length=vertices*stride/4;return vertices*32;
}
int main(void)
{
    init_gl();
    GLuint programs[2];
    const char *sources[]={
        "#version 410\nout gl_PerVertex {vec4 gl_Position;float gl_PointSize;};\nlayout(location=0) in vec4 a;layout(location=1) in vec4 b;out vec4 capture_value;out vec4 stamp;void main(){capture_value=a;stamp=b;gl_Position=vec4(0,0,0,1);}",
        "#version 410\nout gl_PerVertex {vec4 gl_Position;float gl_PointSize;};\nlayout(location=0) in int a;layout(location=1) in vec4 b;out ivec4 capture_value;out vec4 stamp;void main(){capture_value=ivec4(a,0,0,1);stamp=b;gl_Position=vec4(0,0,0,1);}"};
    for(unsigned i=0;i<2;i++){MString *s=mstring_from_str(sources[i]);programs[i]=compile_program(s);mstring_unref(s);}
    GLuint vao,feedback,framebuffer,renderbuffer;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glGenRenderbuffers(1,&renderbuffer);glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,1,1);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,renderbuffer);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    enum {CAPACITY=4*1024*1024};uint32_t *expected=malloc(CAPACITY),*actual=malloc(CAPACITY);assert(expected&&actual);
    glGenBuffers(1,&feedback);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,CAPACITY,NULL,GL_STREAM_READ);
    PGRAPHGLState r={0};glGenBuffers(1,&r.gl_inline_array_buffer);init_inline_stream(&r);assert(r.inline_stream.mapping&&r.inline_stream.buffer);
    NV2AState d={.pgraph={.gl_renderer_state=&r}};for(unsigned i=0;i<16;i++)d.pgraph.vertex_attributes[i].inline_value[3]=1;
    const unsigned formats[]={NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S1,NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_S32K,NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_OGL,NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_UB_D3D,NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP};
    glEnable(GL_RASTERIZER_DISCARD);size_t used=0;unsigned draws=0,streamed=0,fallbacks=0;
    for(unsigned frame=1;frame<=72;frame++) {
        if(frame%4==3)r.inline_stream.cursor.used=GL_INLINE_STREAM_SLICE_BYTES-32;
        for(unsigned f=0;f<6;f++)for(unsigned count=1;count<=4;count++) {
            if(f==4&&count!=4)continue;
            if(f==5&&count!=1)continue;
            for(unsigned old=0;old<2;old++) {
                unsigned vertices=frame==72&&f==0&&count==4?8193:19;
                size_t bytes=make_data(&d.pgraph,formats[f],count,vertices,draws,expected+used/4);assert(bytes<=CAPACITY-used);
                bool disabled=r.inline_stream.cursor.disabled;r.inline_stream.cursor.disabled=old;
                assert(pgraph_gl_bind_inline_array(&d)==vertices);r.inline_stream.cursor.disabled=disabled;
                streamed+=r.inline_stream.draw_buffer==r.inline_stream.buffer;fallbacks+=r.inline_stream.draw_buffer==r.gl_inline_array_buffer;
                glUseProgram(programs[f==5]);glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback,used,bytes);
                glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,vertices);glEndTransformFeedback();
                assert(glGetError()==GL_NO_ERROR);used+=bytes;draws++;
            }
        }
        gl_inline_stream_seal(&r.inline_stream.cursor,frame);
        if(frame%6==0){wait_gpu();r.sampled_texture_pool.completed=frame;}
    }
    wait_gpu();glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,used,actual);assert(glGetError()==GL_NO_ERROR);
    for(size_t i=0;i<used/4;i++)if(actual[i]!=expected[i]){fprintf(stderr,"STREAM_GPU_MISMATCH scalar=%zu expected=%08x actual=%08x\n",i,expected[i],actual[i]);return 1;}
    assert(draws==2592&&streamed>400&&fallbacks>1000&&streamed+fallbacks==draws);
    printf("Inline stream GPU: %u draws, %zu exact scalar bits, %u mapped/%u fallback; 6 formats, delayed retirement, pressure, oversize and repeated wraps PASS\n",draws,used/4,streamed,fallbacks);
    finalize_inline_stream(&r);glDeleteBuffers(1,&r.gl_inline_array_buffer);glDeleteBuffers(1,&feedback);glUseProgram(0);for(unsigned i=0;i<2;i++)glDeleteProgram(programs[i]);glDeleteVertexArrays(1,&vao);glDeleteRenderbuffers(1,&renderbuffer);glDeleteFramebuffers(1,&framebuffer);free(expected);free(actual);return 0;
}
