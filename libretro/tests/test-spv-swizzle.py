# SPDX-License-Identifier: LGPL-2.1-or-later
"""Validate fixed Morton conversion modules and optional original/direct GPU parity."""
from pathlib import Path
import argparse,ctypes,os,re,shutil,subprocess,tempfile

def run(r,cc,folder,gpu):
 if os.name=='nt':
  k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 host=(r/'hw/xbox/nv2a/pgraph/vk/unswizzle.c').read_text()
 block=re.search(r'struct UnswizzlePush \{(.*?)\};',host,re.S).group(1)
 fields=re.findall(r'\b\w+\b',block.replace('uint32_t',''))
 assert fields==['src','dst','width','height','depth','bpp','mask_x','mask_y','mask_z','n_words','palette'],fields
 code=Path(__file__).with_suffix('.c').read_text()
 q=(r/'libretro/tests/test-spv-geom.c').read_text();a=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=q.index('int line_tests(',a);code=code.replace('/* QUIET ERRORS */',q[a:b])
 prefix=(r/'libretro/tests/spv-clear-vulkan.h').read_text();prefix=prefix[:prefix.index('static VkPipeline pipeline(')].replace('Utility GPU:','Swizzle GPU:')
 prefix=prefix.replace('static unsigned memory_type(unsigned bits,VkMemoryPropertyFlags flags){for(unsigned i=0;i<memory_properties.memoryTypeCount;i++)if((bits&(1u<<i))&&(memory_properties.memoryTypes[i].propertyFlags&flags)==flags)return i;abort();}', 'static unsigned memory_type(unsigned bits,VkMemoryPropertyFlags flags){for(unsigned pass=0;pass<2;pass++){unsigned required=flags|(pass?0:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);for(unsigned i=0;i<memory_properties.memoryTypeCount;i++)if((bits&(1u<<i))&&(memory_properties.memoryTypes[i].propertyFlags&required)==required)return i;}abort();}')
 (folder/'gpu-common.h').write_text(prefix);(folder/'test.c').write_text(code);exe=folder/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-function','-Wno-missing-field-initializers','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'libretro/tests'),'-I'+str(folder),'-I'+str(r/'deps/SPIRV-Reflect/include'),'-I'+str(r/'deps/SPIRV-Reflect'),'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/volk')]
 sources=[str(folder/'test.c')]+[str(r/'hw/xbox/nv2a/pgraph/spv'/n) for n in ['swizzle.c','glslc.c','glslc_lex.c']]+[str(r/'hw/xbox/nv2a/pgraph/swizzle.c'),str(r/'deps/SPIRV-Reflect/spirv_reflect.c')]
 if gpu:cmd+=['-DTEST_VULKAN'];sources+=[str(r/'deps/volk/volk.c')]
 cmd+=sources+['-lm']
 if gpu and os.name!='nt':cmd+=['-ldl']
 subprocess.run(cmd+['-o',str(exe)],check=True,env=env);subprocess.run([str(exe),str(folder)],check=True,timeout=180,env=env)
 validator=shutil.which('spirv-val',path=env['PATH'])
 if validator:
  for p in folder.glob('*.spv'):subprocess.run([validator,'--target-env','vulkan1.0' if p.name.startswith('static') else 'vulkan1.3',str(p)],check=True,env=env)
  print('Swizzle modules: spirv-val passed',flush=True)
 else:print('SKIP: spirv-val unavailable',flush=True)
 if gpu:
  subprocess.run([str(exe),str(folder),'--gpu'],check=True,timeout=180,env=env)
  if validator:
   for p in folder.glob('reference-*.spv'):subprocess.run([validator,'--target-env','vulkan1.3',str(p)],check=True,env=env)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='gcc');p.add_argument('--gpu',action='store_true');p.add_argument('--keep',type=Path);a=p.parse_args()
 if a.keep:a.keep.mkdir(parents=True,exist_ok=True);run(a.checkout.resolve(),a.cc,a.keep.resolve(),a.gpu)
 else:
  with tempfile.TemporaryDirectory(prefix='spv-swizzle-') as d:run(a.checkout.resolve(),a.cc,Path(d),a.gpu)
