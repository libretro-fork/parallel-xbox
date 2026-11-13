# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded native render-pass scheduling and key-owned attachment metadata."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()
def fn(name):
 m=re.search(r'^(?:static )?(?:bool|void) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ('RenderPassState','RenderPass'))
pump=re.search(r'struct \{\s*RenderPassState rp\[4\];.*?\} pump;',h,re.S)[0]
pump=pump.replace('struct {','typedef struct PumpProgress {',1).replace('} pump;','} PumpProgress;')
types+='\n'+pump+'\n'+pump.replace('PumpProgress','PumpProgressBefore').replace('unsigned next_pass;','')
parts=fn('pump_next_render_pass')+'\n'+fn('pgraph_vk_libpump_step')
assert 'unsigned budget = 4, checks = 32;' in parts
assert 'init_render_pass_state' not in parts
fill=re.search(r'^static void pipeline_job_fill\(.*?^\}',s,re.M|re.S)[0]
depth=re.search(r'    if \([^\n]*\) \{\n        job->ds = depth_stencil;\n    \}',fill)[0]
color=re.search(r'\.attachmentCount = ([^,\n]+)',fill)[1]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* PRODUCTION */',parts).replace('/* DEPTH */',depth).replace('/* COLOR */',color)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='render-pass-pump-') as d:
 p=Path(d);(p/'qemu').mkdir();(p/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n');(p/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 src=p/'test.c';exe=p/('test.exe' if os.name=='nt' else 'test')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(p),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),'-o',str(exe)]
 def run(text,negative=False):
  src.write_text(text);subprocess.run(cmd,env=env,check=True)
  result=subprocess.run([str(exe)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
  if negative:assert result.returncode,'negative control escaped'
  else:assert result.returncode==0,result.stdout.decode(errors='replace');print(result.stdout.decode(),end='')
 run(code)
 controls=[
  ('lighting owner service skipped',code.replace('    prepare_bound_vertex_family(pg);','    (void)prepare_bound_vertex_family;',1)),
  ('only final bound pass prepared',code.replace('*rp = pass->state;','(void)pass; *rp = r->live;',1)),
  ('round-robin cursor stuck',code.replace('r->pump.next_pass++','r->pump.next_pass',1)),
  ('preparation budget inflated',code.replace('unsigned budget = 4, checks = 32;','unsigned budget = 8, checks = 32;',1)),
  ('depth attachment from live surface',code.replace('key->render_pass_state.zeta_format != VK_FORMAT_UNDEFINED','r->zeta_binding',1)),
  ('color attachment from live surface',code.replace('key->render_pass_state.color_format != VK_FORMAT_UNDEFINED','r->color_binding',1)),
  ('cursor not normalized after owner change',code.replace('r->pump.next_pass = 0;','r->pump.next_pass = 999;',1)),
  ('empty bank still consumes owners',code.replace('if (!n) {','if (false && !n) {',1)),
 ]
 for name,mutant in controls:
  assert mutant!=code,name;run(mutant,True);print('Required negative control rejected:',name)
