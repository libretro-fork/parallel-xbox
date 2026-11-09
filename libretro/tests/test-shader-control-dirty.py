# SPDX-License-Identifier: GPL-2.0-or-later
"""Control-register dirty filtering against actual shader-consumer expressions."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/glsl/shaders.c').read_text()
assert int(re.search(r'shader_reg_snap\[(\d+)\]',(r/'hw/xbox/nv2a/pgraph/pgraph.h').read_text())[1])>=12
assert 'shader_reg_snap' not in (r/'hw/xbox/nv2a/nv2a.c').read_text(), 'host snapshots must not change the migration schema'
def fn(s,name):
 m=re.search(r'^(?:static )?(?:bool|void) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
sources={n:(r/('hw/xbox/nv2a/pgraph/glsl/'+n+'.c')).read_text() for n in ('vsh','geom','psh')}
allowed=('NV_PGRAPH_CONTROL_0_ALPHATESTENABLE','NV_PGRAPH_CONTROL_0_ALPHAFUNC','NV_PGRAPH_CONTROL_0_ALPHAREF','NV_PGRAPH_CONTROL_0_ZENABLE','NV_PGRAPH_CONTROL_0_Z_PERSPECTIVE_ENABLE','NV_PGRAPH_CONTROL_3_SHADEMODE','NV_PGRAPH_CONTROL_3_PROVOKING_VERTEX','NV_PGRAPH_CONTROL_3_FOGENABLE','NV_PGRAPH_CONTROL_3_FOG_MODE')
for source in sources.values():
 for name in set(re.findall(r'NV_PGRAPH_CONTROL_[03]_[A-Z0-9_]+',source)):
  assert any(name==a or name.startswith(a+'_') for a in allowed),'new shader control dependency: '+name
def assignment(source,owner,field):
 return re.search(re.escape(owner+'->'+field)+r'\s*=.*?;',source,re.S)[0]
oracle='\n'.join(assignment(sources['vsh'],'vsh',n) for n in ('z_perspective','smooth_shading','fog_enable','fog_mode'))
oracle+='\n{GeomState *state=&out.state.geom;'+''.join(assignment(sources['geom'],'state',n) for n in ('smooth_shading','first_vertex_is_provoking','z_perspective'))+'}\n'
oracle+='{PshState *state=&out.state.psh;uint32_t control=pgraph_reg_r(pg,NV_PGRAPH_CONTROL_0),raster=0;'+''.join(assignment(sources['psh'],'state',n) for n in ('alpha_test','z_perspective','smooth_shading'))
oracle+=re.search(r'bool ui_possible =.*?;',sources['psh'],re.S)[0]+'out.ui=ui_possible;}\n'
parts='\n'.join(fn(s,n) for n in ('pgraph_glsl_snapshot_shader_regs','shader_reg_dirty','signed_g8b8_address_dirty','pgraph_glsl_check_shader_state_dirty'))
parts+='\n'+fn((r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text(),'pgraph_gl_uniform_sources_invalidate')
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',parts).replace('/* ORACLE */',oracle)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='shader-control-dirty-') as d:
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
  ('whole control register invalidates shader',code.replace('return (pgraph_reg_r(pg, reg) & fields) != pg->shader_reg_snap[slot];','(void)fields; (void)slot; return true;',1)),
  ('UI depth-enable dependency omitted',code.replace('NV_PGRAPH_CONTROL_0_SHADER_FIELDS, 9','(NV_PGRAPH_CONTROL_0_SHADER_FIELDS & ~NV_PGRAPH_CONTROL_0_ZENABLE), 9',1)),
  ('fog-mode dependency omitted',code.replace('NV_PGRAPH_CONTROL_3_SHADER_FIELDS, 10','(NV_PGRAPH_CONTROL_3_SHADER_FIELDS & ~NV_PGRAPH_CONTROL_3_FOG_MODE), 10',1)),
  ('control snapshot stale',code.replace('pg->shader_reg_snap[9] = pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0) &','pg->shader_reg_snap[9] = 0 * pgraph_reg_r(pg, NV_PGRAPH_CONTROL_0) &',1)),
  ('host resolution transition ignored',code.replace('(pg->surface_scale_factor > 1) != pg->shader_reg_snap[11]','false',1)),
  ('legacy sibling wakeups discarded',code.replace('state->psh.combiner_dynamic &&','false && state->psh.combiner_dynamic &&',1)),
  ('restored GL shader view stays live',code.replace('r->shader_binding = NULL;','(void)r;',1)),
 ]
 for name,mutant in controls:
  assert mutant!=code,name;run(mutant,True);print('Required negative control rejected:',name)
