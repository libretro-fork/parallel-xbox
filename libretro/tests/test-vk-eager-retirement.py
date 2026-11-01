# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production eager submission and command-slot retirement."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=Path(os.environ.get('VK_EAGER_SOURCE',root/'hw/xbox/nv2a/pgraph/vk/draw.c')).read_text()
def function(name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 a=m.start();b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(function(n) for n in ['pgraph_vk_reclaim_retired_images','pgraph_vk_slot_retired','pgraph_vk_settle_poll','pgraph_vk_reclaim_command_buffer','pgraph_vk_wait_all_slots','eager_submit_after_draw']))
assert 'eager_submit_after_draw(pg);' in function('pgraph_vk_draw_end')
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='vk-eager-retirement-',dir=root.parent) as tmp:
 t=Path(tmp);(t/'test.c').write_text(code);binary=os.environ.get('VK_EAGER_BINARY',str(t/'test'))
 flags=shlex.split(os.environ.get('VK_EAGER_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,str(t/'test.c'),'-o',binary]+(['-static'] if win else []),check=True)
 if not os.environ.get('VK_EAGER_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
