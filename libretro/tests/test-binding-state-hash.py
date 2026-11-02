# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compare production binding decisions/cache keys with frozen f97ffe6.
Real state/binding types, LRU and XXH3; modeled speculation/uniform callbacks.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text(encoding='utf-8')
def fn(name):
 m=re.search(r'^(?:static )?(?:void|ShaderBinding \*)\s*'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m
 a=s.index('{',m.start());b=a+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
reference=Path(__file__).with_name('binding-state-hash-reference.h').read_text().replace('get_shader_binding_for_state(', 'reference_get_binding(').replace('pgraph_vk_bind_shaders(', 'reference_bind(')
functions=reference+'\n'+fn('get_shader_binding_for_state')+'\n'+fn('pgraph_vk_bind_shaders')
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8');types=re.search(r'typedef struct ShaderBinding \{.*?\} ShaderBinding;',h,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */',functions)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='binding-state-hash-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n');(p/'test.c').write_text(code)
 for checked in [True,False]:
  exe=p/('test.exe' if os.name=='nt' else 'test');flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),str(repo/'util/fast-hash.c'),str(repo/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
