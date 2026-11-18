#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Ready fragment adoption preserves the exact dequeued family key."""
from pathlib import Path
import os, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(path,name):
 s=path.read_text();a=s.index(name);a=s.rfind('\n',0,a)+1;b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
s=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
ready_path=r/'hw/xbox/nv2a/pgraph/gl/ready-psh.inc'
prepared=ready_path.is_file() and 'static bool ready_psh_adopt_gate(' in ready_path.read_text()
a=s.index('        /* Gated combiners must reach adoption even if cached. */')
a=s.rfind('        ShaderModuleCacheKey key;',0,a)
b=s.index('        fed |= gen_job_push(&key, gate, false);',a)+len('        fed |= gen_job_push(&key, gate, false);')
feed=s[a:b]
assert ('ready_psh_adopt_gate(r, &st)' in feed)==prepared
code=r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL ready gate line=%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {struct {bool ready;} ready_psh;} PGRAPHGLState;
static PshState original;
static unsigned notified,queued;
#define GL_FRAGMENT_SHADER 0x8b30u
typedef struct {unsigned kind;struct {PshState state;} psh;} ShaderModuleCacheKey;
static bool gen_job_push(const ShaderModuleCacheKey *key,bool gate,bool seed){CHECK(key->kind==GL_FRAGMENT_SHADER&&gate&&!seed);CHECK(!memcmp(&key->psh.state,&original,sizeof(original)));queued++;return true;}
void pgraph_glsl_psh_dynamic_ready(const PshState *state){CHECK(!memcmp(state,&original,sizeof(original)));notified++;}
/* PREDICATE */
/* ADOPTION */
static bool feed_gate(PGRAPHGLState *r,PshState st){
 (void)r;bool gate=true,fed=false;
 do {
/* FEED */
 }while(0);
 return fed;
}
int main(void){
 unsigned cases=0;
 for(unsigned available=0;available<2;available++)for(unsigned depth=0;depth<1024;depth++)for(unsigned mode=0;mode<32;mode++)for(unsigned smooth=0;smooth<2;smooth++){
  PGRAPHGLState renderer={{available}};
  PshState state={0};state.depth_format=depth;state.combiner_dynamic=true;state.alpha_dynamic=true;state.smooth_shading=smooth;
  state.shader_stage_program=mode;state.dim_tex[0]=2;state.surface_zeta_format=2;
  original=state;notified=queued=0;
  /* Independent domain for this single 2D stage: no UI clamp without PROJECT2D. */
  bool expected=PREPARED&&available&&mode<=5&&(!(depth&DEPTH_FORMAT_UI_CLAMP)||mode==1);
  CHECK(feed_gate(&renderer,state)==!expected);CHECK(queued==!expected);
  CHECK(notified==expected&&!memcmp(&state,&original,sizeof(state)));cases++;
 }
 printf("GL ready gate: %u ready/unavailable/legacy/projected/unsupported/exact-key cases PASS\n",cases);
}
""".replace('PREPARED','1' if prepared else '0').replace('/* PREDICATE */',function(r/'hw/xbox/nv2a/pgraph/glsl/psh-ready.inc','bool pgraph_glsl_psh_ready_controls(') if prepared else '').replace('/* ADOPTION */',function(ready_path,'static bool ready_psh_adopt_gate(') if prepared else '').replace('/* FEED */',feed)
with tempfile.TemporaryDirectory(prefix='gl-ready-gate-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,*(['-DXEMU_EXPERIMENTAL_READY_VSH'] if prepared else []),'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),str(c),'-o',str(exe)]
 c.write_text(code);subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,timeout=60)
 for name,old,new in ([('missing native readiness','if (!r->ready_psh.ready) return false;','(void)r;'),('missing projection','state->depth_format |= DEPTH_FORMAT_HELPER_LIVENESS;','(void)0;'),('wrong family key','state->depth_format = depth_format;','(void)depth_format;'),('missing notification','if (covered) pgraph_glsl_psh_dynamic_ready(state);','(void)0;'),('unsupported adoption','bool covered = pgraph_glsl_psh_ready_controls(state, controls);','bool covered = pgraph_glsl_psh_ready_controls(state, controls) || true;')] if prepared else [('missing legacy submission','fed |= gen_job_push(&key, gate, false);','if (false) fed |= gen_job_push(&key, gate, false);'),('wrong legacy key','key.psh.state = st;','key.psh.state = st;key.psh.state.depth_format ^= 1;')]):
  assert old in code,name;c.write_text(code.replace(old,new,1));subprocess.run(cmd,check=True)
  bad=subprocess.run([str(exe)],capture_output=True,timeout=60)
  assert bad.returncode==1 and b'FAIL ready gate' in bad.stderr,name
  print('Required negative control rejected:',name)
 print('Prepared adoption' if prepared else 'Legacy submission contract: ready adoption absent in this revision')
