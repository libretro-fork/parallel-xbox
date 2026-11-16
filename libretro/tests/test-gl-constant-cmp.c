/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* Appended after existing GL context/compile helpers by the Python driver. */
#define GL_INLINE_STREAM_SLICE_BYTES 16384
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


static uint64_t pgraph_vertex_fetch_span(unsigned n,size_t stride,size_t size){return n?(uint64_t)(n-1)*stride+size:0;}

/* DMA ownership and dirty upload effects are bounded models. */
static void *nv_dma_map(NV2AState *d,uint32_t object,hwaddr *length){assert(object<=1);*length=8192;return d->vram_ptr+object*8192;}
static void update_memory_buffer(NV2AState *d,hwaddr addr,hwaddr bytes,bool old){(void)old;assert(addr+bytes<=16384);glBindBuffer(GL_ARRAY_BUFFER,d->pgraph.gl_renderer_state->gl_memory_buffer);glBufferSubData(GL_ARRAY_BUFFER,addr,bytes,d->vram_ptr+addr);}
/* PRODUCTION */
static void decoded(uint32_t raw,float values[4])
{
 const unsigned shifts[]={0,11,22},widths[]={11,11,10};
 for(unsigned i=0;i<3;i++) {
  unsigned width=widths[i];int value=(raw>>shifts[i])&((1u<<width)-1);
  if(value&(1u<<(width-1)))value-=1u<<width;
  values[i]=(float)value/((1u<<(width-1))-1);
  if(values[i]<-1)values[i]=-1;
 }
 values[3]=1;
}
int main(void)
{
 init_gl();GLuint programs[16][2];
 for(unsigned lane=0;lane<16;lane++)for(unsigned integer=0;integer<2;integer++) {
  MString *s=mstring_from_str("#version 410\nout gl_PerVertex {vec4 gl_Position;float gl_PointSize;};\n");
  mstring_append_fmt(s,"layout(location=%u) in %s a;layout(location=%u) in vec4 b;out %s capture_value;out vec4 stamp;void main(){capture_value=%s;stamp=b;gl_Position=vec4(0,0,0,1);}",lane,integer?"int":"vec4",(lane+1)%16,integer?"ivec4":"vec4",integer?"ivec4(a,0,0,1)":"a");
  programs[lane][integer]=compile_program(s);mstring_unref(s);
 }
 GLuint vao,feedback,fb,rb;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,1,1);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 enum{SAMPLES=5120,DRAWS=16*SAMPLES*4,WORDS=DRAWS*8};
 uint32_t *expected=malloc(WORDS*4),*actual=malloc(WORDS*4);assert(expected&&actual);
 glGenBuffers(1,&feedback);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,WORDS*4,NULL,GL_STREAM_READ);
 uint8_t vram[16384]={0};PGRAPHGLState r={0};glGenBuffers(1,&r.gl_memory_buffer);glBindBuffer(GL_ARRAY_BUFFER,r.gl_memory_buffer);glBufferData(GL_ARRAY_BUFFER,sizeof(vram),vram,GL_STREAM_DRAW);
 NV2AState d={.pgraph={.gl_renderer_state=&r,.dma_vertex_b=1},.vram_ptr=vram};
 glEnable(GL_RASTERIZER_DISCARD);unsigned draws=0,constant=0,array=0;
 for(unsigned lane=0;lane<16;lane++) {
  memset(d.pgraph.vertex_attributes,0,sizeof(d.pgraph.vertex_attributes));
  for(unsigned i=0;i<16;i++)d.pgraph.vertex_attributes[i].inline_value[3]=1;
  VertexAttribute *normal=&d.pgraph.vertex_attributes[lane],*stamp=&d.pgraph.vertex_attributes[(lane+1)%16];
  *normal=(VertexAttribute){.format=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_CMP,.count=1,.size=4,.offset=32};
  *stamp=(VertexAttribute){.format=NV097_SET_VERTEX_DATA_ARRAY_FORMAT_TYPE_F,.count=4,.size=4,.stride=16,.offset=4096};
  for(unsigned sample=0;sample<SAMPLES;sample++) {
   unsigned shift=sample<2048?0:sample<4096?11:22,width=shift==22?10:11;
   uint32_t raw=0x155u|(0x2aau<<11)|(0x155u<<22),mask=((1u<<width)-1)<<shift;
   raw=(raw&~mask)|((sample&((1u<<width)-1))<<shift);
   normal->dma_select=sample&1;memcpy(vram+normal->dma_select*8192+normal->offset,&raw,4);
   float values[4];decoded(raw,values);
   for(unsigned mode=0;mode<4;mode++) {
    bool packed=mode==0||mode==3;normal->stride=packed?4:0;normal->inline_value[3]=13;
    float marker[]={lane,sample,mode,1};memcpy(vram+stamp->offset,marker,16);
    if(mode==2){int64_t k;assert(attr_displacement(&d,&d.pgraph,&r,0,&k)&&k==0);}
    pgraph_gl_bind_vertex_attributes(&d,0,0,false,0,0,mode>=2);
    if(d.pgraph.compressed_attrs!=(packed?(1u<<lane):0)||r.attr_shadow[lane].enabled!=packed||(!packed&&memcmp(normal->inline_value,values,16))) {
     fprintf(stderr,"CONSTANT_CMP_MISMATCH lane=%u sample=%u mode=%u key=%x expected-packed=%d\n",lane,sample,mode,d.pgraph.compressed_attrs,packed);return 1;
    }
    if(packed){uint32_t bits[]={raw,0,0,1};memcpy(expected+draws*8,bits,16);array++;}
    else{memcpy(expected+draws*8,values,16);constant++;}
    memcpy(expected+draws*8+4,marker,16);
    glUseProgram(programs[lane][packed]);glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback,(size_t)draws*32,32);
    glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,1);glEndTransformFeedback();assert(glGetError()==GL_NO_ERROR);draws++;
   }
  }
 }
 GLsync fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);GLenum status=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,10000000000ull);assert(status==GL_ALREADY_SIGNALED||status==GL_CONDITION_SATISFIED);glDeleteSync(fence);
 glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,WORDS*4,actual);assert(glGetError()==GL_NO_ERROR);
 for(unsigned i=0;i<WORDS;i++)if(actual[i]!=expected[i]){fprintf(stderr,"CONSTANT_CMP_MISMATCH scalar=%u expected=%08x got=%08x\n",i,expected[i],actual[i]);return 1;}
 assert(draws==DRAWS&&constant==array);
 printf("Constant CMP: %u draws, %u exact scalar bits; %u CPU-decoded constants/%u packed arrays, every lane and signed component, slow/basevertex paths and transitions PASS\n",draws,WORDS,constant,array);
 glUseProgram(0);for(unsigned i=0;i<16;i++)for(unsigned j=0;j<2;j++)glDeleteProgram(programs[i][j]);glDeleteBuffers(1,&feedback);glDeleteBuffers(1,&r.gl_memory_buffer);glDeleteVertexArrays(1,&vao);glDeleteRenderbuffers(1,&rb);glDeleteFramebuffers(1,&fb);free(expected);free(actual);return 0;
}
