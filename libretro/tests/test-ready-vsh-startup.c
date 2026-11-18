/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Actual startup/finalization orchestration with native-owner API mocks.
 * Shader numerics and timing belong to the separate full GPU corpus. */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/vsh.h"
#define assert(x) do { if (!(x)) { fprintf(stderr,"VSH_OWNER_FAIL line=%u: %s\n",__LINE__,#x); exit(86); } } while (0)
#define MOCK static __attribute__((unused))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define GL_INVALID_INDEX UINT32_MAX
#define GL_NO_ERROR 0
#define GL_FALSE 0
#define GL_OUT_OF_MEMORY 0x505
#define GL_INVALID_OPERATION 0x502
#define GL_INVALID_VALUE 0x501
enum { GL_VERTEX_SHADER=1, GL_LINK_STATUS, GL_UNIFORM_BUFFER_BINDING,
       GL_UNIFORM_BUFFER, GL_STREAM_DRAW, GL_BUFFER_SIZE };
typedef unsigned GLuint,GLenum;
typedef int GLint;
typedef struct ShaderModuleCacheEntry { struct { GLenum kind; } key; GLuint gl_stage_program; } ShaderModuleCacheEntry;
typedef struct ShaderBinding { unsigned tag; } ShaderBinding;
typedef struct PGRAPHGLState {
 struct { ShaderModuleCacheEntry stage[4]; GLuint code_buffer;
          const ShaderBinding *uploaded_binding; bool ready; } ready_vsh;
} PGRAPHGLState;
struct MString { unsigned tag; };
static MString strings[4];
static PGRAPHGLState *machine;
static unsigned attempts,source_live,program_deleted,shader_deleted,detached,resolved;
static bool program_alive[5],shader_alive[5],shader_pending[5],attached[5];
static unsigned fail_program,fail_link,fail_block,storage_mode;
static bool sso,fail_name,buffer_alive;
static GLuint buffer_binding;
static GLint buffer_size;
static GLenum pending_error;
static unsigned error_reads,buffer_gens,buffer_calls,buffer_queries,buffer_deleted;
MOCK bool gl_sso_enabled(void){return sso;}
MOCK void raise_error(GLenum e){assert(e>=0x500&&e<=0x507);pending_error|=1u<<(e-0x500);}
MOCK GLenum glGetError(void){
 error_reads++;if(!pending_error)return GL_NO_ERROR;
 unsigned flag=(unsigned)__builtin_ctz(pending_error);pending_error&=~(1u<<flag);return 0x500+flag;
}
MString *pgraph_glsl_gen_vsh_interpreter_dynamic(const VshState *s,GenVshGlslOptions o){
 assert(!machine->ready_vsh.ready&&o.locations&&attempts<4);
 assert(s->smooth_shading==!!(attempts&2)&&o.prefix_outputs==!!(attempts&1));
 source_live++;strings[attempts].tag=attempts+1;return &strings[attempts];
}
MOCK const char *mstring_get_str(MString *s){assert(s&&source_live);return "fixture";}
MOCK void mstring_unref(MString *s){assert(s&&source_live);source_live--;}
MOCK GLuint pgraph_gl_separable_program(GLenum kind,const char *text,GLuint *shader){
 assert(kind==GL_VERTEX_SHADER&&text&&!machine->ready_vsh.ready);unsigned i=++attempts;
 assert(i<=4);*shader=i+10;shader_alive[i]=true;
 if(i==fail_program){raise_error(GL_OUT_OF_MEMORY);raise_error(GL_INVALID_VALUE);return 0;}
 program_alive[i]=attached[i]=true;return i;
}
MOCK void glDeleteShader(GLuint shader){
 unsigned i=shader-10;assert(i>=1&&i<=4&&shader_alive[i]&&!shader_pending[i]);
 shader_deleted++;shader_pending[i]=true;if(!attached[i])shader_alive[i]=false;
}
MOCK void delete_program(PGRAPHGLState *r,GLuint p){
 assert(r==machine&&!r->ready_vsh.ready&&p>=1&&p<=4&&program_alive[p]);
 program_alive[p]=false;program_deleted++;
 if(attached[p]){attached[p]=false;if(shader_pending[p])shader_alive[p]=false;}
}
MOCK void glGetProgramiv(GLuint p,GLenum kind,GLint *linked){
 assert(kind==GL_LINK_STATUS&&!machine->ready_vsh.ready);
 assert(p>=1&&p<=4&&program_alive[p]);*linked=p!=fail_link;
}
MOCK void glGetProgramInfoLog(GLuint p,size_t n,void *length,char *log){
 (void)length;assert(p>=1&&p<=4&&program_alive[p]&&n);
 for(size_t i=0;i<n;i++){assert(log[i]==0);}
 snprintf(log,n,"injected link failure");
}
MOCK void glDetachShader(GLuint p,GLuint shader){
 assert(p>=1&&p<=4&&program_alive[p]&&attached[p]&&shader==p+10&&shader_pending[p]);
 attached[p]=shader_alive[p]=false;detached++;
}
MOCK GLuint glGetUniformBlockIndex(GLuint p,const char *name){
 assert(program_alive[p]&&!attached[p]&&!strcmp(name,"XvsCode"));return p==fail_block?GL_INVALID_INDEX:0;
}
MOCK void glUniformBlockBinding(GLuint p,GLuint block,GLuint point){assert(program_alive[p]&&block==0&&point==0);}
MOCK void module_resolve_uniform_locs(ShaderModuleCacheEntry *s){assert(program_alive[s->gl_stage_program]&&!attached[s->gl_stage_program]);resolved++;}
MOCK void glGetIntegerv(GLenum name,GLint *out){assert(name==GL_UNIFORM_BUFFER_BINDING);*out=(GLint)buffer_binding;}
MOCK void glGenBuffers(int n,GLuint *out){
 assert(n==1&&!machine->ready_vsh.ready);buffer_gens++;*out=fail_name?0:77;
 if(!*out)raise_error(GL_OUT_OF_MEMORY);else buffer_alive=true;
}
MOCK void glBindBuffer(GLenum target,GLuint name){assert(target==GL_UNIFORM_BUFFER&&(name==999||name==0||(name==77&&buffer_alive)));buffer_binding=name;}
MOCK void glBufferData(GLenum target,size_t bytes,const void *data,GLenum usage){
 assert(!machine->ready_vsh.ready&&target==GL_UNIFORM_BUFFER&&buffer_binding==77&&buffer_alive&&data==NULL&&usage==GL_STREAM_DRAW);
 assert(bytes==VSH_INTERPRETER_BUFFER_SIZE&&bytes==2192);buffer_calls++;
 if(storage_mode==1){raise_error(GL_OUT_OF_MEMORY);return;}
 buffer_size=(GLint)bytes+(storage_mode==2?1:0);
 if(storage_mode==3)raise_error(GL_INVALID_OPERATION);
}
MOCK void glGetBufferParameteriv(GLenum target,GLenum param,GLint *out){
 assert(!machine->ready_vsh.ready&&target==GL_UNIFORM_BUFFER&&param==GL_BUFFER_SIZE&&buffer_binding==77&&buffer_alive);
 buffer_queries++;*out=buffer_size;if(storage_mode==4)raise_error(GL_INVALID_OPERATION);
}
MOCK void glDeleteBuffers(int n,const GLuint *name){
 assert(n==1&&*name==77&&buffer_alive&&!machine->ready_vsh.ready);buffer_alive=false;buffer_deleted++;
 if(buffer_binding==*name)buffer_binding=0;
}
/* PRODUCTION */
static void setup(PGRAPHGLState *r){
 memset(r,0,sizeof(*r));memset(program_alive,0,sizeof(program_alive));memset(shader_alive,0,sizeof(shader_alive));
 memset(shader_pending,0,sizeof(shader_pending));memset(attached,0,sizeof(attached));machine=r;
 attempts=source_live=program_deleted=shader_deleted=detached=resolved=0;
 fail_program=fail_link=fail_block=storage_mode=0;sso=true;fail_name=buffer_alive=false;
 buffer_binding=999;buffer_size=0;pending_error=error_reads=buffer_gens=buffer_calls=buffer_queries=buffer_deleted=0;
}
static void assert_empty(PGRAPHGLState *r){
 assert(!r->ready_vsh.ready&&!r->ready_vsh.code_buffer&&!r->ready_vsh.uploaded_binding&&!source_live&&!buffer_alive);
 for(unsigned i=1;i<=4;i++)assert(!program_alive[i]&&!shader_alive[i]&&!attached[i]);
 for(unsigned i=0;i<4;i++)assert(!r->ready_vsh.stage[i].gl_stage_program&&!r->ready_vsh.stage[i].key.kind);
 assert(buffer_binding==999&&!pending_error);
}
int main(void){
 unsigned failures=0;PGRAPHGLState r;
 for(unsigned mode=0;mode<19;mode++){
  setup(&r);
  if(mode>=1&&mode<=4)fail_link=mode;
  else if(mode>=5&&mode<=8)fail_block=mode-4;
  else if(mode>=9&&mode<=12)fail_program=mode-8;
  else if(mode==13)fail_name=true;
  else if(mode>=14&&mode<=17)storage_mode=mode-13;
  else if(mode==18)raise_error(GL_INVALID_VALUE);
  ready_vsh_init(&r);
  if(!mode){
   assert(r.ready_vsh.ready&&r.ready_vsh.code_buffer==77&&buffer_alive&&buffer_binding==999&&buffer_size==2192&&!pending_error);
   assert(attempts==4&&detached==4&&resolved==4&&shader_deleted==4&&buffer_calls==1&&buffer_queries==1&&!source_live);
   ShaderBinding b={0};r.ready_vsh.uploaded_binding=&b;ready_vsh_finalize(&r);assert(program_deleted==4&&buffer_deleted==1);
  }else{
   failures++;assert(!r.ready_vsh.ready);
   if(mode==18)assert(attempts==0&&buffer_gens==0&&error_reads==1);
   if(mode==13)assert(buffer_gens==1&&buffer_calls==0&&buffer_queries==0);
   assert(program_deleted==attempts-(fail_program?1:0));
  }
  assert_empty(&r);ready_vsh_finalize(&r);assert_empty(&r);
  /* Retry on the same cleared owner, preserving the caller's buffer binding. */
  fail_program=fail_link=fail_block=storage_mode=0;fail_name=false;attempts=0;
  memset(shader_pending,0,sizeof(shader_pending));ready_vsh_init(&r);
  assert(r.ready_vsh.ready&&buffer_binding==999);ready_vsh_finalize(&r);assert_empty(&r);
 }
 assert(failures==18);
 setup(&r);sso=false;ready_vsh_init(&r);assert_empty(&r);assert(!attempts&&!error_reads&&!buffer_gens);
 puts("PASS prepared VSH startup: 18 resource/link/block/prior-error failures, exact 2192-byte UBO, binding restore, no-SSO, same-owner retry and repeated teardown");
 return 0;
}
