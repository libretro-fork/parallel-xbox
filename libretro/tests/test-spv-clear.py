# SPDX-License-Identifier: GPL-2.0-or-later
"""Check immutable utility SPIR-V, borrowed creation and optional real GPU parity."""
from pathlib import Path
import argparse,ctypes,os,re,shutil,subprocess,tempfile

def function(s,name):
 m=re.search(r'^(?:static )?(?:VkShaderModule|ShaderModuleInfo \*|void)\s*'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
def run(r,cc,folder,gpu):
 if os.name=='nt':
  k=ctypes.WinDLL('kernel32');k.GetErrorMode.restype=ctypes.c_uint;k.SetErrorMode(k.GetErrorMode()|0x8003)
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 s=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text();names=['pgraph_vk_create_shader_module_from_words']
 draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
 code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join([function(s,n) for n in names]+[function(draw,n) for n in ['init_clear_shaders','finalize_clear_shaders']]))
 errors=(r/'libretro/tests/test-spv-geom.c').read_text();a=errors.index('#ifdef _WIN32\nstatic void quiet_test_abort');b=errors.index('int line_tests(',a);code=code.replace('/* QUIET ERRORS */',errors[a:b])
 (folder/'test.c').write_text(code);exe=folder/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-missing-field-initializers','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'libretro/tests'),'-I'+str(r/'deps/SPIRV-Reflect/include'),'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/volk')]
 sources=[str(folder/'test.c')]+[str(r/'hw/xbox/nv2a/pgraph/spv'/n) for n in ['clear.c','glslc.c','glslc_lex.c']]+[str(r/'deps/SPIRV-Reflect/spirv_reflect.c')]
 if gpu:cmd+=['-DTEST_VULKAN'];sources+=[str(r/'deps/volk/volk.c')]
 cmd+=sources+['-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc','-lm']
 if gpu and os.name!='nt':cmd+=['-ldl']
 subprocess.run(cmd+['-o',str(exe)],check=True,env=env);subprocess.run([str(exe),str(folder)],check=True,timeout=120,env=env)
 validator=shutil.which('spirv-val',path=env['PATH'])
 if validator:
  for p in folder.glob('*.spv'):subprocess.run([validator,'--target-env','vulkan1.0' if p.name.startswith('static') else 'vulkan1.3',str(p)],check=True,env=env)
  print('Utility modules: spirv-val passed',flush=True)
 else:print('SKIP: spirv-val unavailable',flush=True)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='gcc');p.add_argument('--gpu',action='store_true');p.add_argument('--keep',type=Path);a=p.parse_args()
 if a.keep:a.keep.mkdir(parents=True,exist_ok=True);run(a.checkout.resolve(),a.cc,a.keep.resolve(),a.gpu)
 else:
  with tempfile.TemporaryDirectory(prefix='spv-clear-') as d:run(a.checkout.resolve(),a.cc,Path(d),a.gpu)
