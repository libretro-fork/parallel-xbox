# SPDX-License-Identifier: GPL-2.0-or-later
"""Test production packed Vulkan uniform updates and module generations.
Usage: python test-packed-uniforms.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
h=(repo/'hw/xbox/nv2a/pgraph/vk/glsl.h').read_text()
s=(repo/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text()
c=(repo/'hw/xbox/nv2a/pgraph/glsl/common.h').read_text()
u=(repo/'hw/xbox/nv2a/pgraph/glsl/uniform-cache.h').read_text()
def function(source,name):
 m=re.search(r'^(?:static )?(?:inline )?(?:bool|void) '+name+r'\(',source,re.M);assert m
 a=source.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[m.start():b]
structs='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',h,re.S)[0] for name in ['ShaderUniform','ShaderUniformLayout'])
info=re.search(r'typedef struct UniformInfo \{.*?\} UniformInfo;',c,re.S)[0]
sections=u[u.index('enum {'):u.index('};',u.index('enum {'))+2]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',structs+'\n'+info+'\n'+sections)
code=code.replace('/* PRODUCTION */','\n'.join(function(h,name) for name in ['uniform_element_equal','uniform_element_copy','uniform_update'])+'\n'+function(s,'pgraph_vk_uniform_sources_invalidate')+'\n'+function(s,'vk_apply_uniform_updates'))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='packed-uniforms-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
