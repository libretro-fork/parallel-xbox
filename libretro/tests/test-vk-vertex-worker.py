# SPDX-License-Identifier: GPL-2.0-or-later
"""Run actual worker preparation/publication against checked native API mocks."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(name):
 m=re.search(r'^static (?:void|void \*) *'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',source,re.S)[0] for n,source in [('SpvVshOptions',(r/'hw/xbox/nv2a/pgraph/spv/vsh.h').read_text()),('S3tcPipelineJob',(r/'hw/xbox/nv2a/pgraph/vk/s3tc-pipeline-job.h').read_text()),('RenderPassState',h),('PipelineKey',h),('GplPreKey',s),('GplFragKey',s)])
types+='\n'+re.search(r'typedef struct \{\n    bool frag;.*?\} LibJob;',s,re.S)[0]
production=fn('pipeline_prepare_vertex')+'\n'+fn('pipe_worker_fn')
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* PRODUCTION */',production)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vertex-worker-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-Wno-clobbered','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
 if os.name!='nt':flags+=['-fsanitize=address','-fno-omit-frame-pointer']
 def run(text,negative=False):
  src.write_text(text);subprocess.run(flags,env=env,check=True)
  result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
  if negative:assert result.returncode,'worker mutation escaped'
  else:assert not result.returncode,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='')
 run(code)
 for label,before,after in [
  ('lost new vertex owner','pgraph_vk_ref_shader_module(vertex);','(void)pgraph_vk_ref_shader_module;'),
  ('wrong native vertex handle','job->stages[0].module = vertex->module;','job->stages[0].module = VK_NULL_HANDLE;'),
  ('lost fragment owner on prepend','job->mods[i] = job->mods[i - 1];','job->mods[i] = vertex;'),
  ('cancel ignored before preparation','if (retro_atomic_load_acquire_int(&pump_cancel)) {','if (false) {'),
  ('vertex not prepared before native creation','if (job->prepare_vertex) pipeline_prepare_vertex(job);','if (false) pipeline_prepare_vertex(job);'),
 ]:
  assert code.count(before)==1,label;run(code.replace(before,after,1),True);print('Required worker control rejected:',label)
