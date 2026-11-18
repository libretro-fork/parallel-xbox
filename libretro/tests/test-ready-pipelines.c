/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do { if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(86);} }while(0)
#define GL_PIPELINE_OWNER_BUCKETS 1
#define GL_VERTEX_SHADER_BIT 1
#define GL_GEOMETRY_SHADER_BIT 2
#define GL_FRAGMENT_SHADER_BIT 4
#define GL_VALIDATE_STATUS 5
#define GL_FALSE 0

#define GL_NO_ERROR 0
static unsigned startup_error_flags;
static bool startup_publication_error;
static __attribute__((unused)) void startup_raise_error(unsigned error){
#ifdef FIXTURE_CHECKED_STARTUP
 assert(error>=0x500&&error<=0x507);startup_error_flags|=1u<<(error-0x500);
#else
 (void)error;
#endif
}
static __attribute__((unused)) unsigned glGetError(void){
 if(!startup_error_flags)return 0;
 unsigned bit=(unsigned)__builtin_ctz(startup_error_flags);startup_error_flags&=~(1u<<bit);return 0x500+bit;
}

typedef unsigned GLuint;
typedef int GLint;
typedef struct { unsigned unused; } LruNode;
typedef struct ShaderBinding {
    LruNode node;
    GLuint gl_pipeline,gl_prog_vs,gl_prog_gs,gl_prog_fs;
    struct ShaderBinding *pipeline_next,*pipeline_prev,*pipeline_hash_next;
} ShaderBinding;
typedef struct { GLuint gl_stage_program; } Stage;
typedef struct {
    ShaderBinding *pipeline_owners[1],*ready_pipelines;
    struct { bool ready; Stage stage[4]; } ready_vsh;
    struct { bool ready; Stage *stages; } ready_geom,ready_psh;
    unsigned shader_cache;
} PGRAPHGLState;
static ShaderBinding slots[18],borrowers[18];
static Stage gs[16],fs[2];
static unsigned created,deleted,returned,fail_create,fail_validate;
static bool alive[19],validated[19];
static GLuint programs[19][3];
static void glGenProgramPipelines(int n,GLuint *name)
{
    assert(n==1);unsigned i=++created;assert(i<=18);
    *name=i==fail_create?0:i;if(*name)alive[i]=true;else startup_raise_error(0x505);
}
static void glDeleteProgramPipelines(int n,const GLuint *name)
{
    assert(n==1&&*name<=18&&alive[*name]);
    for(unsigned i=0;i<18;i++)assert(borrowers[i].gl_pipeline!=*name);
    alive[*name]=false;deleted++;
}
static void glUseProgramStages(GLuint p,unsigned stage,GLuint program)
{
    assert(alive[p]&&!validated[p]&&program);
    unsigned j=stage==GL_VERTEX_SHADER_BIT?0:stage==GL_GEOMETRY_SHADER_BIT?1:2;
    assert(!programs[p][j]);programs[p][j]=program;
}
static void glValidateProgramPipeline(GLuint p)
{
    assert(alive[p]);unsigned i=p-1,smooth=i<16?(i&1):i-16;
    assert(programs[p][0]==100+smooth*2+(i<16));
    assert(programs[p][1]==(i<16?200+i:0));
    assert(programs[p][2]==300+smooth);validated[p]=true;
}
static void glGetProgramPipelineiv(GLuint p,unsigned pname,GLint *valid)
{
    assert(pname==GL_VALIDATE_STATUS&&validated[p]);*valid=p!=fail_validate;if(startup_publication_error&&p==18){startup_raise_error(0x505);startup_raise_error(0x501);startup_raise_error(0x502);}
}
static void lru_add_free(unsigned *lru,LruNode *node)
{
    assert(!*lru&&returned<18&&node==&slots[returned].node);returned++;
}
/* PRODUCTION */
static void reset(PGRAPHGLState *r)
{
    startup_error_flags=0;startup_publication_error=false;
    memset(r,0,sizeof(*r));memset(slots,0,sizeof(slots));memset(borrowers,0,sizeof(borrowers));
    memset(alive,0,sizeof(alive));memset(validated,0,sizeof(validated));memset(programs,0,sizeof(programs));
    created=deleted=returned=fail_create=fail_validate=0;
    r->ready_pipelines=slots;r->ready_vsh.ready=r->ready_geom.ready=r->ready_psh.ready=true;
    for(unsigned i=0;i<4;i++)r->ready_vsh.stage[i].gl_stage_program=100+i;
    for(unsigned i=0;i<16;i++)gs[i].gl_stage_program=200+i;
    fs[0].gl_stage_program=300;fs[1].gl_stage_program=301;
    r->ready_geom.stages=gs;r->ready_psh.stages=fs;
}
int main(void)
{
    PGRAPHGLState r;
    for(unsigned mode=0;mode<40;mode++){
        reset(&r);
        if(mode>=1&&mode<=18)fail_create=mode;
        if(mode>=19&&mode<=36)fail_validate=mode-18;
        if(mode==37)r.ready_vsh.ready=false;
        if(mode==38)r.ready_psh.ready=false;
        if(mode==39)r.ready_geom.ready=false;
        ready_pipelines_init(&r);
        if(mode){
            assert(!r.ready_pipelines&&returned==18);
            assert(deleted==created-(fail_create?1:0));pipeline_owner_finalize(&r);
            for(unsigned i=1;i<=18;i++)assert(!alive[i]);
        }else{
            assert(r.ready_pipelines==slots&&created==18&&deleted==0&&!returned);
            for(unsigned round=0;round<2000;round++)for(unsigned i=0;i<18;i++){
                ShaderBinding *b=&borrowers[i];b->gl_prog_vs=slots[i].gl_prog_vs;
                b->gl_prog_gs=slots[i].gl_prog_gs;b->gl_prog_fs=slots[i].gl_prog_fs;
                assert(pipeline_owner_acquire(&r,b)&&b->gl_pipeline==slots[i].gl_pipeline&&validated[b->gl_pipeline]);
                pipeline_owner_release(&r,b);assert(!deleted);
            }
            ready_pipelines_finalize(&r);assert(!r.ready_pipelines&&deleted==18);
            pipeline_owner_finalize(&r);
        }
        assert(!startup_error_flags);
        ready_pipelines_finalize(&r);pipeline_owner_finalize(&r);
    }
    reset(&r);r.ready_pipelines=NULL;ready_pipelines_init(&r);ready_pipelines_finalize(&r);
    assert(!created&&!deleted&&!returned);pipeline_owner_finalize(&r);

#ifdef FIXTURE_CHECKED_STARTUP
    reset(&r);startup_raise_error(0x501);ready_pipelines_init(&r);
    assert(!r.ready_pipelines&&!created&&!deleted&&returned==18&&!startup_error_flags);pipeline_owner_finalize(&r);
    reset(&r);startup_publication_error=true;ready_pipelines_init(&r);
    assert(!r.ready_pipelines&&!startup_error_flags&&returned==18&&deleted==created);pipeline_owner_finalize(&r);
    puts("PASS pipeline checked startup: prior-error/no-owner rollback and multi-error final-tuple rejection");
#endif
    puts("PASS 18 native tuples, 36000 draw acquisitions without creation, 36 partial failures, unavailable-stage rollback and teardown");
    return 0;
}
