# SPDX-License-Identifier: LGPL-2.1-or-later
"""Byte-compare selective/full module keys with frozen baselines.
--vertex-clear-baseline isolates the overwritten vertex-state clear change.
--check-only skips timing; MODULE_KEY_ASM optionally saves GCC assembly.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text(encoding='utf-8')
def fn(source,name):
 m=re.search(r'^(?:static )?(?:inline )?(?:void|int) '+name+r'\([^;]*?\)\s*\{',source,re.M);assert m,name
 a=source.index('{',m.start());b=a+1;d=1
 while d:d+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[m.start():b]
reference_name='vertex-key-clear-reference.h' if '--vertex-clear-baseline' in sys.argv[3:] else 'pre-module-keys-reference.h'
reference=Path(__file__).with_name(reference_name).read_text().replace('vk_pre_state_module_keys(', 'reference_pre_keys(').replace('pgraph_vk_ref_pre_state_modules(', 'reference_pre_refs(').replace('vk_state_module_keys(', 'reference_keys(').replace('pgraph_vk_ref_state_modules(', 'reference_refs(')
functions=reference+'\n'+'\n'.join(fn(s,n) for n in ['vk_pre_state_module_keys','vk_state_module_keys','pgraph_vk_ref_pre_state_modules','pgraph_vk_ref_state_modules'])
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8');types=re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;',h,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */',functions)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='pre-module-keys-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n');(p/'test.c').write_text(code)
 for checked in ([True] if '--check-only' in sys.argv[3:] else [True,False]):
  exe=p/('test.exe' if os.name=='nt' else 'test');flags=['-fno-ipa-icf','-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else ['-fno-ipa-icf']
  if '--vertex-clear-baseline' in sys.argv[3:]: flags.append('-DREFERENCE_VERTEX_CLEAR')
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',str(exe)],env=env,check=True)
  if os.environ.get('MODULE_KEY_ASM') and not checked:
   subprocess.run([cc,'-O2','-fno-ipa-icf','-std=gnu11','-S','-I'+str(p),'-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',os.environ['MODULE_KEY_ASM']],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
