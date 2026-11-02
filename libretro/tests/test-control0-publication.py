# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check extracted CONTROL0 against its frozen pre-optimization method."""
from pathlib import Path
import os,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/pgraph.c').read_text();a=s.index('DEF_METHOD(NV097, SET_CONTROL0)');a=s.index('{',a);b=a+1;depth=1
while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
code=Path(__file__).with_suffix('.c').read_text().replace('/* CURRENT */',s[a:b]).replace('/* REFERENCE */',Path(__file__).with_name('control0-method-reference.h').read_text())
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='control0-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 for checked in [True,False]:
  exe=p/('test.exe' if os.name=='nt' else 'test')
  flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(repo),str(p/'test.c'),'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True)
