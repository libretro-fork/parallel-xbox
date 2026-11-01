# SPDX-License-Identifier: LGPL-2.1-or-later
"""Production packed-source Morton readback routing, descriptors and barriers."""
from pathlib import Path
import ctypes,os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def fn(file,name):
 s=(r/file).read_text();m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\(',s,re.M);assert m,name
 a=m.start();b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
b='hw/xbox/nv2a/pgraph/vk/'
u=(r/(b+'unswizzle.c')).read_text();push=re.search(r'struct UnswizzlePush \{.*?\};',u,re.S)[0]
parts=[fn('hw/xbox/nv2a/pgraph/swizzle.c','generate_swizzle_masks')]
parts += [fn(b+'unswizzle.c',n) for n in ['pgraph_vk_unswizzle_packed_buffer_changed','swizzle_to_ram_from','pgraph_vk_swizzle_to_ram','pgraph_vk_swizzle_possible','pgraph_vk_swizzle_packed_possible','pgraph_vk_swizzle_packed_to_ram']]
parts.append(fn(b+'surface.c','store_packed_to_ram'))
surface=(r/(b+'surface.c')).read_text();assert 'surface->swizzle && !packed_swizzle' in surface
code=Path(__file__).with_suffix('.c').read_text().replace('/* PUSH */',push).replace('/* PRODUCTION */','\n'.join(parts));env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
with tempfile.TemporaryDirectory(prefix='packed-swizzle-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=60)

if '--gpu' in sys.argv[3:]:
 compute=(r/(b+'surface-compute.c')).read_text().replace('#include "hw/xbox/nv2a/pgraph/pgraph.h"','').replace('#include "renderer.h"','').replace('#include "compute-dispatch.h"','#include "hw/xbox/nv2a/pgraph/vk/compute-dispatch.h"')
 unswizzle=(r/(b+'unswizzle.c')).read_text().replace('#include "hw/xbox/nv2a/nv2a_int.h"','').replace('#include "renderer.h"','')
 pack=[]
 for name in ['packed_readback_to_host_barrier','pack_depth_readback']:
  src=(r/(b+'surface.c')).read_text();m=re.search(r'^static (?:void|VkBuffer) '+name+r'\(',src,re.M);a=m.start();end=src.index('{',a)+1;depth=1
  while depth:depth+=(src[end]=='{')-(src[end]=='}');end+=1
  pack.append(src[a:end])
 code=Path(__file__).with_name('test-packed-swizzle-vulkan.c').read_text().replace('/* COMPUTE PRODUCTION */',compute+'\n'+unswizzle).replace('/* READBACK PRODUCTION */',parts[-1]+'\n'+'\n'.join(pack))
 with tempfile.TemporaryDirectory(prefix='packed-swizzle-gpu-') as tmp:
  p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
  link=[str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-function','-Wno-misleading-indentation','-Wno-missing-field-initializers','-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/spirv-headers/include'),str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/depth.c'),str(r/'hw/xbox/nv2a/pgraph/spv/swizzle.c'),str(r/'hw/xbox/nv2a/pgraph/swizzle.c'),*link,'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
