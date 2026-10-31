# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate direct binary geometry against the production GLSL generator.
--gpu additionally captures every geometry output using test-only Vulkan storage writes.
"""
from pathlib import Path
import argparse,os,re,shutil,subprocess,tempfile

def function(source,signature):
 a=source.index(signature);i=source.index('{',a)+1;depth=1
 while depth:depth+=(source[i]=='{')-(source[i]=='}');i+=1
 return source[a:i]

def test_source(r):
 h=(r/'hw/xbox/nv2a/pgraph/glsl/geom.h').read_text()
 enums=(r/'hw/xbox/nv2a/pgraph/vsh_regs.h').read_text()
 types='\n'.join(re.search(r'enum '+n+r' \{[^}]*\};',enums)[0] for n in ['ShaderPrimitiveMode','ShaderPolygonMode'])
 types+='\n'+'\n'.join(re.search(r'typedef struct (?:\w+ )?\{[^{}]*\} '+n+r';',h)[0] for n in ['GeomState','GenGeomGlslOptions'])
 g=(r/'hw/xbox/nv2a/pgraph/glsl/geom.c').read_text();c=(r/'hw/xbox/nv2a/pgraph/glsl/common.c').read_text()
 source=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types)
 source=source.replace('/* GENERATORS */',function(c,'MString *pgraph_glsl_get_vtx_header(')+'\n'+function(g,'static const char *get_vertex_order(')+'\n'+function(g,'MString *pgraph_glsl_gen_geom('))
 s=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text()
 return source.replace('/* SELECTOR */',function(s,'static bool vk_module_line_options(')+'\n'+function(s,'static bool vk_module_triangle_options('))

def run(r,cc,folder,gpu):
 if os.name=='nt':
  # Inherited by compiler/test children; never change AeDebug or system policy.
  import ctypes
  kernel=ctypes.WinDLL('kernel32',use_last_error=True)
  kernel.GetErrorMode.restype=ctypes.c_uint
  kernel.SetErrorMode.argtypes=[ctypes.c_uint]
  kernel.SetErrorMode(kernel.GetErrorMode()|0x0001|0x0002|0x8000)
 env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
 (folder/'test.c').write_text(test_source(r));exe=folder/('test.exe' if os.name=='nt' else 'test')
 sources=[str(folder/'test.c')]+[str(r/'hw/xbox/nv2a/pgraph/spv'/n) for n in ['geom.c','glslc.c','glslc_lex.c']]
 command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-sign-compare','-Wno-missing-field-initializers','-I'+str(r),'-I'+str(r/'libretro/tests'),'-I'+str(r/'deps/SPIRV-Reflect/include'),'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/volk'),'-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
 if gpu:
  command+=['-DTEST_VULKAN'];sources.append(str(r/'deps/volk/volk.c'))
 command+=sources+['-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc','-lm']
 if gpu and os.name!='nt':command+=['-ldl']
 subprocess.run(command+['-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe),str(folder)],check=True,timeout=120,env=env)
 validator=shutil.which('spirv-val',path=env['PATH'])
 if validator:
  files=list(folder.glob('*.spv'))
  for file in files:
   import struct
   version=struct.unpack_from('<I',file.read_bytes(),4)[0]
   subprocess.run([validator,'--target-env','vulkan1.3' if version>=0x10600 else 'vulkan1.2',str(file)],check=True,env=env)
  print(f'spirv-val: {len(files)} production/direct/capture modules passed',flush=True)
 else:print('SKIP: spirv-val not installed',flush=True)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='gcc');p.add_argument('--gpu',action='store_true');p.add_argument('--keep',type=Path);a=p.parse_args();r=a.checkout.resolve()
 if a.keep:a.keep.mkdir(parents=True,exist_ok=True);run(r,a.cc,a.keep.resolve(),a.gpu)
 else:
  with tempfile.TemporaryDirectory(prefix='direct-spv-geom-') as d:run(r,a.cc,Path(d),a.gpu)
