# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact ready fallback selection and immutable full-PSO owner handoff."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();shader=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text();psh=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(s,name):
 m=re.search(r'^(?:static )?(?:bool|void|ShaderBinding \*) *'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ('RenderPassState','PipelineKey','PipelineBinding'))
parts='\n'.join([fn(psh,n) for n in ('psh_zero_alpha_fields','psh_zero_combiner_fields','pgraph_glsl_psh_canonicalize_dynamic')]+[fn(shader,n) for n in ('pgraph_vk_ready_shader_binding','pgraph_vk_use_ready_shader_binding')]+['static bool pipeline_ready(PGRAPHVkState*,uint64_t,const PipelineKey*); static ShaderBinding *pgraph_vk_materialize_ready_shader_binding(PGRAPHVkState *r,const PipelineKey *k,uint64_t h){if (!pipeline_ready(r,h,k))return NULL;return pgraph_vk_ready_shader_binding(r,&k->shader_state,fast_hash((const uint8_t*)&k->shader_state,sizeof(k->shader_state)));}']+[fn(draw,n) for n in ('pipeline_ready','pipeline_use_ready_sibling')])
create=fn(draw,'create_pipeline');prefix=create[:create.index('    /* The job holds')]
prefix+='    sync_calls++;sync_key=key;snode->pipeline=(VkPipeline)(uintptr_t)0x123;r->pipeline_binding=snode;\n}\n'
assert create.index('pipeline_use_ready_sibling(pg, &key, hash)')<create.index('vkCreateGraphicsPipelines(')
assert create.index('memcpy(&snode->key, &key, sizeof(key));')<create.index('pipeline_use_ready_sibling(pg, &key, hash)')
assert 'pgraph_vk_ref_state_modules(' not in fn(draw,'pipeline_use_ready_sibling')
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* PRODUCTION */',parts+'\n'+prefix)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='ready-pipeline-sibling-') as d:
 p=Path(d);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
 src.write_text(code);subprocess.run(cmd,env=env,check=True);subprocess.run([str(exe)],env=env,check=True,timeout=60)
 controls=[
  ('missing pipeline readiness',code.replace('if (!pipeline_ready(r,h,k))return NULL;','(void)pipeline_ready;(void)h;if (false)return NULL;',1)),
  ('shader hash collision accepted',code.replace('!memcmp(&binding->state, state, sizeof(*state))','state != NULL',1)),
  ('pipeline hash collision accepted',code.replace('!memcmp(&binding->key, key, sizeof(*key))','key != NULL',1)),
  ('unready vertex binding accepted',code.replace('binding->vsh.module_info && binding->psh.module_info','binding->psh.module_info',1)),
  ('pending cache key not owned',code.replace('memcpy(&snode->key, &key, sizeof(key));','memset(&snode->key, 0, sizeof(key));',1)),
  ('missing fragment owner',code.replace('pgraph_vk_ref_shader_module(psh);','(void)psh;',1)),
  ('queued key not owned',code.replace('memcpy(&job->key, key, sizeof(job->key));','memset(&job->key, 0, sizeof(job->key));',1)),
  ('missing uniform refresh',code.replace('    update_shader_uniforms(pg);','    (void)update_shader_uniforms;',1)),
  ('retry not requested',code.replace('pg->program_data_dirty = true;','(void)pg;',1)),
  ('duplicate job queued',code.replace('if (!pump_full_in_flight(full_hash, key)) {','if (true) {',1)),
 ]
 for name,mutant in controls:
  assert mutant!=code,name;src.write_text(mutant);subprocess.run(cmd,env=env,check=True)
  bad=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60);assert bad.returncode==1 and b'FAIL ' in bad.stderr,name
  print('Required negative control rejected:',name)
