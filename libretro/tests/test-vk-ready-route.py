# SPDX-License-Identifier: GPL-2.0-or-later
"""Mode-aware cache owners and finite->exotic->finite without guest-key changes."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/shaders.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text();ready=(r/'hw/xbox/nv2a/pgraph/vk/ready-vertex.inc').read_text()
def fn(text,name):
 m=re.search(r'^(?:static )?(?:bool|void|ShaderBinding \*) *'+name+r'\([^;{]*\)\s*\{',text,re.M);assert m,name
 end=text.index('{',m.start())+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]
head=s[s.index('enum {\n    VK_SHADER_HEAD_BYTES'):s.index('static inline int vk_pre_state_module_keys')]
types=re.search(r'typedef struct ReadyBindingInput \{.*?\} ReadyBindingInput;',s,re.S)[0]
parts='\n'.join(fn(s,n) for n in ['shader_cache_entry_init','shader_cache_entry_init_view','shader_cache_entry_compare','shader_cache_entry_compare_view','ready_binding_compare','ready_binding_init','pgraph_vk_ready_shader_binding','get_shader_binding_for_state'])+'\n'+fn(ready,'vk_ready_vertex_route')
bind=fn(s,'pgraph_vk_bind_shaders');start=bind.index('    bool route = ');end=bind.index('    r->shader_bindings_changed = false;',start)
parts+='\nstatic void refresh_route(PGRAPHState *pg){PGRAPHVkState *r=pg->vk_renderer_state;\n'+bind[start:end]+'\n}'
assert bind.count('r->shader_binding->ready_vertex_route != r->ready_vertex.route')==3
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',head+'\n'+types).replace('/* PRODUCTION */',parts)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-ready-route-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n');src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
 def run(text,negative=False):
  src.write_text(text);subprocess.run(cmd,env=env,check=True);v=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
  if negative:assert v.returncode,'Required namespace control escaped'
  else:assert v.returncode==0,v.stderr.decode();print(v.stdout.decode(),end='')
 run(code)
 for label,needle,replace in [('normal namespace lost','snode->ready_vertex_route != r->ready_vertex.route ||','(r != NULL && false) ||'),('ready query namespace lost','binding->ready_vertex_route == r->ready_vertex.route &&','true &&'),('view namespace lost','binding->ready_vertex_route != r->ready_vertex.route ||\n           memcmp','(r != NULL && false) ||\n           memcmp'),('mode refresh omitted','r->ready_vertex.route = route;','(void)route;')]:
  text=code.replace(needle,replace,1);assert text!=code,label;run(text,True);print('Required negative control rejected:',label)
 # The new independent FS guard sits between these existing native/VS
 # guards. Keep the same native-view omission control on both source eras.
 next_guard='ready_fragment_route' if 'ready_fragment_route' in parts else 'ready_vertex_route'
 for label,needle,replace in [('native object namespace lost','snode->shader_object_route != r->shader_objects.enabled ||','(r != NULL && false) ||'),('native ready query namespace lost','binding->shader_object_route == r->shader_objects.enabled &&','true &&'),('native view namespace lost','binding->shader_object_route != r->shader_objects.enabled ||\n           binding->'+next_guard,'(r != NULL && false) ||\n           binding->'+next_guard)]:
  text=code.replace(needle,replace,1);assert text!=code,label;run(text,True);print('Required negative control rejected:',label)
