# SPDX-License-Identifier: LGPL-2.1-or-later
"""Compare worker records and hash counts with actual XXH3 and key layouts.
--clear-baseline compares against the immediate pre-change record builder.
--check-only skips timing; WORKER_RECORD_ASM optionally saves GCC assembly.
Production state-compilation bodies; modeled module memo and bounded queue.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text(encoding='utf-8')
a=s.index('static void vk_spec_compile_state(');b=s.index('{',a)+1;d=1
while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
current=s[a:b].replace('static void','__attribute__((noinline)) static void',1)
reference_name='worker-record-clear-reference.h' if '--clear-baseline' in sys.argv[3:] else 'worker-key-hash-reference.h'
reference=Path(__file__).with_name(reference_name).read_text().replace('vk_spec_compile_state(', 'reference_state(').replace('vk_spec_compile_module(', 'vk_spec_compile_module(' if '--clear-baseline' in sys.argv[3:] else 'reference_module(').replace('static void','__attribute__((noinline)) static void',1)
record=re.search(r'typedef struct VkSpecDone \{.*?\} VkSpecDone;',s,re.S)[0]
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text(encoding='utf-8');key=re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;',h,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* KEY */',key).replace('/* RECORD */',record).replace('/* FUNCTIONS */',reference+'\n'+current)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='worker-key-hash-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n');(p/'test.c').write_text(code)
 for checked in ([True] if '--check-only' in sys.argv[3:] else [True,False]):
  exe=p/('test.exe' if os.name=='nt' else 'test');flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  if '--clear-baseline' in sys.argv[3:]: flags.append('-DREFERENCE_REUSES_HASH')
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),str(repo/'util/fast-hash.c'),str(repo/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)],env=env,check=True)
  if not checked and os.environ.get('WORKER_RECORD_ASM'):
   subprocess.run([cc,'-O2','-std=gnu11','-fno-ipa-icf','-S','-I'+str(p),'-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',os.environ['WORKER_RECORD_ASM']],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
