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
assert '#ifdef XEMU_EXPERIMENTAL_READY_VSH\n        if (ready_psh_adopt_gate(r, &st)) {\n            continue;\n        }\n#endif\n        fed |= gen_job_push(&key, gate, false);' in s
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
static PshState *lent;
static unsigned notified;
void pgraph_glsl_psh_dynamic_ready(const PshState *state){CHECK(state==lent);CHECK(!memcmp(state,&original,sizeof(original)));notified++;}
/* PREDICATE */
/* ADOPTION */
int main(void){
 unsigned cases=0;
 for(unsigned available=0;available<2;available++)for(unsigned depth=0;depth<1024;depth++)for(unsigned mode=0;mode<32;mode++)for(unsigned smooth=0;smooth<2;smooth++){
  PGRAPHGLState renderer={{available}};
  PshState state={0};state.depth_format=depth;state.combiner_dynamic=true;state.alpha_dynamic=true;state.smooth_shading=smooth;
  state.shader_stage_program=mode;state.dim_tex[0]=2;state.surface_zeta_format=2;
  original=state;lent=&state;notified=0;
  /* Independent domain for this single 2D stage: no UI clamp without PROJECT2D. */
  bool expected=available&&mode<=5&&(!(depth&DEPTH_FORMAT_UI_CLAMP)||mode==1);
  CHECK(ready_psh_adopt_gate(&renderer,&state)==expected);
  CHECK(notified==expected&&!memcmp(&state,&original,sizeof(state)));cases++;
 }
 printf("GL ready gate: %u ready/unavailable/legacy/projected/unsupported/exact-key cases PASS\n",cases);
}
""".replace('/* PREDICATE */',function(r/'hw/xbox/nv2a/pgraph/glsl/psh-ready.inc','bool pgraph_glsl_psh_ready_controls(')).replace('/* ADOPTION */',function(r/'hw/xbox/nv2a/pgraph/gl/ready-psh.inc','static bool ready_psh_adopt_gate('))
with tempfile.TemporaryDirectory(prefix='gl-ready-gate-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),str(c),'-o',str(exe)]
 c.write_text(code);subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True,timeout=60)
 for name,old,new in [('missing native readiness','if (!r->ready_psh.ready) return false;','(void)r;'),('missing projection','state->depth_format |= DEPTH_FORMAT_HELPER_LIVENESS;','(void)0;'),('wrong family key','state->depth_format = depth_format;','(void)depth_format;'),('missing notification','if (covered) pgraph_glsl_psh_dynamic_ready(state);','(void)0;'),('unsupported adoption','bool covered = pgraph_glsl_psh_ready_controls(state, controls);','bool covered = pgraph_glsl_psh_ready_controls(state, controls) || true;')]:
  assert old in code,name;c.write_text(code.replace(old,new,1));subprocess.run(cmd,check=True)
  bad=subprocess.run([str(exe)],capture_output=True,timeout=60)
  assert bad.returncode==1 and b'FAIL ready gate' in bad.stderr,name
  print('Required negative control rejected:',name)
