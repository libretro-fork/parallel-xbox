# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check texture-format method equivalence, atomic work counts and CPU timing."""
from pathlib import Path
import os,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/pgraph.c').read_text();a=s.index('DEF_METHOD(NV097, SET_TEXTURE_FORMAT)');a=s.index('{',a);b=a+1;depth=1
while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
code=Path(__file__).with_suffix('.c').read_text().replace('/* CURRENT */',s[a:b]).replace('/* REFERENCE */',Path(__file__).with_name('texture-format-method-reference.h').read_text())
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='texture-format-method-') as tmp:
 root=Path(tmp);(root/'test.c').write_text(code)
 for counted in (True,False):
  exe=root/('test.exe' if os.name=='nt' else 'test')
  flags=['-DCOUNT_ACCESS','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if counted else []
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function',*flags,'-I'+str(repo),str(root/'test.c'),'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
