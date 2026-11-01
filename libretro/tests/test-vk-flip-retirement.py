# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production Vulkan flip/owner continuation with controlled slot fences."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
v=Path(os.environ.get('VK_FLIP_SOURCE',root/'hw/xbox/nv2a/pgraph/vk/renderer.c')).read_text()
p=(root/'hw/xbox/nv2a/pgraph/pgraph.c').read_text()
def function(source,name):
 m=re.search(r'^[^\n;]*\b'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 a=m.start();b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join([function(v,'pgraph_vk_flip_stall_ready'),function(v,'pgraph_vk_flip_stall'),function(p,'pgraph_process_pending_flip')]))
finalize=function(v,'pgraph_vk_finalize')
assert finalize.index('pgraph_vk_finalize_command_buffers(pg)')<finalize.index('pg->host_flip_pending = false')<finalize.index('g_free')
assert '.flip_stall_ready = pgraph_vk_flip_stall_ready' in v
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='vk-flip-retirement-',dir=root.parent) as tmp:
 t=Path(tmp);(t/'test.c').write_text(code);binary=os.environ.get('VK_FLIP_BINARY',str(t/'test'))
 flags=shlex.split(os.environ.get('VK_FLIP_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,str(t/'test.c'),'-o',binary]+(['-static'] if win else []),check=True)
 if not os.environ.get('VK_FLIP_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
