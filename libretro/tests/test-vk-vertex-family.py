# SPDX-License-Identifier: GPL-2.0-or-later
"""The real foreground family selector only uses complete matching native PSOs."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text();v=(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
def fn(source,name):
 m=re.search(r'^(?:static )?(?:bool|PipelineBinding \*|ShaderBinding \*) *'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 end=source.index('{',m.start())+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',source,re.S)[0] for n,source in [('SpvVshOptions',(r/'hw/xbox/nv2a/pgraph/spv/vsh.h').read_text()),('RenderPassState',h),('PipelineKey',h),('PipelineBinding',h)])
production=fn(v,'pgraph_glsl_vsh_canonicalize_lighting')+'\n'+'\n'.join(fn(s,n) for n in ('ready_vertex_pipeline','queue_vertex_pipeline','pgraph_vk_try_vertex_family'))
for forbidden in ('vkCreate','pump_drain','g_malloc','get_and_ref','pgraph_vk_prepare_vertex_module('):assert forbidden not in production,forbidden
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* PRODUCTION */',production)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vertex-family-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'deps/vulkan-headers'),'-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(src),'-o',str(exe)]
 if os.name!='nt':flags+=['-fsanitize=address','-fno-omit-frame-pointer']
 def run(text,negative=False):
  src.write_text(text);subprocess.run(flags,env=env,check=True)
  result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60)
  if negative:assert result.returncode,'family mutation escaped'
  else:assert not result.returncode,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='')
 run(code)
 for label,before,after in [
  ('full native key collision accepted','!memcmp(&binding->key, key, sizeof(*key))','true'),
  ('final job identity not owned','if (&job->key != key) memcpy(&job->key, key, sizeof(job->key));','/* lost key */'),
  ('fragment lifetime not acquired','pgraph_vk_ref_shader_module(fragment);','(void)fragment;'),
  ('ready vertex stage not acquired','pgraph_vk_ref_shader_module(vertex);','(void)vertex;'),
  ('duplicate pending vertex preparation','pending->prepare_vertex &&','false &&'),
  ('same PSO queued again','if (ticket) *ticket = i;\n            return true;','if (ticket) *ticket = i;\n            return false;'),
 ]:
  assert code.count(before)==1,label;run(code.replace(before,after,1),True);print('Required family control rejected:',label)
