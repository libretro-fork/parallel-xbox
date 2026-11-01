# SPDX-License-Identifier: GPL-2.0-or-later
"""Production frame acquisition and fence-serial resize retirement."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(source,name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 a=m.start();b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
s=(r/'hw/xbox/nv2a/pgraph/vk/renderer.c').read_text();d=(r/'hw/xbox/nv2a/pgraph/vk/display.c').read_text()
code=Path(__file__).with_suffix('.c').read_text().replace('/* READER */','\n'.join(function(s,n) for n in ['nv2a_vk_libretro_get_frame','nv2a_vk_libretro_frame_presented']))
code=code.replace('/* RETIRE */',function(d,'frame_image_for_slot'))
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='vk-frame-handoff-',dir=r.parent) as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('VK_HANDOFF_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('VK_HANDOFF_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-DHAVE_THREADS','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-parameter',*flags,'-I'+str(r),'-I'+str(r/'libretro-common/include'),str(p/'test.c'),str(r/'libretro-common/rthreads/rthreads.c'),str(r/'libretro-common/compat/compat_strl.c'),'-o',binary]+(['-static'] if win else ['-pthread']),check=True)
 if not os.environ.get('VK_HANDOFF_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
