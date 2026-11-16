/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "hw/xbox/nv2a/pgraph/gl/ready-ff-state.h"
#include "hw/xbox/nv2a/pgraph/glsl/geom.h"
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
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
#define XEMU_EXPERIMENTAL_READY_FF
#define GL_PIPELINE_OWNER_BUCKETS 1
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
static void delete_program(PGRAPHGLState *r,GLuint p){(void)r;glDeleteProgram(p);}
static void lru_add_free(unsigned *lru,LruNode *node){(void)lru;(void)node;assert(!"native preparation failed; fallback is not a pass");}
static unsigned resolved;
static void module_resolve_uniform_locs(ShaderModuleCacheEntry *s){
 for(unsigned i=0;i<VshUniform__COUNT;i++)glGetUniformLocation(s->gl_stage_program,VshUniformInfo[i].name);
 resolved++;
}
static GLuint pgraph_gl_separable_program(GLenum kind,const char *src,GLuint *shader){
 GLuint sh=glCreateShader(kind),program=glCreateProgram();glShaderSource(sh,1,&src,NULL);glCompileShader(sh);
 glProgramParameteri(program,GL_PROGRAM_SEPARABLE,GL_TRUE);glAttachShader(program,sh);glLinkProgram(program);*shader=sh;
 GLint valid;glGetProgramiv(program,GL_LINK_STATUS,&valid);
 if(!valid){char log[8192];glGetProgramInfoLog(program,sizeof(log),NULL,log);fprintf(stderr,"native stage kind=%x: %s\n",kind,log);}
 return program;
}
static GLuint prepare(GLenum kind,MString *source){
 assert(source);GLuint shader,program=pgraph_gl_separable_program(kind,mstring_get_str(source),&shader);mstring_unref(source);
 GLint valid;glGetProgramiv(program,GL_LINK_STATUS,&valid);assert(valid);glDetachShader(program,shader);glDeleteShader(shader);return program;
}
/* PRODUCTION */
/* CONTEXT */
int main(void){
 init_gl();
 for(unsigned generation=0;generation<2;generation++){
  PGRAPHGLState r={0};ShaderModuleCacheEntry *stages=calloc(56,sizeof(*stages)),gs[16]={0},fs[2]={0};
  ShaderBinding anchors[252]={0};assert(stages);resolved=0;
  for(unsigned i=0;i<16;i++){
   GeomState state;GenGeomGlslOptions opts;pgraph_glsl_geom_dynamic_state(i,&state,&opts);
   gs[i].gl_stage_program=prepare(GL_GEOMETRY_SHADER,pgraph_glsl_gen_geom_dynamic(&state,opts));
  }
  for(unsigned i=0;i<2;i++){
   PshState state={.smooth_shading=i,.combiner_dynamic=true,.depth_format=DEPTH_FORMAT_HELPER_LIVENESS};
   GLuint p=fs[i].gl_stage_program=prepare(GL_FRAGMENT_SHADER,pgraph_glsl_gen_psh_ready(&state,(GenPshGlslOptions){.locations=true}));
   for(unsigned kind=0;kind<3;kind++)for(unsigned unit=0;unit<4;unit++){
    const char *names[]={"fsTex2D","fsTex3D","fsTexCube"};char name[32];snprintf(name,sizeof(name),"%s%u",names[kind],unit);
    GLint loc=glGetUniformLocation(p,name);assert(loc>=0);glProgramUniform1i(p,loc,kind*4+unit);
   }
  }
  r.ready_ff.stages=stages;r.ready_ff.pipelines=anchors;r.ready_geom.stages=gs;r.ready_geom.ready=true;r.ready_psh.stages=fs;r.ready_psh.ready=true;
  ready_ff_init(&r);assert(r.ready_ff.ready&&resolved==56&&glGetError()==GL_NO_ERROR);
  GLuint native[252],vs[56];for(unsigned i=0;i<56;i++)vs[i]=stages[i].gl_stage_program;
  for(unsigned i=0;i<252;i++){
   ShaderBinding borrower={.gl_prog_vs=anchors[i].gl_prog_vs,.gl_prog_gs=anchors[i].gl_prog_gs,.gl_prog_fs=anchors[i].gl_prog_fs};
   assert(pipeline_owner_acquire(&r,&borrower));native[i]=borrower.gl_pipeline;
   GLint valid;glGetProgramPipelineiv(native[i],GL_VALIDATE_STATUS,&valid);assert(valid&&glIsProgramPipeline(native[i]));
   glBindProgramPipeline(native[i]);pipeline_owner_release(&r,&borrower);assert(glIsProgramPipeline(native[i]));
  }
  glBindProgramPipeline(0);ready_ff_pipelines_finalize(&r);pipeline_owner_finalize(&r);
  for(unsigned i=0;i<252;i++)assert(!glIsProgramPipeline(native[i]));
  ready_ff_stages_finalize(&r);for(unsigned i=0;i<56;i++)assert(!glIsProgram(vs[i]));free(stages);
  for(unsigned i=0;i<16;i++)glDeleteProgram(gs[i].gl_stage_program);
  for(unsigned i=0;i<2;i++)glDeleteProgram(fs[i].gl_stage_program);
  assert(glGetError()==GL_NO_ERROR);
 }
 puts("PASS native FF: two owner generations, each 56 ordinary stages and 252 complete validated VS/GS/FS tuples; all borrowed then retired. NO numerical draws or gameplay timing qualification.");
}
