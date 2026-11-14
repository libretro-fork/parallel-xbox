#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Native ready-cover reuse restores the unpublished PSH projection exactly."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text();psh=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text()
def fn(text,name):
 m=re.search(r'^(?:static )?(?:int|bool|void|ShaderBinding \*) *'+name+r'\([^;{]*\)\s*\{',text,re.M);assert m,name
 b=text.index('{',m.start())+1;depth=1
 while depth:depth+=(text[b]=='{')-(text[b]=='}');b+=1
 return text[m.start():b]
a=s.index('            ShaderBinding *cover = state_modules_ready');b=s.index('\n        } else {',a)
selection=s[a:b].replace('&state','state')
assert selection.index('spec_binding_track')<selection.index('r->spec_track[t].phase = 0;')
parts='\n'.join(fn(psh,n) for n in ('psh_zero_alpha_fields','psh_zero_combiner_fields','pgraph_glsl_psh_canonicalize_dynamic'))
parts+='\n'+fn(s,'spec_track_find')+'\n'+fn(s,'spec_track_adopted')+'\n'+fn(s,'spec_binding_reservation')+'\n'+fn(s,'spec_binding_cancel')+'\n'+fn(s,'spec_binding_track')+'\n'+fn(s,'ready_cover_binding')
body=fn(s,'ready_cover_binding')
for banned in ('ShaderState fb','malloc(','g_new','realloc(','glGet','glCreate','glGen','glUse','glBind','spec_queue('):assert banned not in body,banned
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qemu/lru.h"
#include <encodings/xxh3.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
uint64_t cpu_features_get(void){return 0;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL GL ready cover line=%d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define NV2A_PROF_SHADER_FALLBACK 1
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define SPEC_ADOPTED 2
#define SPEC_PIPE_RESERVED 3
typedef struct {LruNode node;ShaderState state;bool initialized;unsigned gl_pipeline,dyn_gen;} ShaderBinding;
typedef struct {Lru shader_cache;ShaderBinding *spec_binding;struct{uint64_t hash;int phase;}spec_track[1];bool wide_lines;} PGRAPHGLState;
static PGRAPHGLState r;static ShaderBinding slots[4];
static bool boost,probing,stages_ready;static unsigned inits,profiles;unsigned pgraph_glsl_dynamic_gen=17;
static bool pgraph_gpu_boost_gl(void){return boost;}
static void nv2a_profile_inc_counter(unsigned n){CHECK(n==1);profiles++;}
static bool state_modules_ready(PGRAPHGLState *r,const ShaderState *state){(void)r;(void)state;return stages_ready;}
/* PRODUCTION */
static void choose(PGRAPHGLState *r,ShaderState *state,ShaderBinding **selected,int t){ShaderBinding *binding=*selected;uint64_t ph=xxh3_64((const uint8_t *)&state->psh,sizeof(state->psh), 0);
/* SELECTION */
 *selected=binding;
}
static void init(Lru *cache,LruNode *node,const void *key){(void)cache;CHECK(!probing);ShaderBinding *b=container_of(node,ShaderBinding,node);memcpy(&b->state,key,sizeof(b->state));b->initialized=false;b->gl_pipeline=0;inits++;}
static bool compare(Lru *cache,LruNode *node,const void *key){(void)cache;return memcmp(&container_of(node,ShaderBinding,node)->state,key,sizeof(ShaderState));}
static ShaderBinding *publish(const ShaderState *key,uint64_t hash,bool initialized,unsigned pipeline){ShaderBinding *b=container_of(lru_lookup(&r.shader_cache,hash,key),ShaderBinding,node);b->initialized=initialized;b->gl_pipeline=pipeline;return b;}
int main(void){
 lru_init(&r.shader_cache);r.shader_cache.init_node=init;r.shader_cache.compare_nodes=compare;for(unsigned i=0;i<4;i++)lru_add_free(&r.shader_cache,&slots[i].node);
 unsigned cases=0;uint32_t random=127;
 for(unsigned trial=0;trial<256;trial++)for(unsigned mode=0;mode<13;mode++)for(unsigned flags=0;flags<4;flags++){
  probing=false;lru_flush(&r.shader_cache);r.spec_binding=NULL;r.spec_track[0].phase=2;profiles=0;r.wide_lines=false;stages_ready=mode!=12;
  ShaderState state,original,canonical,bad;unsigned char *bytes=(unsigned char *)&state;
  for(unsigned i=0;i<sizeof(state);i++){random=random*1664525u+1013904223u;bytes[i]=random>>24;}
  state.geom.wide_lines=false;state.psh.combiner_dynamic=!!(flags&1);boost=!(flags&2);memcpy(&original,&state,sizeof(state));memcpy(&canonical,&state,sizeof(state));
  bool can=pgraph_glsl_psh_canonicalize_dynamic(&canonical.psh);
  uint64_t hash=xxh3_64((const uint8_t *)&canonical,sizeof(canonical), 0);r.spec_track[0].hash=xxh3_64((const uint8_t *)&original.psh,sizeof(original.psh), 0);
  ShaderBinding *exact=publish(&state,xxh3_64((const uint8_t *)&state,sizeof(state), 0),false,0),*entry=NULL;
  if(mode==1||mode==2||mode==3||mode==6||mode>=8)entry=publish(&canonical,hash,mode!=2,mode==3?0:77);
  if(mode==4||mode==6||mode==7){memcpy(&bad,&canonical,sizeof(bad));((unsigned char *)&bad)[mode==7?0:sizeof(bad)-1]^=1;publish(&bad,hash,true,88);}
  if(mode==5)entry=publish(&canonical,hash^LRU_NUM_BINS,true,77);
  ShaderBinding *busy=NULL;if(mode==8||mode==9||mode==11){memcpy(&bad,&original,sizeof(bad));((unsigned char *)&bad)[0]^=1;if(mode==11)bad.geom.wide_lines=true;busy=publish(&bad,xxh3_64((const uint8_t *)&bad,sizeof(bad), 0),false,0);busy->dyn_gen=pgraph_glsl_dynamic_gen-(mode==9);r.spec_binding=busy;}if(mode==10){busy=entry;busy->dyn_gen=pgraph_glsl_dynamic_gen;r.spec_binding=busy;} if(mode==8||mode==9||mode==11)r.spec_track[0].phase=3;int phase_before=r.spec_track[0].phase;unsigned saved_inits=inits;bool hit=stages_ready&&can&&(mode==1||mode==6||mode>=8)&&mode!=8;ShaderBinding *selected=exact;
  probing=true;choose(&r,&state,&selected,0);probing=false;
  CHECK(!memcmp(&state,&original,sizeof(state))&&inits==saved_inits);
  CHECK(selected==(hit?entry:exact));ShaderBinding *expected_pending=hit?exact:busy;uint64_t ph=xxh3_64((const uint8_t *)&original.psh,sizeof(original.psh), 0);CHECK(r.spec_track[0].phase==(hit?3:((expected_pending&&xxh3_64((const uint8_t *)&expected_pending->state.psh,sizeof(original.psh), 0)==ph)?phase_before:0)));CHECK(profiles==hit);
  CHECK(r.spec_binding==expected_pending);if(hit)CHECK(exact->dyn_gen==pgraph_glsl_dynamic_gen);
  if(hit){spec_track_adopted(&r,&original.psh);CHECK(r.spec_track[0].phase==3);CHECK(QTAILQ_FIRST(&r.shader_cache.global)==&entry->node);CHECK(QTAILQ_FIRST(&r.shader_cache.bins[lru_hash_to_bin(&r.shader_cache,hash)])==&entry->node);
   selected=exact;probing=true;choose(&r,&state,&selected,0);probing=false; /* Repeated adopted checks retain the existing marker. */
   CHECK(r.spec_track[0].phase==3&&inits==saved_inits&&r.spec_binding==exact);
   memset(&state,0xa5,sizeof(state));CHECK(!memcmp(&entry->state,&canonical,sizeof(canonical)));
  }
  cases++;
 }
 printf("GL ready cover: %u native-LRU/exact-hash/collision/readiness/projection-padding/marker/owner cases PASS; PSH temporary=%zu, no vertex/geometry snapshot\n",cases,sizeof(PshState));
}
'''.replace('/* PRODUCTION */',parts).replace('/* SELECTION */',selection)
with tempfile.TemporaryDirectory(prefix='gl-ready-cover-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),str(c),str(r/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)]
 c.write_text(code);subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,timeout=120)
 mutants=[('adoption erases reservation',code.replace('i >= 0 && r->spec_track[i].phase != SPEC_PIPE_RESERVED','i >= 0',1)),('pending exact stages deferred',code.replace('state_modules_ready(r, state) ?', '(state_modules_ready(r, state) || true) ?',1)),('exact reservation displaced',code.replace('pending && pending != target && spec_binding_reservation(r) >= 0','false && pending && pending != target && spec_binding_reservation(r) >= 0',1)),('projection not restored',code.replace('memcpy(&owned_state->psh, &exact, sizeof(exact));','(void)exact;',1)),('hash-only identity',code.replace('!memcmp(&entry->state, owned_state, sizeof(*owned_state))','true',1)),('unfinished binding',code.replace('node->hash == hash && entry->initialized &&','node->hash == hash &&',1)),('null pipeline',code.replace('entry->gl_pipeline &&','true &&',1)),('premature marker release',code.replace('r->spec_track[t].phase = SPEC_PIPE_RESERVED;','r->spec_track[t].phase = 0;',1)),('no exact recipe lend',code.replace('if (cover && spec_binding_track(r, binding)) {','if (cover && (spec_binding_track(r, binding),r->spec_binding=NULL,true)) {',1))]
 for name,mutant in mutants:
  assert mutant!=code,name
  c.write_text(mutant);subprocess.run(cmd,check=True)
  bad=subprocess.run([str(exe)],capture_output=True,timeout=120)
  assert bad.returncode==1 and b'FAIL GL ready cover' in bad.stderr,name
  print('Required negative control rejected:',name)
