# SPDX-License-Identifier: GPL-2.0-or-later
"""Production immutable GL display mailbox with real libretro atomics/threads."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/gl/display.c').read_text()
def function(name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
names=['display_slot_reuse','disp_post_release','disp_post_ticket','display_front_release','disp_take_post','pgraph_gl_get_framebuffer_surface']
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(function(n) for n in names))
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='gl-display-mailbox-',dir=r.parent) as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('GL_MAILBOX_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('GL_MAILBOX_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-DHAVE_THREADS','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,'-I'+str(r/'libretro-common/include'),str(p/'test.c'),str(r/'libretro-common/rthreads/rthreads.c'),str(r/'libretro-common/compat/compat_strl.c'),'-o',binary]+(['-static'] if win else ['-pthread']),check=True)
 if not os.environ.get('GL_MAILBOX_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
