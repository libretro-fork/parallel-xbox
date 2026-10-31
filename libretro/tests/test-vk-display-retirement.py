# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run production display fence retirement against a delayed same-queue model."""
from pathlib import Path
import ctypes, os, re, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(r/'hw/xbox/nv2a/pgraph/vk/display.c').read_text();parts=[]
for name in ['wait_frame_sync','pgraph_vk_poll_display_retired']:
 m=re.search(r'^(?:static )?void '+name+r'\(',s,re.M);assert m
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[a:b])
q=(r/'libretro/tests/test-spv-geom.c').read_text();a=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=q.index('int line_tests(',a)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts)).replace('/* QUIET ERRORS */',q[a:b])
if os.name=='nt':
 k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='display-retirement-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
