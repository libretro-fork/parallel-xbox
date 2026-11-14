/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define GEOM_DYNAMIC_VARIANTS 22
#define GL_GEOMETRY_SHADER 0x8dd9
#define GL_LINK_STATUS 0x8b82
#define GL_FALSE 0
typedef unsigned GLuint;
typedef int GLint;
/* TYPES */
/* MAPPING */
typedef struct {bool free;} LruNode;
typedef struct {unsigned free_count;} Lru;
typedef struct {unsigned kind;struct {GeomState state;GenGeomGlslOptions glsl_opts;}geom;} ShaderModuleCacheKey;
typedef struct {LruNode node;ShaderModuleCacheKey key;GLuint gl_stage_program,gl_shader;} ShaderModuleCacheEntry;
typedef struct ShaderBinding {int ready_geom_variant;GLuint gl_prog_gs;struct{GeomState geom;unsigned psh;}state;} ShaderBinding;
typedef struct {
    Lru shader_module_cache;
    struct {
        ShaderModuleCacheEntry *stages;
        GLint control_locs[GEOM_DYNAMIC_VARIANTS];int controls[GEOM_DYNAMIC_VARIANTS][4];bool controls_valid[GEOM_DYNAMIC_VARIANTS];
        const ShaderBinding *uploaded_binding;bool ready;
        uint64_t covered_binds,control_uploads;
    }ready_geom;
} PGRAPHGLState;
typedef struct {char text[8];} MString;
static unsigned sources,allocations,created,deleted,alive,writes,ordinal,shader_deletes;
static int fail_stage=-1;
static struct {bool alive,linked,attached;int values[4];} programs[20000];
static PGRAPHGLState *current;
static ShaderModuleCacheEntry *ready_geom_find(PGRAPHGLState*,const ShaderModuleCacheKey*);
static MString *pgraph_glsl_gen_geom_dynamic(const GeomState *s,GenGeomGlslOptions o)
{
    assert(pgraph_glsl_geom_dynamic_variant(s,o)>=0);
    MString *m=malloc(sizeof(*m));assert(m);strcpy(m->text,"shader");sources++;allocations++;return m;
}
static const char *mstring_get_str(MString *m){assert(sources);return m->text;}
static void mstring_unref(MString *m){assert(sources);sources--;free(m);}
static GLuint pgraph_gl_separable_program(unsigned kind,const char *source,GLuint *shader)
{
    assert(kind==GL_GEOMETRY_SHADER && sources==1 && !strcmp(source,"shader"));
    ShaderModuleCacheKey key={.kind=GL_GEOMETRY_SHADER,.geom={.state={.primitive_mode=PRIM_TYPE_LINES}}};
    assert(!current->ready_geom.ready&&!ready_geom_find(current,&key));
    GLuint id=++created;assert(id<20000);programs[id].alive=programs[id].attached=true;
    programs[id].linked=(int)ordinal!=fail_stage;ordinal++;alive++;*shader=id;return id;
}
static void glDetachShader(GLuint p,GLuint shader){assert(p==shader&&programs[p].alive&&programs[p].linked&&programs[p].attached);programs[p].attached=false;}
static void glDeleteShader(GLuint shader){assert(shader&&programs[shader].alive);shader_deletes++;}
static void glGetProgramiv(GLuint p,unsigned which,GLint *value)
{assert(programs[p].alive&&which==GL_LINK_STATUS);*value=programs[p].linked;}
static void delete_program(PGRAPHGLState *r,GLuint p)
{assert(r==current&&programs[p].alive);programs[p].alive=programs[p].attached=false;alive--;deleted++;}
static void module_resolve_uniform_locs(ShaderModuleCacheEntry *s)
{assert(s->key.kind==GL_GEOMETRY_SHADER&&programs[s->gl_stage_program].linked);}
static GLint glGetUniformLocation(GLuint p,const char *name)
{assert(programs[p].alive&&!strcmp(name,"gsControl"));return p%GEOM_DYNAMIC_VARIANTS==2?-1:(int)p;}
static void glProgramUniform4iv(GLuint p,GLint loc,int count,const int *values)
{assert(programs[p].alive&&loc==(int)p&&count==1);memcpy(programs[p].values,values,16);writes++;}
static void lru_add_free(Lru *l,LruNode *n)
{assert(!n->free&&l->free_count<128);n->free=true;l->free_count++;}
static unsigned pgraph_glsl_ui_clamp_stage(const unsigned *stage){return *stage;}
/* PRODUCTION */
static ShaderModuleCacheEntry pool[128];
static void setup(PGRAPHGLState *r)
{
    memset(r,0,sizeof(*r));memset(pool,0,sizeof(pool));ordinal=0;current=r;
    r->ready_geom.stages=pool;
    for(unsigned i=GEOM_DYNAMIC_VARIANTS;i<128;i++)lru_add_free(&r->shader_module_cache,&pool[i].node);
}
static ShaderBinding binding(PGRAPHGLState *r,unsigned variant)
{
    ShaderBinding b={.ready_geom_variant=variant,.gl_prog_gs=r->ready_geom.stages[variant].gl_stage_program};
    GenGeomGlslOptions opts;pgraph_glsl_geom_dynamic_state(variant,&b.state.geom,&opts);return b;
}
int main(void)
{
    PGRAPHGLState r;
    setup(&r);ready_geom_init(&r);assert(r.ready_geom.ready&&alive==GEOM_DYNAMIC_VARIANTS&&sources==0);
    assert(r.shader_module_cache.free_count==128-GEOM_DYNAMIC_VARIANTS);
    for(unsigned i=0;i<GEOM_DYNAMIC_VARIANTS;i++){
        ShaderModuleCacheKey key={.kind=GL_GEOMETRY_SHADER};
        pgraph_glsl_geom_dynamic_state(i,&key.geom.state,&key.geom.glsl_opts);
        assert(ready_geom_find(&r,&key)==&pool[i]);
        assert(!pool[i].gl_shader&&!programs[pool[i].gl_stage_program].attached);
        key.kind=1;assert(!ready_geom_find(&r,&key));key.kind=GL_GEOMETRY_SHADER;
        key.geom.glsl_opts.vulkan=true;assert(!ready_geom_find(&r,&key));
    }
    ShaderBinding a=binding(&r,4),b=a,unused=binding(&r,1);
    unsigned before=allocations;
    ready_geom_upload(&r,&a);assert(writes==1);
    int controls[4];pgraph_glsl_geom_dynamic_controls(&a.state.geom,a.state.psh,controls);
    assert(!memcmp(programs[a.gl_prog_gs].values,controls,16));
    for(unsigned i=0;i<10000;i++)ready_geom_upload(&r,&a);
    assert(writes==1&&allocations==before);
    ready_geom_upload(&r,&b);assert(writes==1&&r.ready_geom.uploaded_binding==&b);
    ready_geom_invalidate(&r,&a);assert(r.ready_geom.uploaded_binding==&b);
    ready_geom_invalidate(&r,&b);assert(!r.ready_geom.uploaded_binding);
    b.state.psh=3;b.state.geom.z_perspective=true;b.state.geom.first_vertex_is_provoking=true;
    ready_geom_upload(&r,&b);assert(writes==2);
    pgraph_glsl_geom_dynamic_controls(&b.state.geom,b.state.psh,controls);
    assert(!memcmp(programs[b.gl_prog_gs].values,controls,16));
    ready_geom_upload(&r,&unused);assert(writes==2);
    /* Quads and quad strips borrow the same executable, but switching the
     * strip bit must update its controls even with identical texture state. */
    ShaderBinding quad=binding(&r,10),strip=quad;
    assert(quad.state.geom.primitive_mode==PRIM_TYPE_QUADS);
    strip.state.geom.primitive_mode=PRIM_TYPE_QUAD_STRIP;
    ShaderModuleCacheKey strip_key={.kind=GL_GEOMETRY_SHADER,
        .geom={.state=strip.state.geom,.glsl_opts={.locations=true}}};
    assert(ready_geom_find(&r,&strip_key)==&pool[10]);
    ready_geom_upload(&r,&quad);unsigned quad_writes=writes;
    ready_geom_upload(&r,&strip);assert(writes==quad_writes+1);
    assert(programs[strip.gl_prog_gs].values[0]==16);
    ready_geom_upload(&r,&quad);assert(writes==quad_writes+2);
    assert(programs[quad.gl_prog_gs].values[0]==0);
    assert(allocations==before);
    ready_geom_finalize(&r);assert(!r.ready_geom.ready&&!r.ready_geom.stages&&alive==0);
    ready_geom_finalize(&r);assert(alive==0);
    /* Every partial startup returns its reservation exactly once, restoring
     * all 128 original slots and the existing correct specialized path. */
    for(unsigned failure=0;failure<GEOM_DYNAMIC_VARIANTS;failure++){
        setup(&r);fail_stage=failure;ready_geom_init(&r);
        assert(!r.ready_geom.ready&&!r.ready_geom.stages&&!alive&&!sources);
        assert(r.shader_module_cache.free_count==128);
        for(unsigned j=0;j<128;j++)assert(pool[j].node.free);
        ready_geom_finalize(&r);
    }
    assert(created==deleted&&created==shader_deletes);
    printf("PASS: %u native geometry owners, fixed pool capacity, all partial-init failures, bounded uniform transport, strip switching, invalidation and teardown\n",GEOM_DYNAMIC_VARIANTS);
    return 0;
}
