# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compare production binding decisions/cache keys with frozen f97ffe6.
--copy-baseline uses 3aa46e0, introduced as an identical-code control before
further copy changes. Nine timing paths cover queue/handoff/rejection cases.
--real-canonical extracts the production fragment canonicalizer.
--copy-control compiles identical frozen binding bodies on both sides.
--check-only skips timing. BINDING_BENCH_PIN pins the Windows fixture thread;
BINDING_BENCH_CPU optionally chooses its logical CPU. BINDING_ASM saves GCC assembly.
BINDING_BENCH_BACKEND selects scalar/sse2/avx2/avx512 on supported x86 hosts.
Timing runs reuse one cache allocation and omit correctness-only callback hashes.
Real state/binding types, LRU and XXH3; modeled speculation/uniform callbacks.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text(encoding='utf-8')
def fn(name):
 m=re.search(r'^(?:static )?(?:__attribute__\(\(noinline\)\) )?(?:void|bool|ShaderBinding \*)\s*(?:__attribute__\(\(noinline\)\)\s*)?'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m
 a=s.index('{',m.start());b=a+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
reference_file='binding-copy-reference.h' if '--copy-baseline' in sys.argv[3:] else 'binding-state-hash-reference.h'
reference=Path(__file__).with_name(reference_file).read_text().replace('get_shader_binding_for_state(', 'reference_get_binding(').replace('pgraph_vk_bind_shaders(', 'reference_bind(')
functions=reference+'\n'+'\n'.join(fn(name) for name in ('shader_cache_entry_init_view','shader_cache_entry_compare_view','get_shader_binding_for_state','get_shader_binding_for_view','vk_try_sibling_binding','pgraph_vk_bind_shaders'))
if '--copy-control' in sys.argv[3:]:
 functions=reference+'\n'+fn('shader_cache_entry_init_view')+'\n'+fn('shader_cache_entry_compare_view')+'\n'+Path(__file__).with_name('binding-copy-reference.h').read_text()
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8');types=re.search(r'typedef struct ShaderBinding \{.*?\} ShaderBinding;',h,re.S)[0]
view=s[s.index('enum {\n    VK_SHADER_HEAD_BYTES'):s.index('static inline int vk_pre_state_module_keys(')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types+'\n'+view).replace('/* FUNCTIONS */',functions)
if '--real-canonical' in sys.argv[3:]:
 psh=(repo/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text(encoding='utf-8')
 def extract_psh(name):
  m=re.search(r'^(?:static )?(?:__attribute__\(\(noinline\)\) )?(?:void|bool) '+name+r'\([^;]*?\)\s*\{',psh,re.M);assert m,name
  a=psh.index('{',m.start());b=a+1;d=1
  while d:d+=(psh[b]=='{')-(psh[b]=='}');b+=1
  return psh[m.start():b]
 actual='static bool pgraph_gpu_boost_gl(void){return canonical;}\n'+'\n'.join(extract_psh(n) for n in ('psh_zero_combiner_fields','psh_zero_alpha_fields','pgraph_glsl_psh_canonicalize_dynamic'))
 actual=actual.replace('bool pgraph_glsl_psh_canonicalize_dynamic(', '__attribute__((noinline)) bool pgraph_glsl_psh_canonicalize_dynamic(')
 code=re.sub(r'__attribute__\(\(noinline\)\) bool pgraph_glsl_psh_canonicalize_dynamic\([^\n]+',lambda _:actual,code,count=1)
 code=code.replace('sibling.psh.alpha_dynamic=true;', 'pgraph_glsl_psh_canonicalize_dynamic(&sibling.psh);')
 code=code.replace('dirty=(trial&2)!=0;', 'requested.psh.combiner_dynamic=(trial&1024)!=0;dirty=(trial&2)!=0;')
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='binding-state-hash-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n');(p/'test.c').write_text(code)
 for checked in ([True] if '--check-only' in sys.argv[3:] else [True,False]):
  exe=p/('test.exe' if os.name=='nt' else 'test');flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  if '--copy-baseline' in sys.argv[3:]: flags.append('-DREFERENCE_REUSES_HASH')
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),str(repo/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)],env=env,check=True)
  if not checked and os.environ.get('BINDING_ASM'):
   subprocess.run([cc,'-O2','-std=gnu11','-fno-ipa-icf','-S','-I'+str(p),'-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',os.environ['BINDING_ASM']],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
