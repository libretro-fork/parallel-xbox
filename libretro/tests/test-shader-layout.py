# SPDX-License-Identifier: GPL-2.0-or-later
"""Check compiler layouts and renderer ownership; optionally compare external SPIRV-Reflect.
Usage: python test-shader-layout.py CHECKOUT [CC] [--reflect-dir CHECKOUT]
"""
from pathlib import Path
import argparse, os, re, subprocess, tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('checkout', type=Path)
p.add_argument('cc', nargs='?', default='gcc')
p.add_argument('--reflect-dir', type=Path)
p.add_argument('--shader-dir', type=Path, help='also check seedshaders GLSL output')
a = p.parse_args(); r = a.checkout.resolve()
s = (r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
h = (r/'hw/xbox/nv2a/pgraph/vk/glsl.h').read_text()
def function(name):
    m = re.search(r'^static void '+name+r'\(', s, re.M); assert m
    start = s.index('{', m.start()); end = start+1; depth = 1
    while depth:
        depth += (s[end]=='{')-(s[end]=='}'); end += 1
    return s[m.start():end]
types = '\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';', h, re.S)[0]
                  for name in ['ShaderUniformLayout'])
code = Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */', types)
code = code.replace('/* PRODUCTION */', function('init_uniform_layout')+'\n'+function('finalize_uniform_layout'))
env = os.environ.copy(); env['PATH'] = str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='shader-layout-') as d:
    folder = Path(d); (folder/'test.c').write_text(code)
    exe = folder/('test.exe' if os.name=='nt' else 'test')
    cmd = [a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-missing-field-initializers',
           '-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
           '-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),str(folder/'test.c'),
           str(r/'hw/xbox/nv2a/pgraph/spv/glslc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c')]
    if a.reflect_dir:
        ref = a.reflect_dir.resolve()
        cmd += ['-DTEST_SPIRV_REFLECT','-I'+str(ref),str(ref/'spirv_reflect.c')]
    subprocess.run(cmd+['-lm','-o',str(exe)],check=True,env=env)
    args = [str(exe)]
    if a.shader_dir:
        files = sorted(a.shader_dir.resolve().glob('*.glsl')); assert files
        manifest = folder/'shaders.txt'
        manifest.write_text(''.join(str(file)+'\n' for file in files))
        args.append(str(manifest))
    subprocess.run(args,check=True,env=env)
