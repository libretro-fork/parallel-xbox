# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate fixed depth kernels, allocation-free dispatch and optional GPU parity."""
from pathlib import Path
import argparse,ctypes,os,re,shutil,subprocess,tempfile

def function(s,name):
 m=re.search(r'^(?:static )?(?:VkShaderModule|VkPipeline|void|ComputeGrid)\s*'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
def run(r,cc,folder,gpu):
 if os.name=='nt':
  k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 s=(r/'hw/xbox/nv2a/pgraph/vk/surface-compute.c').read_text();g=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
 code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join([function(g,'pgraph_vk_create_shader_module_from_words')]+[function(s,n) for n in ['create_compute_pipeline','get_compute_pipeline','get_dispatch','create_compute_pipelines','destroy_compute_pipelines']]))
 q=(r/'libretro/tests/test-spv-geom.c').read_text();a=q.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=q.index('int line_tests(',a);code=code.replace('/* QUIET ERRORS */',q[a:b])
 prefix=(r/'libretro/tests/spv-clear-vulkan.h').read_text();prefix=prefix[:prefix.index('static VkPipeline pipeline(')].replace('Utility GPU:','Depth GPU:')
 prefix=prefix.replace('static unsigned memory_type(unsigned bits,VkMemoryPropertyFlags flags){for(unsigned i=0;i<memory_properties.memoryTypeCount;i++)if((bits&(1u<<i))&&(memory_properties.memoryTypes[i].propertyFlags&flags)==flags)return i;abort();}', 'static unsigned memory_type(unsigned bits,VkMemoryPropertyFlags flags){for(unsigned pass=0;pass<2;pass++){unsigned required=flags|(pass?0:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);for(unsigned i=0;i<memory_properties.memoryTypeCount;i++)if((bits&(1u<<i))&&(memory_properties.memoryTypes[i].propertyFlags&required)==required)return i;}abort();}')
 (folder/'gpu-common.h').write_text(prefix);(folder/'test.c').write_text(code);exe=folder/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-function','-Wno-missing-field-initializers','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'libretro/tests'),'-I'+str(folder),'-I'+str(r/'deps/SPIRV-Reflect/include'),'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/volk')]
 sources=[str(folder/'test.c')]+[str(r/'hw/xbox/nv2a/pgraph/spv'/n) for n in ['depth.c','glslc.c','glslc_lex.c']]+[str(r/'deps/SPIRV-Reflect/spirv_reflect.c')]
 if gpu:cmd+=['-DTEST_VULKAN'];sources+=[str(r/'deps/volk/volk.c')]
 cmd+=sources+['-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc','-lm']
 if gpu and os.name!='nt':cmd+=['-ldl']
 subprocess.run(cmd+['-o',str(exe)],check=True,env=env);subprocess.run([str(exe),str(folder)],check=True,timeout=180,env=env)
 validator=shutil.which('spirv-val',path=env['PATH'])
 if validator:
  for p in folder.glob('*.spv'):subprocess.run([validator,'--target-env','vulkan1.0' if p.name.startswith('static') else 'vulkan1.3',str(p)],check=True,env=env)
  print('Depth modules: spirv-val passed',flush=True)
 else:print('SKIP: spirv-val unavailable',flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='gcc');p.add_argument('--gpu',action='store_true');p.add_argument('--keep',type=Path);a=p.parse_args()
 if a.keep:a.keep.mkdir(parents=True,exist_ok=True);run(a.checkout.resolve(),a.cc,a.keep.resolve(),a.gpu)
 else:
  with tempfile.TemporaryDirectory(prefix='spv-depth-') as d:run(a.checkout.resolve(),a.cc,Path(d),a.gpu)
