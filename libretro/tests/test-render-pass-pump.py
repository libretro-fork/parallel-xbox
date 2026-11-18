# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded native render-pass scheduling and key-owned attachment metadata."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
revision=os.environ.get('PUMP_TEST_REVISION')
def source(path):
 return subprocess.check_output(['git','-C',str(r),'show',revision+':'+path],text=True) if revision else (r/path).read_text()
s=source('hw/xbox/nv2a/pgraph/vk/draw.c');h=source('hw/xbox/nv2a/pgraph/vk/renderer.h')
def fn(name):
 m=re.search(r'^(?:static )?(?:bool|void) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
types='\n'.join(re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',h,re.S)[0] for n in ('RenderPassState','RenderPass'))
pump=re.search(r'struct \{\s*RenderPassState rp\[4\];.*?\} pump;',h,re.S)[0]
pump=pump.replace('struct {','typedef struct PumpProgress {',1).replace('} pump;','} PumpProgress;')
types+='\n'+pump+'\n'+pump.replace('PumpProgress','PumpProgressBefore').replace('unsigned next_pass;','')
registered='static void pump_register_render_pass(' in s
if any(marker in s for marker in ('pump_register_render_pass(', '*rp = r->pump.rp[', 'if (r->pump.retry[i]) {')):
 assert registered,'Advertised observed-pass registry must include its registration helper'
parts=(fn('pump_register_render_pass')+'\n' if registered else '')+fn('pump_next_render_pass')+'\n'+fn('pgraph_vk_libpump_step')
assert 'unsigned budget = 4, checks = 32;' in parts
assert 'init_render_pass_state' not in parts
fill=re.search(r'^static void pipeline_job_fill\(.*?^\}',s,re.M|re.S)[0]
depth=re.search(r'    if \([^\n]*\) \{\n        job->ds = depth_stencil;\n    \}',fill)[0]
color=re.search(r'\.attachmentCount = ([^,\n]+)',fill)[1]
extra=Path(__file__).with_name('test-render-pass-pump-registry.c').read_text() if registered else 'static void registry_cases(void) {}'
if registered:
 finalize=fn('pgraph_vk_finalize_pipelines')
 start=finalize.index('    for (unsigned i = 0; i < r->pump.n; i++)')
 end=finalize.index('    destroy_framebuffer_cache',start)
 extra=extra.replace('/* RELEASE */',finalize[start:end])
 ordinary=fn('create_pipeline');prewarm=fn('pgraph_vk_prewarm_shaders')
 assert 'pump_register_render_pass' not in fn('create_clear_pipeline'),'Utility clear pipelines must not register preparation recipes'
 assert ordinary.index('init_pipeline_key(pg, &key);') < ordinary.index('pump_register_render_pass(r, &key.render_pass_state);') < ordinary.index('pipeline_ahead(pg, &key);')
 assert prewarm.index('init_render_pass_state(pg, &pipeline.render_pass);') < prewarm.index('pump_register_render_pass(r, &pipeline.render_pass);') < prewarm.index('pgraph_vk_queue_pre_modules')
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* PRODUCTION */',parts).replace('/* DEPTH */',depth).replace('/* COLOR */',color).replace('/* REGISTRY TESTS */',extra)
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
  ('only final bound pass prepared',code.replace('*rp = r->pump.rp[r->pump.next_pass++];','r->pump.next_pass++; *rp = r->live;',1) if registered else code.replace('*rp = pass->state;','(void)pass; *rp = r->live;',1)),
  ('round-robin cursor stuck',code.replace('r->pump.next_pass++','r->pump.next_pass',1)),
  ('preparation budget inflated',code.replace('unsigned budget = 4, checks = 32;','unsigned budget = 8, checks = 32;',1)),
  ('depth attachment from live surface',code.replace('key->render_pass_state.zeta_format != VK_FORMAT_UNDEFINED','r->zeta_binding',1)),
  ('color attachment from live surface',code.replace('key->render_pass_state.color_format != VK_FORMAT_UNDEFINED','r->color_binding',1)),
  ('cursor not normalized after owner change',code.replace('r->pump.next_pass = 0;','r->pump.next_pass = 999;',1)),
  ('empty bank still consumes owners',code.replace('if (!n) {','if (false && !n) {',1)),
 ]
 if registered:
  controls += [(name,code.replace(token,'false',1)) for name,token in [('disabled seeding admitted','!g_config.perf.shader_seeding'),('missing cull/front capability admitted','!r->dyn_cull_front'),('missing blend capability admitted','!r->dyn_blend'),('missing vertex input capability admitted','!r->dyn_vertex_input')]]
  controls += [
   ('registry admits utility native owners',code.replace('if (!r->pump.n) {','if (!r->pump.n && r->render_passes->len) { RenderPass *p = &g_array_index(r->render_passes, RenderPass, 0); pump_register_render_pass(r, &p->state); }\n    if (!r->pump.n) {',1)),
   ('late ordinary registration lost',code.replace('r->pump.rp[i] = *rp;','r->pump.rp[i] = r->live;',1)),
   ('registry dedup omitted',code.replace('if (!memcmp(&r->pump.rp[i], rp, sizeof(*rp))) {','if (false && !memcmp(&r->pump.rp[i], rp, sizeof(*rp))) {',1)),
   ('registry capacity guard omitted',code.replace('if (r->pump.n == ARRAY_SIZE(r->pump.rp)) {','if (false && r->pump.n == ARRAY_SIZE(r->pump.rp)) {',1)),
   ('retry allocation eager',code.replace('r->pump.retry[i] = NULL;','r->pump.retry[i] = g_array_new(FALSE, FALSE, sizeof(unsigned));',1)),
   ('NULL retry lifecycle unguarded',code.replace('if (r->pump.retry[i]) {','if (true) {',1)),
   ('real depth-only ordinary pass filtered',code.replace('if (!g_config.perf.shader_seeding || !r->dyn_cull_front ||','if (!rp->color_format || !g_config.perf.shader_seeding || !r->dyn_cull_front ||',1)),
  ]
 for name,mutant in controls:
  assert mutant!=code,name;run(mutant,True);print('Required negative control rejected:',name)
