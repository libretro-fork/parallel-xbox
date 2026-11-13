# SPDX-License-Identifier: GPL-2.0-or-later
"""Borrowed vertex module lookup/adoption matches the original native key."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text();v=(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(source,name):
 m=re.search(r'^(?:static )?(?:inline )?(?:bool|void|int|uint64_t|ShaderModuleInfo \*) *'+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
 end=source.index('{',m.start())+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[m.start():end]
parts=s[s.index('enum {\n    VK_VERTEX_KEY_HEAD'):s.index('/* Prewarming needs only geometry and vertex keys;')]
parts+=fn(s,'vk_pre_state_module_keys')+'\n'+fn(v,'pgraph_glsl_vsh_canonicalize_lighting')
for name in ('pgraph_vk_ready_vertex_module','vertex_module_adopt','pgraph_vk_adopt_vertex_module'):
 for forbidden in ('get_and_ref','pgraph_vk_ref_state_modules','vkCreate','pump_drain','g_malloc'):
  assert forbidden not in fn(s,name),(name,forbidden)
types=re.search(r'typedef struct ShaderModuleCacheKey \{.*?\} ShaderModuleCacheKey;',h,re.S)[0]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* PRODUCTION */',parts)
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vertex-module-view-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),str(r/'util/fast-hash.c'),str(r/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)]
 if os.name!='nt':flags+=['-fsanitize=address','-fno-omit-frame-pointer']
 def run(text,negative=False):
  src.write_text(text);subprocess.run(flags,env=env,check=True)
  result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=60)
  if negative:assert result.returncode,'mutation escaped'
  else:assert result.returncode==0,result.stderr.decode(errors='replace');print(result.stdout.decode(),end='')
 run(code)
 for label,before,after in [
  ('missing header hash','xxh3_update(&hash, view->head, sizeof(view->head));','(void)0;'),
  ('missing target/options hash','xxh3_update(&hash, view->tail, sizeof(view->tail));','(void)0;'),
  ('missing acquired module owner','pgraph_vk_ref_shader_module(view->module);','(void)pgraph_vk_ref_shader_module;'),
  ('wrong collision identity','memcmp(&entry->key.vsh.state, view->state, sizeof(*view->state))','false'),
  ('lighting normalization remains specialized','    f->normalization = false;','    (void)f;'),
 ]:
  assert code.count(before)==1,(label,code.count(before))
  run(code.replace(before,after,1),True);print('Required negative control rejected:',label)
