/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Native API mocks test orchestration/lifetime, not shader numerics or timing. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include "hw/xbox/nv2a/pgraph/glsl/psh.h"
typedef unsigned GLuint,GLenum;typedef int GLint;
enum { GL_FALSE,GL_FRAGMENT_SHADER,GL_ACTIVE_TEXTURE,GL_MAX_TEXTURE_IMAGE_UNITS,
       GL_LINK_STATUS,GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_CUBE_MAP,GL_TEXTURE0=100,GL_TEXTURE4=104 };
typedef struct ShaderModuleCacheKey {GLenum kind;struct {PshState state;GenPshGlslOptions glsl_opts;}psh;} ShaderModuleCacheKey;
typedef struct ShaderModuleCacheEntry {unsigned node;ShaderModuleCacheKey key;GLuint gl_stage_program,gl_shader;} ShaderModuleCacheEntry;
typedef struct ShaderBinding {struct {PshState psh;}state;GLuint gl_prog_fs;bool ready_psh;} ShaderBinding;
typedef struct TextureBinding {unsigned refcnt;GLenum gl_target;GLuint gl_texture;uint8_t recycle_key[3];}TextureBinding;
typedef struct PGRAPHGLState {
 struct {ShaderModuleCacheEntry *stages;GLint control_locs[2];uint32_t controls[2][8];bool controls_valid[2];const ShaderBinding *uploaded_binding;GLuint aliases[8];uint8_t aliases_valid;bool aliases_bound,ready;}ready_psh;
 unsigned shader_module_cache,texture_pool,sampled_texture_pool;
 GLuint tex_shadow_name[4];GLenum tex_shadow_target[4];uint8_t tex_shadow_valid_mask;
 TextureBinding *texture_binding[4];
}PGRAPHGLState;
typedef struct PGRAPHState {PGRAPHGLState *gl_renderer_state;bool enabled[4];}PGRAPHState;
static struct {PGRAPHState pgraph;}machine,*g_nv2a=&machine;
typedef void *gpointer;
struct MString {unsigned tag;};static MString sources[2];
static unsigned source_live,created,alive[3],attached[3],freed,queries,loc_fail,link_fail,units,detached,routes,route_sum;
static unsigned active,bound[12][2],binds,uploads,returned;
static uint32_t uniform_values[3][8];
MString *pgraph_glsl_gen_psh_ready(const PshState *s,GenPshGlslOptions opts){assert(opts.locations);assert(s->depth_format&DEPTH_FORMAT_HELPER_LIVENESS);source_live++;return &sources[!!s->smooth_shading];}
static void mstring_unref(MString *s){assert(s && source_live);source_live--;}
static const char *mstring_get_str(MString *s){assert(s);return "";}
static GLuint pgraph_gl_separable_program(GLenum kind,const char *text,GLuint *shader){(void)text;assert(kind==GL_FRAGMENT_SHADER);GLuint p=++created;assert(p<=2);alive[p]=attached[p]=1;*shader=p+10;return p;}
static void glDeleteShader(GLuint sh){assert(sh==11||sh==12);}
static void delete_program(PGRAPHGLState *r,GLuint p){(void)r;assert(p && alive[p]);alive[p]=attached[p]=0;}
static void glGetIntegerv(GLenum name,GLint *out){*out=name==GL_MAX_TEXTURE_IMAGE_UNITS?units:active;}
static void glActiveTexture(GLenum unit){assert(unit>=GL_TEXTURE0&&unit<GL_TEXTURE0+units);active=unit;}
static void glBindTexture(GLenum target,GLuint name){assert(active>=GL_TEXTURE0&&active<GL_TEXTURE0+12);bound[active-GL_TEXTURE0][target==GL_TEXTURE_3D?0:1]=name;binds+=active>=GL_TEXTURE4;}
static void glGetProgramiv(GLuint p,GLenum name,GLint *out){assert(name==GL_LINK_STATUS && !machine.pgraph.gl_renderer_state->ready_psh.ready);*out=p!=link_fail;}
static void glDetachShader(GLuint p,GLuint sh){assert(alive[p]&&attached[p]&&sh==p+10);attached[p]=0;detached++;}
static void module_resolve_uniform_locs(ShaderModuleCacheEntry *s){assert(alive[s->gl_stage_program]&&!attached[s->gl_stage_program]);}
static GLint glGetUniformLocation(GLuint p,const char *name){assert(alive[p]&&name);return ++queries==loc_fail?-1:1;}
static void glProgramUniform1i(GLuint p,GLint loc,int unit){assert(alive[p]&&loc>=0&&unit>=0&&unit<12);routes++;route_sum+=unit;}
static void glProgramUniform4uiv(GLuint p,GLint loc,int count,const uint32_t *v){assert(alive[p]&&loc>=0&&count==2);memcpy(uniform_values[p],v,32);uploads++;}
static __attribute__((unused)) void lru_add_free(unsigned *cache,unsigned *node){(void)cache;assert(node);freed++;}
static __attribute__((unused)) bool pgraph_is_texture_enabled(PGRAPHState *pg,unsigned unit){return pg->enabled[unit];}
static unsigned sampled_texture_key_load(const uint8_t *key){(void)key;return 0;}
static void sampled_texture_put(unsigned *pool,unsigned key,GLuint name){(void)pool;(void)key;for(unsigned i=0;i<8;i++)if(machine.pgraph.gl_renderer_state->ready_psh.aliases[i]==name)assert(!(machine.pgraph.gl_renderer_state->ready_psh.aliases_valid&(1u<<i)));for(unsigned i=0;i<12;i++)for(unsigned j=0;j<2;j++)if(bound[i][j]==name)bound[i][j]=0;}
static void obj_pool_give(unsigned *pool,TextureBinding *b){(void)pool;assert(!b->refcnt);returned++;}
#ifdef FIXTURE_READY_GATE
static unsigned gate_notifications,gate_depth;
void pgraph_glsl_psh_dynamic_ready(const PshState *state){
 assert((unsigned)state->depth_format==gate_depth);gate_notifications++;
}
#endif
/* SELECTOR */
/* PRODUCTION */
/* RETIREMENT */
static void setup(PGRAPHGLState *r,ShaderModuleCacheEntry slots[2]){
 memset(r,0,sizeof(*r));memset(slots,0,sizeof(*slots)*2);memset(bound,0,sizeof(bound));memset(alive,0,sizeof(alive));memset(attached,0,sizeof(attached));memset(uniform_values,0,sizeof(uniform_values));
 memset(&machine,0,sizeof(machine));machine.pgraph.gl_renderer_state=r;r->ready_psh.stages=slots;
 source_live=created=freed=queries=loc_fail=link_fail=detached=routes=route_sum=binds=uploads=returned=0;units=16;active=GL_TEXTURE0+3;
}
static void quiet_abort(int signal_number){(void)signal_number;_Exit(86);}
int main(void){
 signal(SIGABRT,quiet_abort);
 PGRAPHGLState r;ShaderModuleCacheEntry slots[2];
 for(unsigned failure=0;failure<29;failure++){
  setup(&r,slots);if(failure==0)units=8;else if(failure<3)link_fail=failure;else loc_fail=failure-2;
  ready_psh_init(&r);assert(!r.ready_psh.ready&&!r.ready_psh.stages&&freed==2&&!source_live&&!alive[1]&&!alive[2]);assert(active==GL_TEXTURE0+3);
 }
 setup(&r,slots);ready_psh_init(&r);assert(r.ready_psh.ready&&r.ready_psh.stages==slots&&!freed&&!source_live&&created==2&&detached==2&&routes==24&&route_sum==132);
#ifdef FIXTURE_READY_GATE
 PshState gated={.combiner_dynamic=true},exact=gated;gate_depth=0;gate_notifications=0;
 assert(ready_psh_adopt_gate(&r,&gated)&&gate_notifications==1&&!memcmp(&gated,&exact,sizeof(gated)));
 r.ready_psh.ready=false;
 assert(!ready_psh_adopt_gate(&r,&gated)&&gate_notifications==1&&!memcmp(&gated,&exact,sizeof(gated)));
 r.ready_psh.ready=true;gated.shader_stage_program=31;exact=gated;
 assert(!ready_psh_adopt_gate(&r,&gated)&&gate_notifications==1&&!memcmp(&gated,&exact,sizeof(gated)));
#endif
 ShaderBinding a={.state.psh={.depth_format=DEPTH_FORMAT_HELPER_LIVENESS,.combiner_dynamic=true},.ready_psh=true,.gl_prog_fs=1},b=a;
 a.state.psh.shader_stage_program=1;b.state.psh.shader_stage_program=2;b.state.psh.dim_tex[0]=3;a.state.psh.dim_tex[0]=2;
 for(unsigned i=0;i<4;i++){a.state.psh.dim_tex[i]=i?2:a.state.psh.dim_tex[i];b.state.psh.dim_tex[i]=i?2:b.state.psh.dim_tex[i];}
 ShaderModuleCacheKey k={.kind=GL_FRAGMENT_SHADER,.psh.state=a.state.psh};assert(ready_psh_find(&r,&k)==&slots[0]);k.psh.state.smooth_shading=true;assert(ready_psh_find(&r,&k)==&slots[1]);k.psh.state.depth_format=0;assert(!ready_psh_find(&r,&k));
 TextureBinding t={.refcnt=1,.gl_target=GL_TEXTURE_3D,.gl_texture=42};r.texture_binding[0]=&t;machine.pgraph.enabled[0]=true;
 ready_psh_upload(&machine.pgraph,&a);assert(uploads==1&&binds==8&&bound[4][0]==42&&active==GL_TEXTURE0+3);
 ready_psh_upload(&machine.pgraph,&a);assert(uploads==1&&binds==8);
 unsigned primary_valid=r.ready_psh.aliases_valid;pgraph_gl_tex_shadow_invalidate(&r);pgraph_gl_tex_shadow_reset(&r);
 assert(r.ready_psh.aliases_valid==primary_valid&&bound[4][0]==42);ready_psh_upload(&machine.pgraph,&a);assert(binds==8);
 ready_psh_upload(&machine.pgraph,&b);assert(uploads==2);
 ready_psh_upload(&machine.pgraph,&a);assert(uploads==3);
 ready_psh_invalidate(&r,&a);assert(!r.ready_psh.uploaded_binding);a.state.psh.window_clip_exclusive=true;ready_psh_upload(&machine.pgraph,&a);assert(uploads==4);
 machine.pgraph.enabled[0]=false;ready_psh_upload(&machine.pgraph,&a);assert(bound[4][0]==0&&binds==9);
 machine.pgraph.enabled[0]=true;ready_psh_upload(&machine.pgraph,&a);assert(bound[4][0]==42);
 TextureBinding other={.refcnt=1,.gl_target=GL_TEXTURE_2D,.gl_texture=77};
 unsigned valid=r.ready_psh.aliases_valid;texture_binding_destroy(&other);assert(r.ready_psh.aliases_valid==valid&&bound[4][0]==42);
 texture_binding_destroy(&t);assert(returned==2&&r.ready_psh.aliases_valid==(valid&~1u)&&bound[4][0]==0);
 /* Driver and CPU pool reuse exactly the same name/address for a new owner. */
 t.refcnt=1;ready_psh_upload(&machine.pgraph,&a);assert(bound[4][0]==42);
 r.ready_psh.aliases_valid=0;memset(bound,0,sizeof(bound));ready_psh_upload(&machine.pgraph,&a);assert(bound[4][0]==42&&active==GL_TEXTURE0+3);
 ready_psh_finalize(&r);assert(!r.ready_psh.stages&&!r.ready_psh.ready&&!r.ready_psh.aliases_bound&&!alive[1]&&!alive[2]&&active==GL_TEXTURE0+3);for(unsigned i=4;i<12;i++)for(unsigned j=0;j<2;j++)assert(!bound[i][j]);
 ready_psh_finalize(&r);
 puts("PASS native fragment owners: 29 startup failures, exact slot return, binding invalidation, per-program controls, disabled aliases, identical-name/CPU-slot reuse, external invalidation and teardown");
}
