# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile actual main-loop waits and guest idle accounting against a scheduler model."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
parts=[]
for path,names in [('util/main-loop.c',['main_loop_wait_poll','main_loop_notified','main_loop_dispatch','main_loop_wait_internal','main_loop_wait','main_loop_wait_timeout']),('ui/libretro.c',['lr_wait_for_device'])]:
 s=(r/path).read_text()
 for name in names:
  m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\(',s,re.M);assert m,name
  a=m.start();b=s.index('{',a)+1;depth=1
  while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
  parts.append(s[a:b])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
code=code.replace('#include <assert.h>','#include <assert.h>\n#undef assert\n#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\\n",__LINE__,#x); _Exit(1); } } while(0)')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='gpu-wait-deadline-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
