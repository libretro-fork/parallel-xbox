# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check NV097 register-method equivalence, publication counts and CPU timing."""
from pathlib import Path
import os,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
source=(repo/'hw/xbox/nv2a/pgraph/pgraph.c').read_text()
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
methods=[('SET_TEXTURE_FORMAT','texture-format-method-reference.h',[]),
 ('SET_FOG_COLOR','fog-color-method-reference.h',['-DTEST_FOG_COLOR']),
 ('SET_LIGHT_CONTROL','light-control-method-reference.h',['-DTEST_LIGHT_CONTROL']),
 ('SET_COLOR_MATERIAL','color-material-method-reference.h',['-DTEST_COLOR_MATERIAL']),
 ('SET_TEXTURE_PALETTE','texture-palette-method-reference.h',['-DTEST_TEXTURE_PALETTE']),
 ('SET_TRANSFORM_EXECUTION_MODE','transform-mode-method-reference.h',['-DTEST_TRANSFORM_MODE']),
 ('SET_COLOR_MASK','color-mask-method-reference.h',['-DTEST_COLOR_MASK'])]
with tempfile.TemporaryDirectory(prefix='register-method-') as tmp:
 root=Path(tmp)
 for method,reference,defines in methods:
  a=source.index('DEF_METHOD(NV097, '+method+')');a=source.index('{',a);b=a+1;depth=1
  while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
  code=Path(__file__).with_suffix('.c').read_text().replace('/* CURRENT */',source[a:b]).replace('/* REFERENCE */',Path(__file__).with_name(reference).read_text())
  helper=''
  if method=='SET_COLOR_MASK':
   header=(repo/'hw/xbox/nv2a/pgraph/pgraph.h').read_text();lo=header.index('static inline bool pgraph_color_write_enabled(');hi=header.index('{',lo)+1;level=1
   while level:level+=(header[hi]=='{')-(header[hi]=='}');hi+=1
   helper=header[lo:hi]
  code=code.replace('/* HELPERS */',helper)
  (root/'test.c').write_text(code);print(method,flush=True)
  for counted in (True,False):
   exe=root/('test.exe' if os.name=='nt' else 'test')
   flags=['-DCOUNT_ACCESS','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if counted else []
   subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function',*defines,*flags,'-I'+str(repo),str(root/'test.c'),'-o',str(exe)],env=env,check=True)
   subprocess.run([str(exe)],env=env,check=True,timeout=120)
