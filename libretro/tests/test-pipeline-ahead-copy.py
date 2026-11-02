# SPDX-License-Identifier: LGPL-2.1-or-later
"""Verify actual-layout speculative key copies and measure frozen/current blocks.
Copy blocks are extracted from production; queue/hash lifecycle is checked by
 test-pipeline-ahead-key.py. No worker or Vulkan calls are timed here.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();a=s.index('    PipelineKey *key = &job->key;',s.index('static void pipeline_ahead('));b=s.index('    uint64_t hash =',a)
blocks=[Path(__file__).with_name('pipeline-ahead-copy-reference.h').read_text(),s[a:b]]
functions='\n'.join('__attribute__((noinline)) static void '+name+'(PGRAPHVkState *r,LibJob *job,const PipelineKey *draw_key) {\n'+body+'\n}' for name,body in zip(['reference','current'],blocks))
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
types='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',h,re.S)[0] for name in ['RenderPassState','PipelineKey'])
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* FUNCTIONS */',functions)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='ahead-key-copy-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir()
 (p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
 (p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 (p/'test.c').write_text(code)
 for checked in [True,False]:
  exe=p/('test.exe' if os.name=='nt' else 'test')
  flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
