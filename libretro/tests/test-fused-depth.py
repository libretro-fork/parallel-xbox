# SPDX-License-Identifier: GPL-2.0-or-later
"""Fused depth SSA oracle; optional production Vulkan parity and timings."""
from pathlib import Path
import ctypes,os,re,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
b='hw/xbox/nv2a/pgraph/vk/'
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
def fn(file,name):
 s=(r/file).read_text();m=re.search(r'^(?:static )?(?:void|bool) '+name+r'\(',s,re.M);assert m,name
 a=m.start();end=s.index('{',a)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[a:end]
parts=[fn(b+'surface.c',n) for n in ['surface_readback_needs_swizzle','store_packed_to_ram']]
with tempfile.TemporaryDirectory(prefix='fused-depth-') as tmp:
 p=Path(tmp);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),str(Path(__file__).with_suffix('.c')),str(r/'hw/xbox/nv2a/pgraph/spv/depth.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe),str(p)],env=env,check=True,timeout=90)
 validator=shutil.which('spirv-val',path=env['PATH'])
 if validator:
  for module in p.glob('*.spv'):subprocess.run([validator,'--target-env','vulkan1.0',str(module)],env=env,check=True)
  print('Fused depth modules: spirv-val passed',flush=True)
 else:print('SKIP: spirv-val unavailable',flush=True)

host_names=['pgraph_vk_unswizzle_packed_buffer_changed','init_depth_swizzle','pgraph_vk_swizzle_possible','pgraph_vk_pack_depth_swizzled_possible','pgraph_vk_pack_depth_swizzled','pgraph_vk_finalize_unswizzle']
code=Path(__file__).with_name('test-fused-depth-host.c').read_text().replace('/* PRODUCTION */','\n'.join(fn(b+'unswizzle.c',name) for name in host_names))
with tempfile.TemporaryDirectory(prefix='fused-depth-host-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-missing-field-initializers','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),'-I'+str(r/'deps/vulkan-headers'),str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/depth.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=60)

if '--gpu' in sys.argv[3:]:
 compute=(r/(b+'surface-compute.c')).read_text().replace('#include "hw/xbox/nv2a/pgraph/pgraph.h"','').replace('#include "renderer.h"','').replace('#include "compute-dispatch.h"','#include "hw/xbox/nv2a/pgraph/vk/compute-dispatch.h"').replace('#include "libretro/dsp-ownership.h"','')
 unswizzle=(r/(b+'unswizzle.c')).read_text().replace('#include "hw/xbox/nv2a/nv2a_int.h"','').replace('#include "renderer.h"','').replace('#include "compute-dispatch.h"','#include "hw/xbox/nv2a/pgraph/vk/compute-dispatch.h"').replace('#include "libretro/dsp-ownership.h"','')
 pack=[]
 for name in ['depth_stencil_plane_size','packed_readback_to_host_barrier','pack_depth_readback']:
  src=(r/(b+'surface.c')).read_text();m=re.search(r'^static (?:void|VkBuffer|VkDeviceSize) '+name+r'\(',src,re.M);a=m.start();end=src.index('{',a)+1;depth=1
  while depth:depth+=(src[end]=='{')-(src[end]=='}');end+=1
  pack.append(src[a:end])
 code=Path(__file__).with_name('test-packed-swizzle-vulkan.c').read_text().replace('/* COMPUTE PRODUCTION */',compute+'\n'+unswizzle).replace('/* READBACK PRODUCTION */','\n'.join(parts+pack))
 code=code.replace('r.unswizzle.depth_set=VK_NULL_HANDLE;', '').replace('VkDescriptorSet packed_set=r.unswizzle.packed_set;', 'VkDescriptorSet depth_set=r.unswizzle.depth_set;VkDescriptorSet packed_set=r.unswizzle.packed_set;')
 code=code.replace('r.unswizzle.packed_set=variant?packed_set:VK_NULL_HANDLE;', 'r.unswizzle.packed_set=packed_set;r.unswizzle.depth_set=variant?depth_set:VK_NULL_HANDLE;').replace('copies=transfer_dependencies=0;', 'command_dispatches=copies=transfer_dependencies=0;').replace('copies==2*!variant&&transfer_dependencies==2*!variant','copies==0&&transfer_dependencies==0&&command_dispatches==(variant?2:4)')
 code=code.replace('static VkBuffer packed_buffer;', 'static unsigned command_dispatches;static void counted_dispatch(VkCommandBuffer c,uint32_t x,uint32_t y,uint32_t z){command_dispatches++;vkCmdDispatch(c,x,y,z);}'+'\n#define vkCmdDispatch counted_dispatch\nstatic VkBuffer packed_buffer;')
 code=code.replace('mode<3;mode++){unsigned w=mode==0?64:mode==1?512:1024,h=mode==0?64:mode==1?256:1024;', 'mode<7;mode++){unsigned widths[]={64,512,1024,256,1,1024,8192},heights[]={64,256,1024,512,1024,1,2};unsigned w=widths[mode],h=heights[mode];')
 code=code.replace('ring %.3fus packed %.3fus','two-pass %.3fus fused %.3fus').replace('copies/dependencies 2 -> 0','dispatches 4 -> 2; no intermediate packed payload').replace('4800 depth/Morton chains in 2400 two-readback submissions','11200 depth/Morton chains in 5600 two-readback submissions').replace('old ring vs direct source','two-pass vs fused pack/swizzle')
 if '--group128' in sys.argv:code=code.replace('pgraph_vk_init_compute(pg);','r.device_props.limits.maxComputeWorkGroupSize[0]=128;pgraph_vk_init_compute(pg);')
 with tempfile.TemporaryDirectory(prefix='packed-swizzle-gpu-') as tmp:
  p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
  link=[str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-function','-Wno-misleading-indentation','-Wno-missing-field-initializers','-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/spirv-headers/include'),str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/depth.c'),str(r/'hw/xbox/nv2a/pgraph/spv/swizzle.c'),str(r/'hw/xbox/nv2a/pgraph/swizzle.c'),*link,'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
