#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual finite pipeline startup/rollback and draw-owner borrowing."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
owner=(r/'hw/xbox/nv2a/pgraph/gl/pipeline-owners.inc').read_text()
source=(r/'hw/xbox/nv2a/pgraph/gl/ready-pipelines.inc').read_text()
template=Path(__file__).with_suffix('.c').read_text()
shader=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
init=shader[shader.index('void pgraph_gl_init_shaders('):shader.index('static void module_prewarm_finish(')]
assert 'reserved_bindings = 18;' in init and 'for (int i = reserved_bindings;' in init
assert init.index('ready_pipelines_init(r)')>init.index('ready_psh_init(r)')>init.index('ready_geom_init(r)')>init.index('ready_vsh_init(r)')
final=shader[shader.index('void pgraph_gl_finalize_shaders('):shader.index('static void shader_write_to_disk(')]
assert final.index('lru_flush(&r->shader_cache)')<final.index('ready_pipelines_finalize(r)')<final.index('free(r->shader_cache_entries)')<final.index('ready_psh_finalize(r)')

if (Path(sys.argv[1])/'hw/xbox/nv2a/pgraph/gl/ready-config.h').is_file():
    assert 'bool clean_entry = false;' in source, 'Advertised default preparation lacks checked startup safety'
if 'bool clean_entry = false;' in source:
    template='#define FIXTURE_CHECKED_STARTUP 1\n'+template

env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='ready-pipelines-') as tmp:
 d=Path(tmp);c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 def run(text):
  c.write_text(template.replace('/* PRODUCTION */',owner+'\n'+text))
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(c),'-o',str(exe)],check=True,env=env)
  return subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
 result=run(source);assert result.returncode==0,result.stderr;print(result.stdout.decode(),end='',flush=True)
 for name,before,after in [
  ('wrong interpolation','unsigned smooth = geometry ? (i & 1) : i - 16;','unsigned smooth = geometry ? !(i & 1) : i - 16;'),
  ('lost validation','glValidateProgramPipeline(anchor->gl_pipeline);','(void)glValidateProgramPipeline;'),
  ('lost index publication','pipeline_owner_publish(r, anchor);','(void)pipeline_owner_publish;'),
  ('lost failed native deletion','glDeleteProgramPipelines(1, &anchor->gl_pipeline);','(void)glDeleteProgramPipelines;'),
  ('lost final retirement','pipeline_owner_release(r, &r->ready_pipelines[i]);','(void)pipeline_owner_release;'),
  ('lost slot return','lru_add_free(&r->shader_cache, &reserved[i].node);','(void)lru_add_free;'),
 ]:
  assert before in source,name
  result=run(source.replace(before,after));assert result.returncode==86,(name,result.stdout,result.stderr)
  print('Required ready pipeline control rejected:',name,flush=True)

 if 'bool clean_entry = false;' in source:
     mutations=[
         ('own startup errors retained', source.replace('i < 8;', 'i < 1;')),
         ('prior startup error ignored', source.replace('if (error != GL_NO_ERROR) {','if (false) {',1)),
         ('publication errors ignored',source.replace('(error = glGetError())','GL_NO_ERROR').replace('    error = glGetError();\n    if (error != GL_NO_ERROR) {','    error = GL_NO_ERROR;\n    if (error != GL_NO_ERROR) {')),
     ]
     for name,mutated in mutations:
         assert mutated!=source,name
         result=run(mutated)
         assert result.returncode==86 and result.stderr,(name,result.returncode,result.stdout,result.stderr)
         print('Required checked-startup fault rejected:',name,flush=True)
