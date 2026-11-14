/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native API boundaries are mocked; the production owner/transport is included. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef assert
#define assert(x) do { if (!(x)) { fprintf(stderr,"ready owner:%d: %s\n",__LINE__,#x); _Exit(1); } } while (0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef unsigned GLuint;
typedef int GLint;
enum { GL_VERTEX_SHADER=1, GL_FALSE=0, GL_LINK_STATUS=2,
       GL_UNIFORM_BUFFER=3, GL_UNIFORM_BUFFER_BINDING=4, GL_STREAM_DRAW=5,
       NV2A_MAX_TRANSFORM_PROGRAM_LENGTH=136,
       VSH_INTERPRETER_FLAGS_OFFSET=136*16, VSH_INTERPRETER_BUFFER_SIZE=136*16+16 };
#define GL_INVALID_INDEX (~0u)
typedef struct {
    bool is_fixed_function, const_attrs_dynamic, smooth_shading;
    unsigned compressed_attrs, uniform_attrs, swizzle_attrs;
    uint32_t flags;
    struct { int program_length; uint32_t program_data[136][4]; } programmable;
} VshState;
typedef struct { bool locations, prefix_outputs; } GenVshGlslOptions;
typedef struct { unsigned kind; struct { VshState state;
    GenVshGlslOptions glsl_opts; } vsh; } ShaderModuleCacheKey;
typedef struct { ShaderModuleCacheKey key; GLuint gl_stage_program; } ShaderModuleCacheEntry;
typedef struct { GLuint gl_prog_vs; struct { VshState vsh; } state; } ShaderBinding;
typedef struct { struct { ShaderModuleCacheEntry stage[4]; GLuint code_buffer;
    const ShaderBinding *uploaded_binding; bool ready;
    uint64_t covered_binds, code_uploads; } ready_vsh; } PGRAPHGLState;
typedef struct { char text[8]; } MString;
static bool sso=true, alive[8], buffer_alive, missing_block;
static unsigned creates, deletes, uniform_resolves, writes, owners, fail_at;
static GLint bound=77;
static uint8_t storage[VSH_INTERPRETER_BUFFER_SIZE+16];
typedef struct { bool spirv_1_6, push_constants, preserve_float32; unsigned ubo_binding; } SpvVshOptions;
static bool validation_ok=true;
static inline bool pgraph_spv_vsh_supported(const VshState *s,SpvVshOptions options)
{ assert(s&&options.preserve_float32&&!options.ubo_binding);return validation_ok; }
static bool gl_sso_enabled(void) { return sso; }
static MString *pgraph_glsl_gen_vsh_interpreter_dynamic(const VshState *s, GenVshGlslOptions o)
{ assert(o.locations); MString *m=malloc(sizeof(*m));assert(m);owners++;
  snprintf(m->text,sizeof(m->text),"%u",(s->smooth_shading?2:0)|o.prefix_outputs);return m; }
static const char *mstring_get_str(MString *m) { return m->text; }
static void mstring_unref(MString *m) { assert(owners);owners--;free(m); }
static GLuint pgraph_gl_separable_program(unsigned kind,const char *text,GLuint *shader)
{ assert(kind==GL_VERTEX_SHADER&&creates<4&&strtoul(text,NULL,10)==creates);
  GLuint p=++creates;alive[p]=true;*shader=p+10;return p; }
static void glDeleteShader(GLuint s) { assert(s>10&&s<=14); }
static void glGetProgramiv(GLuint p,unsigned name,GLint *out)
{ assert(alive[p]&&name==GL_LINK_STATUS);*out=p!=fail_at; }
static void glGetProgramInfoLog(GLuint p,size_t n,void *length,char *out)
{ assert(p==fail_at&&n>8&&!length);strcpy(out,"fixture"); }
static void delete_program(PGRAPHGLState *r,GLuint p)
{ (void)r;assert(alive[p]);alive[p]=false;deletes++; }
static GLuint glGetUniformBlockIndex(GLuint p,const char *name)
{ assert(alive[p]&&!strcmp(name,"XvsCode"));return missing_block?GL_INVALID_INDEX:9; }
static void glUniformBlockBinding(GLuint p,GLuint block,GLuint binding)
{ assert(alive[p]&&block==9&&!binding); }
static void module_resolve_uniform_locs(ShaderModuleCacheEntry *s)
{ assert(s->key.kind==GL_VERTEX_SHADER&&alive[s->gl_stage_program]);uniform_resolves++; }
static void glGetIntegerv(unsigned name,GLint *value)
{ assert(name==GL_UNIFORM_BUFFER_BINDING);*value=bound; }
static void glGenBuffers(unsigned n,GLuint *value)
{ assert(n==1&&!buffer_alive);*value=90;buffer_alive=true; }
static void glBindBuffer(unsigned target,GLuint value)
{ assert(target==GL_UNIFORM_BUFFER);bound=value; }
static void glBufferData(unsigned target,size_t size,const void *ptr,unsigned usage)
{ assert(target==GL_UNIFORM_BUFFER&&bound==90&&size==VSH_INTERPRETER_BUFFER_SIZE&&!ptr&&usage==GL_STREAM_DRAW);
  memset(storage,0xa5,sizeof(storage)); }
static void glBindBufferBase(unsigned target,unsigned binding,GLuint buffer)
{ assert(target==GL_UNIFORM_BUFFER&&!binding&&buffer==90&&buffer_alive); }
static void glBufferSubData(unsigned target,size_t offset,size_t size,const void *ptr)
{ assert(target==GL_UNIFORM_BUFFER&&bound==90&&buffer_alive&&offset+size<=VSH_INTERPRETER_BUFFER_SIZE);
  memcpy(storage+8+offset,ptr,size);writes++; }
static void glDeleteBuffers(unsigned n,const GLuint *buffer)
{ assert(n==1&&*buffer==90&&buffer_alive);buffer_alive=false; }
static uint32_t pgraph_glsl_vsh_interpreter_flags(const VshState *s) { return s->flags; }
/* PRODUCTION */
int main(void)
{
    PGRAPHGLState r={0};sso=false;ready_vsh_init(&r);assert(!creates&&!r.ready_vsh.ready);
    sso=true;ready_vsh_init(&r);assert(creates==4&&uniform_resolves==4&&!owners&&bound==77&&r.ready_vsh.ready);
    ShaderModuleCacheKey k={.kind=GL_VERTEX_SHADER};k.vsh.state.programmable.program_length=1;
    for(unsigned smooth=0;smooth<2;smooth++)for(unsigned prefix=0;prefix<2;prefix++) {
        k.vsh.state.smooth_shading=smooth;k.vsh.glsl_opts.prefix_outputs=prefix;
        assert(ready_vsh_find(&r,&k)==&r.ready_vsh.stage[smooth*2+prefix]);
    }
    validation_ok=false;assert(!ready_vsh_find(&r,&k));validation_ok=true;
    for(unsigned mask=1;mask<65536;mask++) {
        k.vsh.state.uniform_attrs=mask;assert(!ready_vsh_find(&r,&k));k.vsh.state.uniform_attrs=0;
        k.vsh.state.compressed_attrs=mask;assert(!ready_vsh_find(&r,&k));k.vsh.state.compressed_attrs=0;
        k.vsh.state.swizzle_attrs=mask;assert(!ready_vsh_find(&r,&k));k.vsh.state.swizzle_attrs=0;
    }
    k.vsh.state.is_fixed_function=true;assert(!ready_vsh_find(&r,&k));k.vsh.state.is_fixed_function=false;
    k.vsh.state.const_attrs_dynamic=true;assert(!ready_vsh_find(&r,&k));k.vsh.state.const_attrs_dynamic=false;
    k.vsh.state.programmable.program_length=0;assert(!ready_vsh_find(&r,&k));
    k.vsh.state.programmable.program_length=137;assert(!ready_vsh_find(&r,&k));
    ShaderBinding b={.gl_prog_vs=r.ready_vsh.stage[0].gl_stage_program};
    b.state.vsh.flags=0x1234;
    for(unsigned i=0;i<136;i++)for(unsigned j=0;j<4;j++)b.state.vsh.programmable.program_data[i][j]=i*4+j;
    ready_vsh_upload(&r,&b);assert(writes==2&&!memcmp(storage+8,b.state.vsh.programmable.program_data,2176));
    uint32_t flags;memcpy(&flags,storage+8+2176,4);assert(flags==0x1234);
    ready_vsh_upload(&r,&b);assert(writes==2); /* Same immutable owner. */
    ShaderBinding same=b;
    ready_vsh_upload(&r,&same);assert(writes==2&&r.ready_vsh.uploaded_binding==&same);
    ready_vsh_invalidate(&r,&b);assert(r.ready_vsh.uploaded_binding==&same);
    ready_vsh_upload(&r,&b);assert(writes==2&&r.ready_vsh.uploaded_binding==&b);
    ready_vsh_invalidate(&r,&b);b.state.vsh.flags=0x5678;b.state.vsh.programmable.program_data[0][0]=123456;
    ready_vsh_upload(&r,&b);assert(writes==4&&!memcmp(storage+8,b.state.vsh.programmable.program_data,2176));
    memcpy(&flags,storage+8+2176,4);assert(flags==0x5678); /* Same address, new generation. */
    for(unsigned i=0;i<8;i++)assert(storage[i]==0xa5&&storage[sizeof(storage)-1-i]==0xa5);
    ShaderBinding controls=b;controls.state.vsh.flags=0x8765;
    ready_vsh_upload(&r,&controls);assert(writes==5);
    assert(!memcmp(storage+8,b.state.vsh.programmable.program_data,2176));
    memcpy(&flags,storage+8+2176,4);assert(flags==0x8765);
    b.gl_prog_vs=99;ready_vsh_upload(&r,&b);assert(writes==5&&creates==4&&!owners);
    ready_vsh_finalize(&r);assert(deletes==4&&!buffer_alive&&!r.ready_vsh.ready&&!r.ready_vsh.uploaded_binding);
    creates=deletes=uniform_resolves=0;fail_at=2;ready_vsh_init(&r);
    assert(creates==2&&deletes==2&&!owners&&!buffer_alive&&!r.ready_vsh.ready);
    ready_vsh_finalize(&r);assert(deletes==2);
    creates=deletes=uniform_resolves=0;fail_at=0;missing_block=true;ready_vsh_init(&r);
    assert(creates==1&&deletes==1&&!buffer_alive&&!owners&&!r.ready_vsh.ready);
    ready_vsh_finalize(&r);assert(deletes==1);
    puts("PASS: finite native stage owners, eligibility, bounded direct uploads, cache-node reuse and partial-init teardown");
}
