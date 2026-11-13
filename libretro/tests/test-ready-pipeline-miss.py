# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual native cache miss adopts retained full-PSO metadata before compiling."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
draw=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
s=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text()
h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(source,name):
 m=re.search(r'^(?:static )?(?:inline )?(?:QEMU_ALWAYS_INLINE )?(?:void|bool) '+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 end=source.index('{',m.start())+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ('RenderPassState','PipelineKey','PipelineBinding'))
types+='\n'+s[s.index('enum {\n    VK_SHADER_HEAD_BYTES'):s.index('_Static_assert(offsetof(ShaderStateTail')]
query=fn(draw,'pgraph_vk_ref_ready_pipeline_modules')
for forbidden in ('lru_lookup','get_and_ref','vkCreate','pump_drain','shader_binding->'):
 assert forbidden not in query,forbidden
initializers='\n'.join(fn(s,n) for n in ('shader_cache_entry_init_modules','shader_cache_entry_init','shader_cache_entry_init_view','shader_cache_entry_compare_view'))
body=fn(s,'shader_cache_entry_init_modules')
assert body.index('pgraph_vk_ref_ready_pipeline_modules')<body.index('pgraph_vk_ref_state_modules')
assert 'r->shader_cache_owner = pg;' in fn(s,'shader_cache_init')
assert 'r->shader_cache_owner = NULL;' in fn(s,'shader_cache_finalize')
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* KEY AND QUERY */',fn(draw,'init_pipeline_key_for_shader')+'\n'+query.replace('pgraph_vk_ref_ready_pipeline_modules(','ready_query(',1)).replace('/* INITIALIZERS */',initializers)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='ready-pipeline-miss-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
 if os.name!='nt':flags+=['-fsanitize=address','-fno-omit-frame-pointer']
 def run(text,negative=False):
  src.write_text(text);subprocess.run(flags,env=env,check=True)
  result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60)
  if negative:assert result.returncode,'mutation escaped'
  else:assert result.returncode==0,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='')
 run(code)
 for label,before,after in [
  ('wrong explicit state','memcpy(&key->shader_state, state,','memcpy(&key->shader_state, &r->shader_binding->state,'),
  ('full-key hash collision','memcmp(&pipeline->key, &key, sizeof(key))','false'),
  ('wrong pipeline hash','node->hash != hash','false'),
  ('not-ready PSO','!pipeline->pipeline','false'),
  ('missing vertex acquisition','pgraph_vk_ref_shader_module(vsh);','(void)vsh;'),
  ('missing fragment acquisition','pgraph_vk_ref_shader_module(psh);','(void)psh;'),
  ('missing geometry acquisition','pgraph_vk_ref_shader_module(geom);','(void)geom;'),
  ('ready hit still compiles','if (!pgraph_vk_ref_ready_pipeline_modules(r->shader_cache_owner, binding))','if (!pgraph_vk_ref_ready_pipeline_modules(r->shader_cache_owner, binding) || true)'),
  ('uniform locations not prepared','    update_shader_uniform_locs(binding);','    (void)update_shader_uniform_locs;'),
 ]:
  assert code.count(before)==1,(label,code.count(before))
  run(code.replace(before,after,1),True);print('Required negative control rejected:',label)
