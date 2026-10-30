# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare copy and borrowed SPIR-V reflection using the real compiler/parser.
Usage: python test-spv-reflection-borrow.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc';env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
source=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text();header=(r/'hw/xbox/nv2a/pgraph/vk/glsl.h').read_text()
def function(name):
 m=re.search(r'^static void '+name+r'\(',source,re.M);assert m
 a=source.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[m.start():b]
structs='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',header,re.S)[0] for name in ['ShaderUniform','ShaderUniformLayout'])
init=function('init_layout_from_spv')
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',structs).replace('/* PRODUCTION */','\n'.join([function('block_to_uniforms'),init,function('finalize_uniform_layout')]))
with tempfile.TemporaryDirectory(prefix='spv-reflection-',dir=r.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-I'+str(r),'-I'+str(r/'deps/SPIRV-Reflect/include'),'-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),str(r/'deps/SPIRV-Reflect/spirv_reflect.c'),'-lm','-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
