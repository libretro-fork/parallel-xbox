# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile actual GL fallback/readback functions and swizzler against a byte oracle."""
from pathlib import Path
import re,os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();s=(r/'hw/xbox/nv2a/pgraph/gl/surface.c').read_text();parts=[]
for name in ('surface_copy_shrink_row','surface_download_to_buffer','surface_download_harvest'):
 m=re.search(r'^static (?:void|bool) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(s[a:b])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='gl-readback-fused-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('FUSED_READBACK_BINARY',str(p/'test'))
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare',*shlex.split(os.environ.get('FUSED_READBACK_CFLAGS','-fsanitize=address,undefined')),'-I'+str(r/'hw/xbox/nv2a/pgraph'),str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/swizzle.c'),'-o',binary],check=True)
 if not os.environ.get('FUSED_READBACK_SKIP_RUN'):subprocess.run([binary],check=True,timeout=60)
