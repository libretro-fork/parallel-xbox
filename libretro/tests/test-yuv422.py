# SPDX-License-Identifier: GPL-2.0-or-later
"""Production packed-YUV conversion parity, row bounds and optional CPU timing."""
from pathlib import Path
import os,re,subprocess,sys,tempfile,ctypes
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc';s=(r/'hw/xbox/nv2a/pgraph/util.h').read_text();parts=[]
for name in ['cliptobyte','convert_yuy2_to_rgb','convert_uyvy_to_rgb','pgraph_yuv422_store_rgba','pgraph_yuv422_to_rgba']:
 m=re.search(r'^(?:static\s+inline\s+)?(?:void|uint8_t) '+name+r'\(',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[a:b])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts));env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
with tempfile.TemporaryDirectory(prefix='yuv422-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 checks=[] if '--bench' in sys.argv[3:] else ['-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*checks,str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe),*sys.argv[3:]],check=True,env=env,timeout=120)
