# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check production pipeline-job reset against the old full clear.
Actual job/key/shader declarations; only OS/MString declarations are stubbed.
Usage: python test-pipeline-job-reset.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
m=re.search(r'^static void pipeline_job_reset\(.*?^\}',s,re.M|re.S);assert m
current=m[0]
assert s.count('    pipeline_job_reset(job);')==1 and s.count('    pipeline_job_reset(&job);')==1
reference=Path(__file__).with_name('pipeline-job-reset-reference.h').read_text()
functions='\n'.join(fn.replace('static void','__attribute__((noinline)) static void',1) for fn in [reference,current])
h=(repo/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
types='\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',h,re.S)[0] for name in ['RenderPassState','PipelineKey'])
types+='\n'+'\n'.join(re.search(r'typedef struct '+name+r' \{.*?\} '+name+r';',s,re.S)[0] for name in ('GplPreKey','GplFragKey'))
job=re.search(r'typedef struct \{\n    bool frag;.*?\} LibJob;',s,re.S)[0]
if 'S3tcPipelineJob' in job:job=re.search(r'typedef struct S3tcPipelineJob \{.*?\} S3tcPipelineJob;',(repo/'hw/xbox/nv2a/pgraph/vk/s3tc-pipeline-job.h').read_text(),re.S)[0]+'\n'+job

code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types+'\ntypedef struct ShaderModuleInfo ShaderModuleInfo;\n'+job).replace('/* FUNCTIONS */',functions)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='job-reset-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir()
 (p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
 (p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 (p/'test.c').write_text(code)
 for checked in [True,False]:
  exe=p/('test.exe' if os.name=='nt' else 'test')
  flags=['-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else []
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(p),'-I'+str(repo),'-I'+str(repo/'deps/vulkan-headers'),str(p/'test.c'),'-o',str(exe)],env=env,check=True)
  subprocess.run([str(exe)],env=env,check=True,timeout=120)
