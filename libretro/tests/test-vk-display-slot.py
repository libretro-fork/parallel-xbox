# SPDX-License-Identifier: GPL-2.0-or-later
"""Production nonblocking display-slot acquisition and cached redraw retry."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/display.c').read_text()
def function(name):
 m=re.search(r'^static bool '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
a=s.index('static void render_display(');b=s.index('    struct PGRAPHVkScanoutMode *mode =',a)
prefix=s[s.index('    unsigned slot = 0;',a):b]
assert 'wait_frame_sync(' not in s[a:s.index('static void create_surface_sampler',a)]
assert b<s.index('pgraph_vk_finish(',a) and b<s.index('pgraph_vk_upload_surface_data(',a)
a=s.index('    if (!disp->retry_pending &&');b=s.index(') {',a)
condition=s[a+len('    if ('):b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* ACQUIRE */',function('try_frame_sync')).replace('/* PREFIX */',prefix).replace('/* CACHE */',condition)
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='vk-display-slot-',dir=r.parent) as d:
 p=Path(d);(p/'test.c').write_text(code);binary=os.environ.get('DISPLAY_SLOT_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('DISPLAY_SLOT_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('DISPLAY_SLOT_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
