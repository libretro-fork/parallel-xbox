# SPDX-License-Identifier: LGPL-2.1-or-later
"""Execute production staging copies and memory-property refresh with visibility spies."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
parts=[]
for path,names in [('buffer.c',['create_buffer','destroy_buffer','recreate_buffer']),('draw.c',['sync_staging_buffer'])]:
 s=(r/'hw/xbox/nv2a/pgraph/vk'/path).read_text()
 for name in names:
  m=re.search(r'^static void '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name;a=m.start();b=s.index('{',a)+1;depth=1
  while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
  parts.append(s[a:b])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts));q=(r/'libretro/tests/test-spv-geom.c').read_text();a=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=q.index('int line_tests(',a);code=code.replace('/* QUIET ERRORS */',q[a:b])
with tempfile.TemporaryDirectory(prefix='vk-staging-visibility-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r/'deps/vulkan-headers'),str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,timeout=30,env=env)
