# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run production overlay retirement against independently delayed GPU queues."""
from pathlib import Path
import os,re,subprocess,sys,tempfile,ctypes
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(path,name):
 s=(r/path).read_text();m=re.search(r'^(?:static )?(?:inline )?(?:void|bool) '+name+r'\(',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
base='hw/xbox/nv2a/pgraph/vk/'
parts=[function(base+'command.c',n) for n in ['aux_release_oldest','aux_retire_oldest','pgraph_vk_poll_aux_retired','pgraph_vk_end_single_time_commands_async']]
parts += [function(base+'draw.c',n) for n in ['trash_drain_retired','pgraph_vk_trash_push']]
parts += [function(base+'display.c','retire_pvideo_image'),function(base+'pvideo.c','pvideo_convert_bind_source')]
create=function(base+'display.c','create_pvideo_image')
assert 'acquire_pvideo_image(r, &image_create_info)' in create
assert 'vkCreateSampler' not in create
init=function(base+'pvideo.c','pgraph_vk_pvideo_init')
convert=function(base+'pvideo.c','pgraph_vk_pvideo_convert')
assert 'vkCreateComputePipelines' in init and 'pvideo_convert_bind_source' not in init
assert 'vkCreate' not in convert and 'pvideo_convert_bind_source(r, input, direct ? window : 0,' in convert
init_display=function(base+'display.c','pgraph_vk_init_display')
assert 'pgraph_vk_pvideo_init(pg)' in init_display and 'create_pvideo_sampler(pg)' in init_display
assert 'retire_pvideo_image(r)' in create and 'pgraph_vk_finish' not in create and 'single_time_commands' not in create
h=(r/(base+'renderer.h')).read_text();a=h.index('    struct XemuVkTrashEntry {');b=h.index('} *trash;',a)
trash=h[a:b+1]+';'
code=Path(__file__).with_suffix('.c').read_text().replace('/* TRASH TYPE */',trash).replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
with tempfile.TemporaryDirectory(prefix='pvideo-retire-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=30)
