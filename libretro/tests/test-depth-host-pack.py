# SPDX-License-Identifier: LGPL-2.1-or-later
"""Production depth pack descriptors, direct host destination and barriers."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def fn(file,name):
 s=(r/file).read_text();m=re.search(r'^(?:static )?(?:void|VkBuffer) '+name+r'\(',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
b='hw/xbox/nv2a/pgraph/vk/'
parts=[fn(b+'surface-compute.c',n) for n in ['write_descriptor_sets','pgraph_vk_pack_depth_stencil_dims','pgraph_vk_pack_depth_stencil']]
parts += [fn(b+'surface.c',n) for n in ['packed_readback_to_host_barrier','pack_depth_readback']]
s=(r/(b+'surface.c')).read_text();a=s.index('static bool download_surface_to_buffer');end=s.index('void pgraph_vk_surface_download_if_dirty',a);download=s[a:end]
assert download.index('packed_readback_to_host_barrier(cmd')<download.rindex('pgraph_vk_end_single_time_commands(pg, cmd)')<download.index('invalidate_readback(r, read_size)')
buf=(r/(b+'buffer.c')).read_text();assert 'if (index == BUFFER_STAGING_DST)' in buf and '.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT' in buf
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
with tempfile.TemporaryDirectory(prefix='depth-host-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=60)

if '--gpu' in sys.argv[3:]:
 s=(r/(b+'surface-compute.c')).read_text().replace('#include "hw/xbox/nv2a/pgraph/pgraph.h"','').replace('#include "renderer.h"','').replace('#include "compute-dispatch.h"','#include "hw/xbox/nv2a/pgraph/vk/compute-dispatch.h"')
 code=Path(__file__).with_name('test-depth-host-pack-vulkan.c').read_text().replace('/* COMPUTE PRODUCTION */',s).replace('/* READBACK PRODUCTION */','\n'.join(parts[-2:]))
 with tempfile.TemporaryDirectory(prefix='depth-host-gpu-') as tmp:
  p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
  link=[str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-Wno-missing-field-initializers','-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/spirv-headers/include'),str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/depth.c'),*link,'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
