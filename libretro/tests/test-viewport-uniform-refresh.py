# SPDX-License-Identifier: LGPL-2.1-or-later
"""Offline viewport candidate through production refresh/cache; no renderer edits.
Only the vertex-constant uniform is bound. Other uniform values are omitted.
Usage: python test-viewport-uniform-refresh.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
def block(s,marker,decl=False):
 a=s.index(marker);b=s.index('{',a);e=b+1;depth=1
 while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a if decl else b:e]
s=(repo/'hw/xbox/nv2a/pgraph/pgraph.c').read_text();methods=''
for name,row in [('OFFSET','VPOFF'),('SCALE','VPSCL')]:
 body=block(s,'DEF_METHOD_INC(NV097, SET_VIEWPORT_'+name+')')
 row='NV_IGRAPH_XF_XFCTX_'+row
 assignment=f'    pg->vsh_constants[{row}][slot] = parameter;'
 candidate=body.replace(assignment,f'    pg->vsh_constants_dirty[{row}] |= pg->vsh_constants[{row}][slot] != parameter;\n'+assignment).replace(f'    pg->vsh_constants_dirty[{row}] = true;','')
 for variant,text in [('baseline',body),('candidate',candidate)]:
  methods+=f'__attribute__((noinline)) static void {variant}_{name}(PGRAPHState *pg,int method,uint32_t parameter) {text}\n'
s=(repo/'hw/xbox/nv2a/pgraph/glsl/uniform-cache.c').read_text()
a=s.index('const int8_t pgraph_vsh_uniform_section');b=s.index('};',a)+2
functions='\n'.join(block(s,m,True) for m in ['void pgraph_vsh_uniform_source_refresh(','bool uniform_cache_apply('])
vsh=(repo/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
code=Path(__file__).with_suffix('.c').read_text()
for marker,value in [('/* METHODS */',methods),('/* MAP */',s[a:b]),('/* FUNCTIONS */',functions),('/* COPY */',block(vsh,'if (locs[VshUniform_c] != -1)',True))]:code=code.replace(marker,value)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='viewport-uniform-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 for checked in [True,False]:
  exe=p/('test.exe' if os.name=='nt' else 'test')
  flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,str(p/'test.c'),'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
