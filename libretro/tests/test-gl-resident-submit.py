#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Ready resident stages suppress duplicate links without shortening job lends."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
h=(r/'hw/xbox/nv2a/pgraph/gl/renderer.h').read_text()
def function(name):
 a=s.index('static ',s.index(name)-20);a=s.rfind('\n',0,a)+1
 b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
key=re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;',h,re.S)[0]
records=s[s.index('#define GEN_JOBS 16'):s.index('static retro_eventcount_t gen_ready;')]
a=s.index('static struct {',s.index('/* Links in flight'))
async_records=s[a:s.index('} comb_async[32];',a)+len('} comb_async[32];')]
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/qemu/queue.h"
typedef unsigned GLenum;typedef unsigned GLuint;
#define GL_VERTEX_SHADER 0x8b31u
#define GL_FRAGMENT_SHADER 0x8b30u
#define GL_GEOMETRY_SHADER 0x8dd9u
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL resident submit line=%d: %s\n",__LINE__,#x);exit(1);}}while(0)
/* KEY */
struct MString {char text[2];};
/* RECORDS */
/* ASYNC */
typedef struct LruNode {QTAILQ_ENTRY(LruNode) next_bin;uint64_t hash;} LruNode;
typedef struct {QTAILQ_HEAD(,LruNode) bins[1];} Lru;
typedef struct {LruNode node;ShaderModuleCacheKey key;GLuint gl_stage_program;bool locs_pending;} ShaderModuleCacheEntry;
typedef struct {Lru shader_module_cache;} PGRAPHGLState;
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
static uint64_t fast_hash(const uint8_t *key,size_t n){(void)key;CHECK(n==sizeof(ShaderModuleCacheKey));return 42;}
static unsigned lru_hash_to_bin(Lru *cache,uint64_t hash){(void)cache;(void)hash;return 0;}
static unsigned active,compiled,unrefs,psh_ready,vsh_ready,spec_ready,released;
static ShaderModuleCacheKey expected;
static void owner_live(void){CHECK(gen_jobs[active].used&&gen_jobs[active].state==GEN_COMPLETE&&gen_jobs[active].code);CHECK(!memcmp(&gen_jobs[active].key,&expected,sizeof(expected)));}
static const char *mstring_get_str(MString *code){owner_live();CHECK(code==gen_jobs[active].code);return code->text;}
static void mstring_unref(MString *code){owner_live();CHECK(code==gen_jobs[active].code);code->text[0]='!';unrefs++;}
static GLuint pgraph_gl_separable_program(GLenum kind,const char *src,GLuint *shader){owner_live();CHECK(kind==expected.kind&&src==gen_jobs[active].code->text&&src[0]=='x');compiled++;*shader=77;return 88;}
void pgraph_glsl_psh_dynamic_ready(const PshState *state){owner_live();CHECK(state==&gen_jobs[active].key.psh.state);psh_ready++;}
void pgraph_glsl_vsh_dynamic_ready(const VshState *state){owner_live();CHECK(state==&gen_jobs[active].key.vsh.state);vsh_ready++;}
static void spec_track_adopted(PGRAPHGLState *r,const PshState *state){(void)r;owner_live();CHECK(state==&gen_jobs[active].key.psh.state);spec_ready++;}
static void fixture_store(retro_atomic_int_t *state,int value){CHECK(state==&gen_jobs[active].state&&value==GEN_IDLE&&!gen_jobs[active].used&&!gen_jobs[active].code&&unrefs==1);*state=value;memset(&gen_jobs[active].key,0xa5,sizeof(expected));released++;}
/* FUNCTIONS */
int main(void){
 unsigned cases=0;const GLenum kinds[]={GL_VERTEX_SHADER,GL_FRAGMENT_SHADER,GL_GEOMETRY_SHADER};
 for(unsigned i=0;i<GEN_JOBS;i++)for(unsigned slot=0;slot<32;slot++)for(unsigned kind=0;kind<3;kind++)for(unsigned flags=0;flags<4;flags++)for(unsigned mode=0;mode<5;mode++)for(unsigned empty=0;empty<2;empty++){
  PGRAPHGLState r={0};QTAILQ_INIT(&r.shader_module_cache.bins[0]);
  memset(gen_jobs,0,sizeof(gen_jobs));memset(comb_async,0,sizeof(comb_async));memset(&expected,0,sizeof(expected));
  expected.kind=kinds[kind];expected.psh.state.combiner_control=123;
  active=i;compiled=unrefs=psh_ready=vsh_ready=spec_ready=released=0;
  MString code={{empty?'\0':'x','\0'}};gen_jobs[i].key=expected;gen_jobs[i].used=true;gen_jobs[i].state=GEN_COMPLETE;gen_jobs[i].code=&code;gen_jobs[i].gate=flags&1;gen_jobs[i].seed=flags&2;
  ShaderModuleCacheEntry module={0};module.node.hash=42;module.key=expected;module.gl_stage_program=99;
  if(mode==2)module.locs_pending=true;if(mode==3)module.gl_stage_program=0;if(mode==4)module.key.psh.state.combiner_control++;
  if(mode)QTAILQ_INSERT_TAIL(&r.shader_module_cache.bins[0],&module.node,next_bin);
  comb_async_submit(&r,i,slot);
  CHECK(unrefs==1&&released==1&&!gen_jobs[i].used&&!gen_jobs[i].code&&gen_jobs[i].state==GEN_IDLE);
  bool ready=mode==1,notify=ready&&!(flags&2),submit=!ready&&!empty;
  CHECK(compiled==submit&&comb_async[slot].used==submit);
  CHECK(psh_ready==(notify&&kind==1&&(flags&1)));
  CHECK(spec_ready==(notify&&kind==1&&!(flags&1)));
  CHECK(vsh_ready==(notify&&kind==0));
  if(submit){CHECK(!memcmp(&comb_async[slot].key,&expected,sizeof(expected)));CHECK(comb_async[slot].gl_shader==77&&comb_async[slot].gl_program==88&&comb_async[slot].gate==!!(flags&1)&&comb_async[slot].seed==!!(flags&2));}
  for(unsigned j=0;j<32;j++)if(j!=slot)CHECK(!comb_async[j].used);
  cases++;
 }
 printf("GL resident submit: %u exact/collision/pending/null/seed/gate/empty/slot/lifetime cases PASS; key=%zu bytes, ready-hit key copies=0, submitted key copies=1\n",cases,sizeof(expected));
}
'''.replace('/* KEY */',key).replace('/* RECORDS */',records).replace('/* ASYNC */',async_records).replace('/* FUNCTIONS */','\n'.join(function(n).replace('retro_atomic_store_relaxed_int(', 'fixture_store(') for n in ('module_ready(','comb_async_adopted(','comb_async_submit(')))
with tempfile.TemporaryDirectory(prefix='gl-resident-submit-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'libretro-common/include'),str(c),'-o',str(exe)]
 c.write_text(code);subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,timeout=60)
 mutants=[('unfinished resident link',code.replace('module->gl_stage_program && !module->locs_pending','module->gl_stage_program',1)),('null program',code.replace('module->gl_stage_program && !module->locs_pending','!module->locs_pending',1)),('hash-only identity',code.replace('node->hash == hash && !memcmp(&module->key, key, sizeof(*key))','node->hash == hash',1)),('early job retirement',code.replace('MString *code = gen_jobs[i].code;','MString *code = gen_jobs[i].code; gen_jobs[i].used = false;',1)),('missing adoption',code.replace('comb_async_adopted(r, key, gen_jobs[i].gate, gen_jobs[i].seed);','(void)r;',1)),('seed changes live family',code.replace('if (seed) {','if (false && seed) {',1))]
 for name,mutant in mutants:
  assert mutant!=code,name
  c.write_text(mutant);subprocess.run(cmd,check=True)
  bad=subprocess.run([str(exe)],capture_output=True,timeout=60)
  assert bad.returncode==1 and b'FAIL resident submit' in bad.stderr,name
  print('Required negative control rejected:',name)


