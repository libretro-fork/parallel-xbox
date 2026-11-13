# SPDX-License-Identifier: GPL-2.0-or-later
"""Scoped immutable worker-key lends, native dirty guards and load invalidation."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();shader=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text();glsl=(r/'hw/xbox/nv2a/pgraph/glsl/shaders.c').read_text();renderer=(r/'hw/xbox/nv2a/pgraph/vk/renderer.c').read_text()
def fn(s,name):
 m=re.search(r'^(?:static )?(?:bool|void|const ShaderState \*|ShaderBinding \*) *'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
parts='\n'.join([fn(glsl,n) for n in ('shader_reg_dirty','signed_g8b8_address_dirty','pgraph_glsl_check_shader_state_dirty')]+[fn(draw,'pgraph_vk_pending_shader_state'),fn(draw,'pgraph_vk_pending_vertex_family'),fn(shader,'pgraph_vk_ready_shader_binding'),fn(shader,'vk_try_pending_binding')])
borrow=fn(draw,'pgraph_vk_pending_shader_state');select=fn(shader,'vk_try_pending_binding')
assert 'pump_drain' not in borrow and 'get_shader_binding_for_state(r, &binding->state,' in select
assert 'pgraph_glsl_get_shader_state' not in select and 'memcpy' not in select
assert 'if (r->sibling_retry && vk_try_pending_binding(pg))' in fn(shader,'pgraph_vk_bind_shaders')
flush=re.search(r'    pg->vk_renderer_state->sibling_retry = false;\n    pg->vk_renderer_state->shader_binding = NULL;',fn(renderer,'pgraph_vk_flush'))[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',parts).replace('/* FLUSH */',flush)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='pending-key-lend-') as d:
 p=Path(d);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),str(src),'-o',str(exe)]
 def run(text,negative=False):
  src.write_text(text);subprocess.run(cmd,env=env,check=True)
  result=subprocess.run([str(exe)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
  if negative:assert result.returncode,'negative control escaped'
  else:assert result.returncode==0,result.stdout.decode(errors='replace');print(result.stdout.decode(),end='')
 run(code)
 controls=[
  ('changed raster/pass/VI keeps generic lend',code.replace('(r->sibling_vertex_family && check_pipeline_dirty(pg))','(check_pipeline_dirty(pg), false)',1)),
  ('expired/recycled slot lent',code.replace('ticket - pump_d >= pump_w_local - pump_d','false',1)),
  ('changed family generation ignored',code.replace('r->sibling_generation != pgraph_glsl_dynamic_gen','false',1)),
  ('feature disabled but old lend used',code.replace('!pgraph_gpu_boost_gl() ||','false ||',1)),
  ('guest shader writes ignored',code.replace('!pgraph_glsl_check_shader_state_dirty(pg, state)','true',1)),
  ('ring key retained through cache promotion',code.replace('get_shader_binding_for_state(r, &binding->state,','get_shader_binding_for_state(r, state,',1)),
  ('retry not invalidated on miss',code.replace('r->sibling_retry = false;','r->sibling_retry = true;',1)),
  ('completion fails to request real capture',code.replace('pg->program_data_dirty = true;','(void)pg;',1)),
  ('load leaves old lend active',code.replace('pg->vk_renderer_state->sibling_retry = false;','(void)pg;',1)),
  ('load keeps old host shader view',code.replace('pg->vk_renderer_state->shader_binding = NULL;','(void)pg;',1)),
 ]
 for name,mutant in controls:
  assert mutant!=code,name;run(mutant,True);print('Required negative control rejected:',name)
