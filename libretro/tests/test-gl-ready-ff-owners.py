#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual finite FF initialization, rollback, tuple borrowing and retirement."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(r/'hw/xbox/nv2a/pgraph/gl/ready-ff.inc').read_text()
owner=(r/'hw/xbox/nv2a/pgraph/gl/pipeline-owners.inc').read_text()
template=Path(__file__).with_suffix('.c').read_text()
shader=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text()
init=shader[shader.index('void pgraph_gl_init_shaders('):shader.index('static void module_prewarm_finish(')]
assert init.index('r->ready_ff.pipelines =')<init.index('for (int i = reserved_bindings;')
assert init.index('r->ready_ff.stages =')<init.index('for (int i = reserved_geometry;')
assert 'reserved_bindings += GL_READY_FF_PIPELINE_COUNT;' in init
assert 'reserved_geometry += GL_READY_FF_STAGE_COUNT;' in init
assert init.index('ready_ff_init(r)')>init.index('ready_psh_init(r)')>init.index('ready_geom_init(r)')
final=shader[shader.index('void pgraph_gl_finalize_shaders('):shader.index('static void shader_write_to_disk(')]
assert final.index('lru_flush(&r->shader_cache)')<final.index('ready_ff_pipelines_finalize(r)')<final.index('free(r->shader_cache_entries)')
assert final.index('lru_flush(&r->shader_module_cache)')<final.index('ready_ff_stages_finalize(r)')<final.index('g_free(r->shader_module_cache_entries)')
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='gl-ready-ff-owners-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir()
 (d/'qemu/osdep.h').write_text('#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <assert.h>\n')
 (d/'qemu/mstring.h').write_text('typedef struct MString {unsigned index;} MString;\n')
 c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 def run(text):
  c.write_text(template.replace('/* PRODUCTION */',owner+'\n'+text))
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),str(c),'-o',str(exe)],env=env,check=True)
  return subprocess.run([str(exe)],env=env,capture_output=True,timeout=60)
 result=run(source);assert result.returncode==0,result.stderr;print(result.stdout.decode(),end='',flush=True)
 for before,after in [
  ('smooth * 2u + geometry','smooth * 2u + !geometry'),
  ('glValidateProgramPipeline(anchor->gl_pipeline);','(void)glValidateProgramPipeline;'),
  ('pipeline_owner_publish(r, anchor);','(void)pipeline_owner_publish;'),
  ('pipeline_owner_release(r, &r->ready_ff.pipelines[i]);','(void)pipeline_owner_release;'),
  ('lru_add_free(&r->shader_cache, &pipelines[i].node);','(void)lru_add_free;'),
  ('lru_add_free(&r->shader_module_cache, &stages[i].node);','(void)lru_add_free;'),
  ('if (stage->gl_stage_program) delete_program(r, stage->gl_stage_program);','(void)delete_program;'),
  ('assert(!r->ready_ff.ready);','assert(!r->ready_ff.ready);r->ready_ff.ready=true;'),
 ]:
  assert source.count(before)==1,before
  result=run(source.replace(before,after));assert result.returncode==86,(before,result.stderr)
  print('Required FF owner fault rejected:',before,flush=True)
