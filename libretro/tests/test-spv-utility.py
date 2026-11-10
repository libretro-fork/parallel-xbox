# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare immutable utility modules to production reference GLSL; explicit regeneration only."""
from pathlib import Path
import argparse,os,re,shutil,shlex,subprocess,tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');p.add_argument('--generate',action='store_true');p.add_argument('--ownership-only',action='store_true');a=p.parse_args();r=a.checkout.resolve()
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
s=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text();start=s.index('VkShaderModule pgraph_vk_create_utility_shader_module(');end=s.index('\n}',start)+2;body=s[start:end]
assert all(x not in body for x in ['compile_glsl','malloc','memcpy','ShaderSource','glslc_compile'])
for name,kinds in [('pvideo.c',['PVIDEO']),('gpuprops.c',['PROPS_VERTEX','PROPS_GEOMETRY','PROPS_FRAGMENT']),('s3tc-decode.h',['S3TC_ARRAY','S3TC_VOLUME'])]:
 text=(r/'hw/xbox/nv2a/pgraph/vk'/name).read_text();assert 'create_fixed_shader_module_from_glsl' not in text
 for kind in kinds:assert 'SPV_UTILITY_'+kind in text
props=(r/'hw/xbox/nv2a/pgraph/vk/gpuprops.c').read_text()
for variable,kind in [('vert_shader_module','PROPS_VERTEX'),('geom_shader_module','PROPS_GEOMETRY'),('frag_shader_module','PROPS_FRAGMENT')]:
 assert re.search(variable+r'\s*=\s*pgraph_vk_create_utility_shader_module\(\s*r,\s*SPV_UTILITY_'+kind+r'\s*\)',props),variable
with tempfile.TemporaryDirectory(prefix='spv-utility-') as tmp:
 d=Path(tmp);code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body);(d/'test.c').write_text(code)
 sources=[str(d/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c')]
 if not a.generate:sources.append(str(r/'hw/xbox/nv2a/pgraph/spv/utility.c'))
 env=dict(os.environ);env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-Wno-missing-field-initializers','-Wno-misleading-indentation','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'deps/spirv-headers/include')]+(['-DGENERATE_UTILITY_WORDS'] if a.generate else [])+sources+['-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc','-lm','-o',str(exe)],check=True,env=env)
 subprocess.run(shlex.split(env.get("SPV_UTILITY_RUNNER",""))+[str(exe),str(d)],check=True,env=env)
 if not a.ownership_only:
  validator=shutil.which('spirv-val',path=env['PATH']);assert validator,'UNQUALIFIED: spirv-val unavailable'
  for file in d.glob('*.spv'):subprocess.run([validator,'--target-env','vulkan1.3' if '-1-' in file.name else 'vulkan1.2',str(file)],check=True,env=env)
  print('12 fixed utility modules validated as Vulkan SPIR-V')
 else:print('Focused word equality/ownership checks only; separate SPIR-V validation required')
 if a.generate:shutil.copyfile(d/'regenerate.inc',r/'hw/xbox/nv2a/pgraph/spv/utility-words.inc')
