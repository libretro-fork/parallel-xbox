# SPDX-License-Identifier: GPL-2.0-or-later
"""Real SPSC completion publication, ownership, wrapping and CPU timing.
Production/frozen builders and actual key layouts/XXH3; module compilation mocked.
Default runs checked/optimized correctness and two required failing controls.
--benchmark adds synthetic CPU timing; --copy-control uses identical builders.
WORKER_BENCH_PRODUCER_CPU / WORKER_BENCH_CONSUMER_CPU optionally pin Windows CPUs.
WORKER_DIRECT_SOURCE optionally extracts an uncommitted candidate source file.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
source=Path(os.environ.get('WORKER_DIRECT_SOURCE',repo/'hw/xbox/nv2a/pgraph/vk/shaders.c'))
s=source.read_text(encoding='utf-8')
a=s.index('static void vk_spec_compile_state(');b=s.index('{',a)+1;d=1
while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
current=s[a:b].replace('static void','__attribute__((noinline)) static void',1)
reference=Path(__file__).with_name('worker-direct-reference.h').read_text(encoding='utf-8').replace('vk_spec_compile_state(', 'reference_state(').replace('static void','__attribute__((noinline)) static void',1)
if '--copy-control' in sys.argv[3:]:
 current=reference.replace('reference_state(', 'vk_spec_compile_state(')
record=re.search(r'typedef struct VkSpecDone \{.*?\} VkSpecDone;',s,re.S)[0]
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8');key=re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;',h,re.S)[0]
glsl=(repo/'hw/xbox/nv2a/pgraph/vk/glsl.h').read_text(encoding='utf-8')
source_type='typedef struct ShaderLayouts ShaderLayouts;\n'+re.search(r'typedef struct ShaderSource \{.*?\} ShaderSource;',glsl,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text(encoding='utf-8-sig').replace('/* SOURCE */',source_type).replace('/* KEY */',key).replace('/* RECORD */',record).replace('/* FUNCTIONS */',reference+'\n'+current)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='worker-direct-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_bytes(b'#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_bytes(b'typedef struct MString MString;\n');(p/'test.c').write_bytes(code.encode())
 for checked in [True,False]:
  exe=p/('test.exe' if os.name=='nt' else 'test');flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  if not checked and '--benchmark' in sys.argv[3:]: flags.append('-DBENCHMARK')
  command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-pthread',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),str(repo/'libretro-common/encodings/encoding_xxh3.c'),str(repo/'libretro-common/queues/retro_spsc.c'),'-o',str(exe)]
  subprocess.run(command,env=env,check=True)
  print('UBSan' if checked else 'Optimized',flush=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)

 # Deliberate bad builders must compile successfully and then fail the oracle.
 key_write=re.search(r'd.key = keys\[i\];|memcpy\(bytes \+ offsetof\(VkSpecDone, key\), &keys\[i\], sizeof\(keys\[i\]\)\);',current)
 drop=re.search(r'pgraph_vk_shader_source_unref\((?:d.spirv|spv)\);',current)
 assert key_write and drop, 'Update negative controls for the production builder'
 for label,old,new in [('missing-key',key_write[0],'/* omitted key */'),
                       ('leaked-drop',drop[0],'/* leaked drop */')]:
  assert old in current, 'Update negative-control mutation for the production builder'
  mutant=code.replace(current,current.replace(old,new,1))
  (p/'test.c').write_bytes(mutant.encode())
  subprocess.run(command,env=env,check=True)
  result=subprocess.run([str(exe)],env=env,capture_output=True,text=True,timeout=120)
  assert result.returncode and 'FAIL' in result.stderr,(label,result.returncode,result.stdout,result.stderr)
  print('Required negative control rejected:',label,flush=True)
