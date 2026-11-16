/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/nv2a/pgraph/gl/ready-ff-state.h"
#undef assert
#define assert(x) do {if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(86);}}while(0)
#define XEMU_EXPERIMENTAL_READY_FF
#define GL_PIPELINE_OWNER_BUCKETS 1
#define GL_VERTEX_SHADER 1
#define GL_VERTEX_SHADER_BIT 1
#define GL_GEOMETRY_SHADER_BIT 2
#define GL_FRAGMENT_SHADER_BIT 4
#define GL_VALIDATE_STATUS 5
#define GL_LINK_STATUS 6
#define GL_FALSE 0
typedef unsigned GLuint;
typedef int GLint;
typedef struct {unsigned unused;} LruNode;
typedef struct {unsigned kind;struct {VshState state;GenVshGlslOptions glsl_opts;}vsh;} ShaderModuleCacheKey;
typedef struct {LruNode node;ShaderModuleCacheKey key;GLuint gl_stage_program,gl_shader;} ShaderModuleCacheEntry;
typedef struct ShaderBinding {
 LruNode node;GLuint gl_pipeline,gl_prog_vs,gl_prog_gs,gl_prog_fs;
 struct ShaderBinding *pipeline_next,*pipeline_prev,*pipeline_hash_next;
}ShaderBinding;
typedef struct {
 ShaderBinding *pipeline_owners[1];unsigned shader_cache,shader_module_cache;
 struct {ShaderModuleCacheEntry *stages;ShaderBinding *pipelines;bool ready;}ready_ff;
 struct {ShaderModuleCacheEntry *stages;bool ready;}ready_geom,ready_psh;
}PGRAPHGLState;
static ShaderModuleCacheEntry stages[56],gs[16],fs[2];
static ShaderBinding anchors[252],borrowers[252];
static unsigned submitted,shader_deleted,resolved,created,deleted,program_deleted,returned,modules_returned;
static unsigned fail_link,fail_create,fail_validate,fail_program;
static bool alive[253],validated[253],program_alive[56];
static GLuint programs[253][3];
static MString text;
static PGRAPHGLState *test_state;
static MString *pgraph_test_gen(const VshState *s,GenVshGlslOptions o){
 int index=ready_ff_state_index(s,o.prefix_outputs);assert(index>=0&&index<56);
 assert(o.locations&&!o.vulkan);text.index=index;return &text;
}
#define pgraph_glsl_gen_vsh pgraph_test_gen
static const char *mstring_get_str(MString *s){return (const char *)s;}
static void mstring_unref(MString *s){assert(s==&text);}
static GLuint pgraph_gl_separable_program(unsigned kind,const char *source,GLuint *shader){
 assert(!test_state->ready_ff.ready);
 assert(kind==GL_VERTEX_SHADER&&((const MString *)source)->index==submitted);
 unsigned i=submitted++;*shader=2000+i;
 if(fail_program==i+1)return 0;
 program_alive[i]=true;return 1000+i;
}
static void glDeleteShader(GLuint sh){assert(sh==2000+shader_deleted);shader_deleted++;}
static void glGetProgramiv(GLuint p,unsigned kind,GLint *ok){assert(kind==GL_LINK_STATUS&&p>=1000&&p<1056&&program_alive[p-1000]);*ok=fail_link!=p-999;}
static void glDetachShader(GLuint p,GLuint s){assert(p==1000+resolved&&s==2000+resolved);}
static void module_resolve_uniform_locs(ShaderModuleCacheEntry *s){assert(s==&stages[resolved]&&s->gl_stage_program==1000+resolved);resolved++;}
static void delete_program(PGRAPHGLState *r,GLuint p){
 assert(!r->ready_ff.ready&&p>=1000&&p<1056&&program_alive[p-1000]);
 for(unsigned i=1;i<=252;i++)assert(!alive[i]);
 program_alive[p-1000]=false;program_deleted++;
}
static void glGenProgramPipelines(int n,GLuint *p){assert(!test_state->ready_ff.ready&&n==1&&created<252);*p=++created;if(*p==fail_create)*p=0;else alive[*p]=true;}
static void glDeleteProgramPipelines(int n,const GLuint *p){
 assert(n==1&&*p&&*p<=252&&alive[*p]);
 for(unsigned i=0;i<252;i++)assert(borrowers[i].gl_pipeline!=*p);
 alive[*p]=false;deleted++;
}
static void glUseProgramStages(GLuint p,unsigned stage,GLuint program){
 assert(alive[p]&&!validated[p]&&program);unsigned j=stage==1?0:stage==2?1:2;
 assert(!programs[p][j]);programs[p][j]=program;
}
static void glValidateProgramPipeline(GLuint p){
 unsigned i=p-1,tuple=i%18,group=i/18,smooth=tuple<16?(tuple&1):tuple-16;
 assert(alive[p]&&programs[p][0]==1000+group*4+smooth*2+(tuple<16));
 assert(programs[p][1]==(tuple<16?3000+tuple:0));assert(programs[p][2]==4000+smooth);validated[p]=true;
}
static void glGetProgramPipelineiv(GLuint p,unsigned kind,GLint *valid){assert(kind==5&&validated[p]);*valid=p!=fail_validate;}
static void lru_add_free(unsigned *lru,LruNode *node){
 if(lru==&test_state->shader_cache){assert(returned<252&&node==&anchors[returned].node);returned++;}
 else {assert(lru==&test_state->shader_module_cache&&modules_returned<56&&node==&stages[modules_returned].node);modules_returned++;}
}
/* PRODUCTION */
static void reset(PGRAPHGLState *r){
 test_state=r;memset(r,0,sizeof(*r));memset(stages,0,sizeof(stages));memset(anchors,0,sizeof(anchors));memset(borrowers,0,sizeof(borrowers));
 memset(alive,0,sizeof(alive));memset(validated,0,sizeof(validated));memset(program_alive,0,sizeof(program_alive));memset(programs,0,sizeof(programs));
 submitted=shader_deleted=resolved=created=deleted=program_deleted=returned=modules_returned=0;
 fail_link=fail_create=fail_validate=fail_program=0;
 r->ready_ff.stages=stages;r->ready_ff.pipelines=anchors;r->ready_geom.stages=gs;r->ready_psh.stages=fs;
 r->ready_geom.ready=r->ready_psh.ready=true;
 for(unsigned i=0;i<16;i++)gs[i].gl_stage_program=3000+i;
 fs[0].gl_stage_program=4000;fs[1].gl_stage_program=4001;
}
int main(void){
 PGRAPHGLState r;
 for(unsigned mode=0;mode<=618;mode++){
  reset(&r);
  if(mode>=1&&mode<=56)fail_link=mode;
  else if(mode<=112&&mode>=57)fail_program=mode-56;
  else if(mode<=364&&mode>=113)fail_create=mode-112;
  else if(mode<=616&&mode>=365)fail_validate=mode-364;
  else if(mode==617)r.ready_geom.ready=false;
  else if(mode==618)r.ready_psh.ready=false;
  /* Actual pool-member addresses distinguish the two LRU owners. */
  ready_ff_init(&r);
  if(!mode){
   assert(r.ready_ff.ready&&submitted==56&&resolved==56&&created==252&&!deleted&&!returned&&!modules_returned);
   for(unsigned i=0;i<56;i++){
    ShaderModuleCacheKey *key=&stages[i].key;
    assert(ready_ff_find(&r,key)==&stages[i]);
    key->vsh.state.z_perspective=true;assert(ready_ff_find(&r,key)==&stages[i]);
    key->vsh.state.compressed_attrs=1;assert(!ready_ff_find(&r,key));key->vsh.state.compressed_attrs=0;
    key->vsh.glsl_opts.vulkan=true;assert(!ready_ff_find(&r,key));key->vsh.glsl_opts.vulkan=false;
   }
   for(unsigned round=0;round<200;round++)for(unsigned i=0;i<252;i++){
    ShaderBinding *b=&borrowers[i];b->gl_prog_vs=anchors[i].gl_prog_vs;b->gl_prog_gs=anchors[i].gl_prog_gs;b->gl_prog_fs=anchors[i].gl_prog_fs;
    assert(pipeline_owner_acquire(&r,b)&&b->gl_pipeline==anchors[i].gl_pipeline&&validated[b->gl_pipeline]);
    pipeline_owner_release(&r,b);assert(!deleted&&created==252);
   }
   ready_ff_pipelines_finalize(&r);ready_ff_stages_finalize(&r);assert(deleted==252&&program_deleted==56);
  }else{
   assert(!r.ready_ff.ready&&!r.ready_ff.stages&&!r.ready_ff.pipelines&&returned==252&&modules_returned==56);
   assert(deleted==created-(fail_create?1:0));assert(program_deleted==submitted-(fail_program?1:0));
  }
  for(unsigned i=0;i<56;i++)assert(!program_alive[i]);
  for(unsigned i=1;i<=252;i++)assert(!alive[i]);
  pipeline_owner_finalize(&r);ready_ff_pipelines_finalize(&r);ready_ff_stages_finalize(&r);
 }
 puts("PASS FF owners: 56 stages/252 tuples, 50400 allocation-free draw acquisitions, 618 partial/unavailable failures, exact rollback and repeat teardown");
}
