#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the draw's fallback branch; preserve exact queue keys and padding."""
from pathlib import Path
import os, re, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
p=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text()
def function(text,name):
 m=re.search(r'^(?:static )?(?:bool|void) '+name+r'\([^;{]*\)\s*\{',text,re.M);assert m,name
 end=text.index('{',m.start())+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]
a=s.index('            /* Project only our unpublished PSH metadata.')
b=s.index('\n        }\n    }\n\n    if (!binding->initialized',a)
branch=s[a:b]
assert 'ShaderState ' not in branch and '&fb' not in branch
parts='\n'.join(function(p,n) for n in ('psh_zero_alpha_fields','psh_zero_combiner_fields','pgraph_glsl_psh_canonicalize_dynamic'))
code=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#include "qemu/fast-hash.h"
uint64_t cpu_features_get(void){return 0;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"projection FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define NV2A_PROF_SHADER_FALLBACK 1
#define SPEC_ADOPTED 2
typedef struct{unsigned unused;}LruNode;
typedef struct{LruNode node;ShaderState state;bool initialized;}ShaderBinding;
typedef struct{unsigned shader_cache;struct{int phase;}spec_track[1];}PGRAPHGLState;
static ShaderState exact,canonical;static ShaderBinding fallback,real;
static bool boost,known;static int marker;static unsigned lookups,queues,gens,tracks,profiles;
static uint64_t ph,sh;static bool pgraph_gpu_boost_gl(void){return boost;}
/* PARTS */
static LruNode *lru_lookup(unsigned *cache,uint64_t hash,const void *key){(void)cache;CHECK(hash==fast_hash((const uint8_t *)&canonical,sizeof(canonical)));CHECK(!memcmp(key,&canonical,sizeof(canonical)));lookups++;return &fallback.node;}
static int spec_track_find(PGRAPHGLState *r,uint64_t hash){(void)r;CHECK(hash==sh);return marker;}
static bool state_modules_known(PGRAPHGLState *r,const ShaderState *state){(void)r;CHECK(!memcmp(state,&canonical,sizeof(*state)));return known;}
static void generate_shaders(PGRAPHGLState *r,ShaderBinding *b){(void)r;CHECK(b==&fallback);b->initialized=true;gens++;}
static void spec_queue(PGRAPHGLState *r,uint64_t hash,const ShaderState *state){(void)r;CHECK(hash==ph||hash==sh);CHECK(!memcmp(state,hash==ph?&exact:&canonical,sizeof(*state)));queues++;}
static bool spec_binding_track(PGRAPHGLState *r,ShaderBinding *b){(void)r;CHECK(b==&real||b==&fallback);tracks++;return true;}
static void nv2a_profile_inc_counter(unsigned n){CHECK(n==1);profiles++;}
static void choose(PGRAPHGLState *r,ShaderState *input,int t){
 ShaderState state;memcpy(&state,input,sizeof(state));ShaderBinding *binding=&real;
 /* BRANCH */
 CHECK(!memcmp(&state,input,sizeof(state)));
 CHECK(binding==(fallback.initialized&&lookups?&fallback:&real));
}
int main(void){uint32_t random=41;unsigned cases=0;
 for(unsigned trial=0;trial<64;trial++)for(unsigned mode=0;mode<64;mode++){
  unsigned char *bytes=(unsigned char *)&exact;for(unsigned i=0;i<sizeof(exact);i++){random=random*1664525u+1013904223u;bytes[i]=random>>24;}
  exact.psh.combiner_dynamic=!!(mode&1);boost=!(mode&2);known=!!(mode&4);fallback.initialized=!!(mode&8);bool initial=fallback.initialized;
  marker=(mode&16)?0:-1;int t=(mode&32)?0:-1;PGRAPHGLState r={0};r.spec_track[0].phase=SPEC_ADOPTED;
  memcpy(&canonical,&exact,sizeof(exact));bool can=pgraph_glsl_psh_canonicalize_dynamic(&canonical.psh);
  ph=fast_hash((const uint8_t *)&exact.psh,sizeof(exact.psh));sh=fast_hash((const uint8_t *)&canonical.psh,sizeof(canonical.psh));
  lookups=queues=gens=tracks=profiles=0;choose(&r,&exact,t);
  bool made=can&&!initial&&marker>=0&&known;bool ready=can&&(initial||made);
  CHECK(lookups==can&&gens==made&&profiles==ready);
  CHECK(queues==(unsigned)(can&&!initial&&marker<0)+(unsigned)(ready&&t<0));
  CHECK(tracks==(unsigned)(can&&!ready)+(unsigned)ready);
  CHECK(r.spec_track[0].phase==(made?0:SPEC_ADOPTED));cases++;
 }
 printf("GL fallback projection: %u exact/canonical queue, readiness and padding cases PASS; PSH=%zu full-state=%zu\n",cases,sizeof(PshState),sizeof(ShaderState));
}
'''.replace('/* PARTS */',parts).replace('/* BRANCH */',branch)
with tempfile.TemporaryDirectory(prefix='gl-cover-projection-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),str(c),str(r/'util/fast-hash.c'),str(r/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)]
 c.write_text(code);subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
 for name,mutant in [('missing early restore',code.replace('memcpy(&state.psh, &exact_psh, sizeof(exact_psh));','(void)exact_psh;',1)),('wrong exact queue',code.replace('spec_queue(r, ph, &state);','spec_queue(r, ph, &canonical);',1))]:
  assert mutant!=code,name;c.write_text(mutant);subprocess.run(cmd,check=True)
  assert subprocess.run([str(exe)],capture_output=True).returncode!=0,name
 print('GL fallback projection mutations rejected')
