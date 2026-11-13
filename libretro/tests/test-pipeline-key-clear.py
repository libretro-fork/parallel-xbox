# SPDX-License-Identifier: LGPL-2.1-or-later
"""Whole-byte pipeline-key equivalence and frozen-reference CPU timings.
Use actual shader headers/key declarations with minimal OS/MString declarations.
Renderer inputs and render-pass/register helpers are modeled; no core build.
Usage: python test-pipeline-key-clear.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
def fn(name):
 a=s.index(name+'(');a=s.rfind('\n',0,a)+1;b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
helper=fn('init_pipeline_key_for_shader')
current=fn('init_pipeline_key').replace('init_pipeline_key(', 'current(',1)
reference=Path(__file__).with_name('pipeline-key-init-reference.h').read_text().replace('init_pipeline_key(', 'reference(',1)
functions=helper+'\n'+'\n'.join(fn.replace('static void','__attribute__((noinline)) static void',1) for fn in [reference,current])
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
types='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',h,re.S)[0] for name in ['RenderPassState','PipelineKey'])
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */',functions)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='pipeline-key-clear-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir()
 (p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
 (p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 (p/'test.c').write_text(code)
 for checked in [True,False]:
  exe=p/('test.exe' if os.name=='nt' else 'test')
  flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
