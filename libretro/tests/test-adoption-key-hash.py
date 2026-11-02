# SPDX-License-Identifier: LGPL-2.1-or-later
"""Exercise production shader-module adoption through the real LRU lifecycle.
Actual keys, lookup/initializer/ready-scan bodies and hash; modeled GPU objects.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text(encoding='utf-8')
def fn(name):
 m=re.search(r'^static (?:void|bool|ShaderSource \*|ShaderModuleInfo \*)\s*'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
 a=s.index('{',m.start());b=a+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
reference=Path(__file__).with_name('adoption-key-hash-reference.h').read_text().replace('vk_spec_take_ready(', 'reference_ready(').replace('shader_module_cache_entry_init(', 'reference_init(')
functions=reference+'\n'+'\n'.join(fn(n) for n in ['vk_spec_take_ready','shader_module_cache_entry_init','shader_module_cache_entry_post_evict','shader_module_cache_entry_compare','get_and_ref_shader_module_for_key'])
assert functions.count('static ShaderModuleInfo *\nget_and_ref_shader_module_for_key')==1
functions=functions.replace('static ShaderModuleInfo *\nget_and_ref_shader_module_for_key', '__attribute__((noinline)) static ShaderModuleInfo *\nget_and_ref_shader_module_for_key')
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8')
types='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',h,re.S)[0] for name in ['ShaderModuleCacheKey','ShaderModuleCacheEntry'])
types+='\n'+re.search(r'typedef struct VkSpecDone \{.*?\} VkSpecDone;',s,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */',functions)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='adoption-key-hash-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n');(p/'test.c').write_text(code)
 for checked in [True,False]:
  exe=p/('test.exe' if os.name=='nt' else 'test');flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),str(repo/'util/fast-hash.c'),str(repo/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
