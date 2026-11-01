# SPDX-License-Identifier: GPL-2.0-or-later
"""Production imported-depth eligibility and GPU row packing, without a CPU snapshot."""
from pathlib import Path
import argparse,os,re,shlex,shutil,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');p.add_argument('--gpu',action='store_true');a=p.parse_args();r=a.checkout.resolve()
s=(r/'hw/xbox/nv2a/pgraph/vk/surface.c').read_text(encoding='utf-8')
def function(name):
 m=re.search(r'^static (?:bool|void) '+name+r'\([^;]*?\)\s*\{',s,re.M);assert m;lo=m.start();hi=s.index('{',lo)+1;depth=1
 while depth:
  depth+=(s[hi]=='{')-(s[hi]=='}');hi+=1
 return s[lo:hi]
u=s[s.index('void pgraph_vk_upload_surface_data('):s.index('void pgraph_vk_upload_surface_data(')+18000]
assert 'bool from_vram = surface_upload_from_vram(r, surface);' in u
assert 'staged_offset, from_vram ? surface->pitch : row_bytes,' in u
assert 'regions[0].bufferRowLength = 0;' in u
assert '.buffer = source_buffer,' in u[u.index('VkBufferMemoryBarrier post_copy_src_barrier'):]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(function(n) for n in ['surface_upload_from_vram','copy_depth_source']))
with tempfile.TemporaryDirectory(prefix='vk-depth-source-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' or 'mingw' in a.cc else 'test')
 flags=shlex.split(os.environ.get('DEPTH_SOURCE_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' else '-g'))
 env=os.environ.copy();env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 link=[]
 if a.gpu:flags+=['-DTEST_GPU'];link=[str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']
 subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I'+str(r/'deps/vulkan-headers'),*flags,str(d/'test.c'),*link,'-o',str(exe)],check=True,env=env)
 if os.environ.get('DEPTH_SOURCE_BINARY'):shutil.copy2(exe,os.environ['DEPTH_SOURCE_BINARY'])
 if not os.environ.get('DEPTH_SOURCE_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env,timeout=180)
