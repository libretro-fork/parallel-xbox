# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise production guest MMIO wakes and GPU progress accounting without game assets."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
parts=[]
for path,names in [('hw/xbox/nv2a/pfifo.c',['pfifo_kick','pfifo_kick_guest','pfifo_write']),('hw/xbox/nv2a/user.c',['user_write']),('hw/xbox/nv2a/nv2a.c',['nv2a_libretro_guest_busy']),('hw/xbox/nv2a/pgraph/pgraph.c',['pgraph_write']),('hw/xbox/nv2a/pcrtc.c',['pcrtc_write']),('ui/libretro.c',['lr_devices_busy','lr_quiet_exit'])]:
 s=(r/path).read_text()
 for name in names:
  m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\(',s,re.M);assert m,name
  a=m.start();b=s.index('{',a)+1;depth=1
  while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
  parts.append(s[a:b])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
# A regression reports a normal failing process, never a Windows crash dialog.
code=code.replace('#include <assert.h>','#include <assert.h>\n#undef assert\n#define assert(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\\n",__LINE__,#x); _Exit(1); } } while(0)')
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='nv2a-guest-wakes-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-I'+str(r),str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
