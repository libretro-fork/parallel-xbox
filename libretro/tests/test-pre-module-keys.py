# SPDX-License-Identifier: LGPL-2.1-or-later
"""Byte-compare selective/full module keys with frozen baselines.
--vertex-clear-baseline uses the pre-vertex-clear snapshot.
--fragment-clear-baseline uses the immediate pre-fragment-clear snapshot.
--geometry-clear-baseline uses the immediate pre-geometry-clear snapshot.
--copy-control compares identical geometry-baseline builders.
MODULE_KEY_BENCH_CPU optionally pins a Windows CPU; timing is monotonic.
MODULE_KEY_SOURCE optionally selects a local candidate renderer source file.
--check-only skips timing; MODULE_KEY_ASM optionally saves GCC assembly.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
source=Path(os.environ.get('MODULE_KEY_SOURCE',repo/'hw/xbox/nv2a/pgraph/vk/shaders.c'))
s=source.read_text(encoding='utf-8')
def fn(source,name):
 m=re.search(r'^(?:static )?(?:inline )?(?:void|int) '+name+r'\([^;]*?\)\s*\{',source,re.M);assert m,name
 a=source.index('{',m.start());b=a+1;d=1
 while d:d+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[m.start():b]
reference_name='vertex-key-clear-reference.h' if '--vertex-clear-baseline' in sys.argv[3:] else 'pre-module-keys-reference.h'
if '--geometry-clear-baseline' in sys.argv[3:]: reference_name='geometry-key-clear-reference.h'
if '--fragment-clear-baseline' in sys.argv[3:]: reference_name='fragment-key-clear-reference.h'
reference=Path(__file__).with_name(reference_name).read_text(encoding='utf-8').replace('vk_pre_state_module_keys(', 'reference_pre_keys(').replace('pgraph_vk_ref_pre_state_modules(', 'reference_pre_refs(').replace('vk_state_module_keys(', 'reference_keys(').replace('pgraph_vk_ref_state_modules(', 'reference_refs(')
current='\n'.join(fn(s,n) for n in ['vk_pre_state_module_keys','vk_state_module_keys','pgraph_vk_ref_pre_state_modules','pgraph_vk_ref_state_modules'])
if '--copy-control' in sys.argv[3:]:
 assert '--geometry-clear-baseline' in sys.argv[3:], 'copy control requires the geometry-clear baseline'
 current=reference.replace('reference_pre_keys(', 'vk_pre_state_module_keys(').replace('reference_pre_refs(', 'pgraph_vk_ref_pre_state_modules(').replace('reference_keys(', 'vk_state_module_keys(').replace('reference_refs(', 'pgraph_vk_ref_state_modules(')
functions=reference+'\n'+current
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8');types=re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;',h,re.S)[0]
helpers=Path(__file__).with_name('test-worker-direct.c').read_text(encoding='utf-8')
helpers=helpers[helpers.index('static void pin_thread('):helpers.index('static retro_atomic_int_t started,start_gate;')].replace('static double monotonic_ns(', 'static inline double monotonic_ns(')
code=Path(__file__).with_suffix('.c').read_text(encoding='utf-8').replace('/* BENCH HELPERS */',helpers).replace('/* TYPES */',types).replace('/* FUNCTIONS */',functions)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='pre-module-keys-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_bytes(b'#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_bytes(b'typedef struct MString MString;\n');(p/'test.c').write_bytes(code.encode())
 for checked in ([True] if '--check-only' in sys.argv[3:] else [True,False]):
  exe=p/('test.exe' if os.name=='nt' else 'test');flags=['-fno-ipa-icf','-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else ['-fno-ipa-icf']
  if '--geometry-clear-baseline' in sys.argv[3:]: flags.append('-DREFERENCE_GEOMETRY_CLEAR')
  if '--copy-control' in sys.argv[3:]: flags.append('-DCOPY_CONTROL')
  if 'after_geom' not in current: flags.append('-DCURRENT_CLEARS_GEOMETRY_STATE')
  if '--vertex-clear-baseline' in sys.argv[3:]: flags.append('-DREFERENCE_VERTEX_CLEAR')
  if '--fragment-clear-baseline' in sys.argv[3:]: flags.append('-DREFERENCE_FRAGMENT_CLEAR')
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',str(exe)],env=env,check=True)
  if os.environ.get('MODULE_KEY_ASM') and not checked:
   subprocess.run([cc,'-O2','-std=gnu11',*flags,'-S','-I'+str(p),'-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',os.environ['MODULE_KEY_ASM']],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)

 if '--geometry-clear-baseline' in sys.argv[3:] and '--copy-control' not in sys.argv[3:]:
  if 'after_geom' in current:
   geom_clear=re.search(r'memset\(\(char \*\)&keys\[n\] \+ after_geom, 0, [^;]+;',current)[0]
  else:
   geom_clear='memset(&keys[n], 0, sizeof(keys[n]));'
  negative_flags=['-DCURRENT_CLEARS_GEOMETRY_STATE'] if 'after_geom' not in current else []
  for label,mutant in [
   ('missing geometry copy',code.replace(current,current.replace('keys[n].geom.state = st->geom;','/* omitted geometry copy */',1),1)),
   ('missing geometry clear',code.replace(current,current.replace(geom_clear,'/* missing geometry clear */',1),1)),
   ('missing clear observer',code.replace('cleared+=n;return memset(dst,v,n);','return memset(dst,v,n);',1))]:
   assert mutant!=code,label
   (p/'test.c').write_bytes(mutant.encode())
   subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fno-ipa-icf','-DCHECKED','-DREFERENCE_GEOMETRY_CLEAR',*negative_flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',str(exe)],env=env,check=True)
   result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=120)
   assert result.returncode and b'FAIL line' in result.stderr,(label,result.returncode,result.stderr)
   print('Required negative control rejected:',label,flush=True)
