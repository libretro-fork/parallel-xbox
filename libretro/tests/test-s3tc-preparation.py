# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production compute jobs and queue functions with controlled native-call interleavings."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc';s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();header=(r/'hw/xbox/nv2a/pgraph/vk/s3tc-pipeline-job.h').read_text()
def fn(source,name):
 m=re.search(r'^(?:static )?(?:void \*|void|bool|LibJob \*) *'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 p=source.index('{',m.start())+1;depth=1
 while depth:depth+=(source[p]=='{')-(source[p]=='}');p+=1
 return source[m.start():p]
meta=re.search(r'typedef struct S3tcPipelineJob \{.*?\} S3tcPipelineJob;',header,re.S)[0]
job=re.search(r'typedef struct \{\n    bool frag;.*?\} LibJob;',s,re.S)[0]
names=['pipeline_job_reset','pipe_worker_fn','s3tc_pipeline_job_land','pump_drain','pump_quiesce','pump_slot','pump_submit','pgraph_vk_s3tc_pipeline_ready','pgraph_vk_s3tc_pipeline_uncovered','pgraph_vk_quiesce_pipeline_worker']
parts='\n'.join(fn(s,n) for n in names)
# A deterministic worker step resumes the actual loop's cursor, and stops at
# its completion notification after the production release publication.
parts=parts.replace('unsigned pr = 0;','unsigned pr = cursor;')
ready=fn(s,'pgraph_vk_s3tc_pipeline_ready')
for banned in ['vkCreate','Wait(','pump_quiesce(','pump_drain(','malloc(','memcpy(']:assert banned not in ready,banned
preflight=(r/'hw/xbox/nv2a/pgraph/vk/s3tc-decode.h').read_text()
assert preflight.index('pgraph_vk_s3tc_pipeline_ready(pg,volume)')<preflight.index('pgraph_vk_staging_alloc(pg, total')
finalize=fn(preflight,'s3tc_decode_finalize');assert finalize.index('pgraph_vk_quiesce_pipeline_worker(pg)')<finalize.index('if (!r->s3tc_decode.set_layout)')
prototypes='bool pgraph_vk_s3tc_pipeline_ready(PGRAPHState*,unsigned);\n'
code=Path(__file__).with_suffix('.c').read_text().replace('/* JOB */',meta+'\n'+job).replace('/* BUILDER */',fn(header,'s3tc_pipeline_job_create')).replace('/* FUNCTIONS */',parts+'\n'+finalize)
code=code.replace('static VkResult vkCreateComputePipelines',prototypes+'static VkResult vkCreateComputePipelines',1)
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='s3tc-preparation-') as tmp:
 d=Path(tmp);p=d/'test.c';p.write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'deps/vulkan-headers'),str(p),str(r/'hw/xbox/nv2a/pgraph/spv/utility.c'),'-o',str(exe)]
 env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','');subprocess.run(cmd,check=True,env=env);subprocess.run([str(exe)],check=True,env=env,timeout=30)

 controls=[
  ('first miss falsely ready',code.replace('    pump_submit(job);\n    return false;','    pump_submit(job);\n    return true;',1)),
  ('upload creates compute pipeline',code.replace('    pump_submit(job);','    s3tc_pipeline_job_create(job->device,job->cache,&job->layout,&job->s3tc,&job->result);\n    pump_submit(job);',1)),
  ('worker reads mutable target',code.replace('SPV_UTILITY_S3TC_ARRAY,job->spirv_1_6);','SPV_UTILITY_S3TC_ARRAY,pgraph_vk_spirv_1_6);',1)),
  ('duplicate layout request',code.replace('(!r->s3tc_decode.layout && r->s3tc_decode.pending_mask)','false',1)),
  ('layout owner retires before quiesce',code.replace('    pgraph_vk_quiesce_pipeline_worker(pg);','    /* owner retired too early */',1)),
  ('unpublished result adopted',code.replace('unsigned done=(unsigned)retro_atomic_load_acquire_int(&pump_r);','unsigned done=pump_w_local;',1)),
  ('optional upload starts thread',code.replace('if (!pipe_started) { return false; }','if (!pipe_started) { pipe_start(); }',1)),
  ('wrong variant published',code.replace('.volume=volume,.spirv_1_6=','.volume=volume^1u,.spirv_1_6=',1)),
 ]
 for name,mutant in controls:
  assert mutant!=code,name;p.write_text(mutant);subprocess.run(cmd,check=True,env=env)
  result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
  assert result.returncode and b'S3TC preparation line' in result.stderr,(name,result.stdout,result.stderr)
  print('Required S3TC preparation control rejected:',name)
