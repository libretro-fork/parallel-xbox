# SPDX-License-Identifier: GPL-2.0-or-later
"""Production compressed volume CPU copy cases and frozen-baseline timing."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();s=(r/'hw/xbox/nv2a/pgraph/vk/texture.c').read_text()
def fn(name):
 m=re.search(r'^static (?:uint8_t \*|bool |void )'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
parts=[re.search(r'typedef struct TextureLevel \{.*?\} TextureLevel;',s,re.S)[0]]
parts += [fn(n) for n in ['stage_level','level_from_vram','stage_blocks_3d']]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
# Model production: copy bodies inline into callers that know BC is 8/16 bytes.
code=code.replace('static void stage_blocks_3d(', 'static inline __attribute__((always_inline)) void stage_blocks_3d(')
with tempfile.TemporaryDirectory(prefix='volume-block-copy-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
 win='mingw' in cc or os.name=='nt';binary=os.environ.get('VOLUME_COPY_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('VOLUME_COPY_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare',*flags,'-I'+str(r/'deps/vulkan-headers'),str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('VOLUME_COPY_SKIP_RUN'):subprocess.run([binary,*sys.argv[3:]],check=True,timeout=120)
