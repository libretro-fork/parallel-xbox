#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Flip-time readiness, native cache-slot lends and localized draw wakeup."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text()
def fn(text,name):
 m=re.search(r'^(?:static )?(?:bool|void|int) '+name+r'\([^;{]*\)\s*\{',text,re.M);assert m,name
 b=text.index('{',m.start())+1;depth=1
 while depth:depth+=(text[b]=='{')-(text[b]=='}');b+=1
 return text[m.start():b]
key=re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;',h,re.S)[0]
parts='\n'.join(fn(s,n) for n in ('state_module_keys','module_ready','spec_track_find','spec_binding_reservation','spec_binding_cancel','spec_binding_track','state_modules_ready','spec_binding_pump','forget_binding_program_uniforms','shader_cache_entry_post_evict','pgraph_gl_uniform_sources_invalidate'))
parts=fn((r/'hw/xbox/nv2a/pgraph/glsl/geom.c').read_text(),'pgraph_glsl_need_geom')+'\n'+parts
# Check actual call-site ordering: service after module/seed work, and cancel
# the old lend before lookup can recycle its native cache slot.
flip=fn(s,'pgraph_gl_shaders_flip_service');assert flip.index('comb_async_service(')<flip.index('spec_binding_pump(')
draw=fn(s,'pgraph_gl_bind_shaders');assert draw.index('spec_binding_cancel(r);')<draw.index('lru_lookup(')
assert 'r->shader_binding->dyn_gen == pgraph_glsl_dynamic_gen' in draw
assert 'binding != fbind && !binding->initialized' in draw
assert draw.index('spec_binding_track(r, fbind);')<draw.index('if (fbind->initialized)')
assert 'spec_binding_track(r, binding);' in draw
for forbidden in ('malloc(', 'g_new', 'realloc(', 'glGet', 'glFinish', 'glLink', 'glCompile', 'lru_lookup('):
 assert forbidden not in fn(s,'spec_binding_pump'),forbidden
code=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "qemu/lru.h"
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
typedef unsigned GLenum;typedef unsigned GLuint;
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define SPEC_ADOPTED 2
#define SPEC_PIPE_RESERVED 3
#define GL_VERTEX_SHADER 0x8b31u
#define GL_FRAGMENT_SHADER 0x8b30u
#define GL_GEOMETRY_SHADER 0x8dd9u
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL GL binding pump line=%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
/* KEY */
typedef struct {LruNode node;ShaderModuleCacheKey key;GLuint gl_stage_program;bool locs_pending;} ShaderModuleCacheEntry;
typedef struct {LruNode node;ShaderState state;unsigned dyn_gen;bool initialized,cached;GLuint gl_program,gl_pipeline,gl_prog_vs,gl_prog_fs,gl_prog_gs;void *program;int program_owner;} ShaderBinding;
typedef struct {Lru shader_cache,shader_module_cache;ShaderBinding *shader_binding,*spec_binding;struct{uint64_t hash;int phase;}spec_track[2];bool wide_lines;struct{bool valid,ff_valid;}vsh_uni_src;bool uni_cache;} PGRAPHGLState;
static PGRAPHGLState r;
static ShaderBinding slots[2];static ShaderModuleCacheEntry modules[3];
static unsigned generated,restores,deleted;static GLuint bound;
static bool sso=true,boost=true;
unsigned pgraph_glsl_dynamic_gen;
static bool gl_sso_enabled(void){return sso;}static bool first_meet_enabled(void){return boost;}
static uint64_t fast_hash(const uint8_t *data,size_t n){(void)data;CHECK(n==sizeof(ShaderModuleCacheKey)||n==sizeof(PshState));return 42;}
static void uniform_cache_invalidate(bool *v){*v=false;}
/* Namespace lifetime semantics are covered by test-gl-program-namespace;
 * this fixture checks the pump and its actual eviction helper together. */
static void uniform_cache_forget(bool *v,GLuint program){(void)v;CHECK(program);}
static void glUseProgram(GLuint p){CHECK(p==0);}
static void glBindProgramPipeline(GLuint p){bound=p;restores++;}
static void glDeleteProgram(GLuint p){(void)p;deleted++;}
static void glDeleteProgramPipelines(unsigned n,GLuint *p){CHECK(n==1);(void)p;deleted++;}
static void shader_binary_release(void *p,int *owner){(void)p;*owner=0;}
static bool state_module_keys(const ShaderState *,ShaderModuleCacheKey[3]);
static bool module_ready(PGRAPHGLState *,const ShaderModuleCacheKey *);
static void generate_shaders(PGRAPHGLState *pg,ShaderBinding *target){
 CHECK(target==pg->spec_binding&&!target->initialized);
 ShaderModuleCacheKey keys[3];bool gs=state_module_keys(&target->state,keys);
 for(unsigned i=gs?0:1;i<3;i++)CHECK(module_ready(pg,&keys[i]));
 target->initialized=true;target->gl_pipeline=123;bound=123;generated++;
}
/* PRODUCTION */
static void init_node(Lru *cache,LruNode *node,const void *key){(void)cache;ShaderBinding *b=container_of(node,ShaderBinding,node);b->state=*(const ShaderState *)key;b->initialized=false;b->gl_pipeline=b->gl_program=0;b->program=NULL;}
static bool compare(Lru *cache,LruNode *node,const void *key){(void)cache;return memcmp(&container_of(node,ShaderBinding,node)->state,key,sizeof(ShaderState));}
int main(void){
 lru_init(&r.shader_cache);lru_init(&r.shader_module_cache);r.shader_cache.init_node=init_node;r.shader_cache.compare_nodes=compare;r.shader_cache.post_node_evict=shader_cache_entry_post_evict;
 unsigned cases=0;
 for(unsigned marker=1;marker<=3;marker++)for(unsigned gen_case=0;gen_case<3;gen_case++)for(unsigned geometry=0;geometry<2;geometry++)for(unsigned cover=0;cover<2;cover++)for(unsigned mask=0;mask<64;mask++)for(unsigned scenario=0;scenario<10;scenario++){
  lru_flush(&r.shader_cache);memset(slots,0,sizeof(slots));r.shader_cache.num_free=0;r.shader_cache.num_used=0;QTAILQ_INIT(&r.shader_cache.global);QTAILQ_INIT(&r.shader_cache.bins[42]);QTAILQ_INIT(&r.shader_module_cache.bins[42]);
  r.shader_binding=r.spec_binding=NULL;for(unsigned i=0;i<2;i++)lru_add_free(&r.shader_cache,&slots[i].node);
  pgraph_glsl_dynamic_gen=gen_case==0?0:gen_case==1?17:UINT32_MAX;
  r.spec_track[0].hash=42;r.spec_track[0].phase=marker;r.spec_track[1].phase=0; ShaderState state={0};state.geom.primitive_mode=geometry?PRIM_TYPE_TRIANGLES:PRIM_TYPE_POINTS;state.geom.polygon_front_mode=state.geom.polygon_back_mode=POLY_MODE_FILL;state.vsh.uniform_attrs=1;state.psh.combiner_control=7;state.psh.combiner_dynamic=cover;
  ShaderBinding *target=container_of(lru_lookup(&r.shader_cache,41,&state),ShaderBinding,node);
  state.psh.combiner_control++;
  ShaderBinding *live=container_of(lru_lookup(&r.shader_cache,42,&state),ShaderBinding,node);
  live->initialized=true;live->gl_pipeline=77;live->dyn_gen=pgraph_glsl_dynamic_gen;r.shader_binding=live;r.wide_lines=false;spec_binding_track(&r,target);CHECK(r.spec_binding==target&&target->dyn_gen==pgraph_glsl_dynamic_gen);if(!cover&&marker==3){CHECK(!spec_binding_track(&r,live));CHECK(r.spec_binding==target);}
  ShaderModuleCacheKey keys[3];bool gs=state_module_keys(&target->state,keys);bool ready=true;
  memset(modules,0,sizeof(modules));
  for(unsigned i=gs?0:1;i<3;i++){
   modules[i].node.hash=42;modules[i].key=keys[i];modules[i].gl_stage_program=10+i;
   if(!(mask&(1u<<i)))modules[i].locs_pending=true;
   if(mask&(1u<<(i+3)))modules[i].key.psh.state.combiner_control^=1;
   ready&=(mask&(1u<<i))&&!(mask&(1u<<(i+3)));
   QTAILQ_INSERT_TAIL(&r.shader_module_cache.bins[42],&modules[i].node,next_bin);
  }
  sso=boost=true;generated=restores=deleted=0;bound=77;
  if(scenario==1){lru_evict_node(&r.shader_cache,&target->node);CHECK(!r.spec_binding);memset(&target->state,0xa5,sizeof(target->state));}
  if(scenario==2){pgraph_gl_uniform_sources_invalidate(&r);CHECK(!r.spec_binding);memset(&target->state,0xa5,sizeof(target->state));}
  if(scenario==3)target->dyn_gen--;
  if(scenario==4)r.wide_lines=true;
  if(scenario==5)sso=false;
  if(scenario==6)boost=false;
  if(scenario==7){target->initialized=true;target->gl_pipeline=123;}
  if(scenario==8){r.shader_binding=NULL;bound=0;} if(scenario==9){modules[1].gl_stage_program=0;ready=false;}
  if(scenario!=1&&scenario!=2)CHECK(state_modules_ready(&r,&target->state)==ready);spec_binding_pump(&r);
  bool allowed=scenario==0||scenario==7||scenario==8||scenario==9;
  bool finished=allowed&&(ready||scenario==7);bool cancelled=!cover&&marker==3&&(!allowed||(!ready&&scenario!=7));
  CHECK((r.spec_track[0].phase==0)==((finished&&!cover)||cancelled));CHECK(generated==(finished&&scenario!=7));if(!allowed||cancelled)CHECK(!r.spec_binding);
  CHECK(pgraph_glsl_dynamic_gen==(gen_case==0?0:gen_case==1?17:UINT32_MAX));
  CHECK(!finished||!r.spec_binding);
  CHECK(finished||!allowed||cancelled||r.spec_binding==target);
  if(scenario!=2&&scenario!=8)CHECK(r.shader_binding==live);
  if(finished&&scenario!=8)CHECK((live->dyn_gen!=pgraph_glsl_dynamic_gen)==!cover);
  if(allowed&&!finished)CHECK(live->dyn_gen==pgraph_glsl_dynamic_gen);
  CHECK(bound==(scenario==8?0:77));
  unsigned old_generated=generated;spec_binding_pump(&r);CHECK(generated==old_generated);
  cases++;
 }
 puts("GL binding pump: native LRU eviction/poisoning, flush, generation wrap, host-option/geometry changes, exact stage readiness and localized draw wakeup PASS");printf("%u cases; retained payload snapshots=0, temporary query keys=%zu bytes\n",cases,3*sizeof(ShaderModuleCacheKey));
}
'''.replace('/* KEY */',key).replace('/* PRODUCTION */',parts)
with tempfile.TemporaryDirectory(prefix='gl-binding-pump-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),str(c),'-o',str(exe)]
 c.write_text(code);subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,timeout=120)
 mutants=[('completed marker never released',code.replace('r->spec_track[t].phase = 0;', '(void)t;',1)),('cover forces unnecessary wakeup',code.replace('r->shader_binding && !target->state.psh.combiner_dynamic','r->shader_binding',1)),('tracking loses generation',code.replace('target->dyn_gen = pgraph_glsl_dynamic_gen;', '(void)target;',1)),('tracking loses owner',code.replace('r->spec_binding = target;', '(void)r;',1)),('null stage',code.replace('module->gl_stage_program && !module->locs_pending','!module->locs_pending',1)),('unfinished stage',code.replace('module->gl_stage_program && !module->locs_pending','module->gl_stage_program',1)),('no localized wakeup',code.replace('r->shader_binding->dyn_gen = pgraph_glsl_dynamic_gen - 1;','(void)r;',1)),('lost live pipeline',code.replace('glBindProgramPipeline(r->shader_binding ?\n                              r->shader_binding->gl_pipeline : 0);','glBindProgramPipeline(123);',1)),('lend survives eviction',code.replace('if (r->spec_binding == binding) {','if (false && r->spec_binding == binding) {',1)),('lend survives load',code.replace('    r->shader_binding = NULL;\n    spec_binding_cancel(r);','    r->shader_binding = NULL;',1)),('stale family generation',code.replace('target->dyn_gen != pgraph_glsl_dynamic_gen ||','false ||',1)),('missing vertex readiness',code.replace('module_ready(r, &keys[1]) &&','true &&',1))]
 for name,mutant in mutants:
  assert mutant!=code,name
  c.write_text(mutant);subprocess.run(cmd,check=True)
  bad=subprocess.run([str(exe)],capture_output=True,timeout=120)
  assert bad.returncode==1 and b'FAIL GL binding pump' in bad.stderr,name
  print('Required negative control rejected:',name)
