# SPDX-License-Identifier: GPL-2.0-or-later
"""Production pitched texture staging, byte-fetch bounds and GPU copy regions."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/texture.c').read_text()
parts=[re.search(r'typedef struct TextureLevel \{.*?\} TextureLevel;',s,re.S)[0]]
for name in ['stage_level','level_from_vram','stage_linear_2d','texture_level_copy']:
 a=s.rfind('\nstatic ',0,s.index(name+'('))+1;b=s.index('{',s.index(name+'('))+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[a:b])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
with tempfile.TemporaryDirectory(prefix='linear-staging-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=60)
