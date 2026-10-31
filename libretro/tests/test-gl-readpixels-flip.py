# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual row flip and GL readpixel wrapper with an independent pixel/stride oracle."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();s=(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/common.c').read_text();parts=[]
for name in ('glo_flip_rows_portable','glo_flip_rows_avx2','glo_flip_rows','glo_readpixels'):
 m=re.search(r'^(?:static )?void '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 if name=='glo_flip_rows_avx2':a=s.rfind('__attribute__',0,a)
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 parts.append(("#ifdef GLO_HAVE_AVX2_SWAP\n"+s[a:b]+"\n#endif") if name=="glo_flip_rows_avx2" else s[a:b])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts).replace('static void glo_flip_rows','static __attribute__((noinline)) void glo_flip_rows'))
with tempfile.TemporaryDirectory(prefix='gl-flip-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('GL_FLIP_BINARY',str(p/'test'))
 subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r/'libretro-common/include'),*shlex.split(os.environ.get('GL_FLIP_CFLAGS','-fsanitize=address,undefined')),str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('GL_FLIP_SKIP_RUN'):
  subprocess.run([binary,*(['bench'] if os.environ.get('GL_FLIP_BENCH') else [])],check=True,timeout=60)
  if not os.environ.get('GL_FLIP_BENCH'):subprocess.run([binary],env=dict(os.environ,GL_FLIP_PORTABLE='1'),check=True,timeout=60)
