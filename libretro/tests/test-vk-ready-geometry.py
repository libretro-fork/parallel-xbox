#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual finite Vulkan GS source/layout, startup owners and metadata controls."""
from pathlib import Path
import os,re,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def fn(s,signature):
 a=s.index(signature);z=s.index('{',a)+1;depth=1
 while depth:depth+=(s[z]=='{')-(s[z]=='}');z+=1
 return s[a:z]
g=(r/'hw/xbox/nv2a/pgraph/glsl/geom.c').read_text();h=(r/'hw/xbox/nv2a/pgraph/glsl/geom.h').read_text();v=(r/'hw/xbox/nv2a/pgraph/vsh_regs.h').read_text();common=(r/'hw/xbox/nv2a/pgraph/glsl/common.c').read_text();native=(r/'hw/xbox/nv2a/pgraph/vk/glsl.c').read_text()
types='\n'.join(re.search(r'enum '+name+r' \{[^}]*\};',v)[0] for name in ['ShaderPrimitiveMode','ShaderPolygonMode'])+'\n'+'\n'.join(re.search(r'typedef struct (?:\w+ )?\{[^{}]*\} '+name+r';',h)[0] for name in ['GeomState','GenGeomGlslOptions'])
old=Path(__file__).with_name('test-spv-geom.c').read_text();strings=old[old.index('typedef struct {char *str;'):old.index('/* TYPES */')]
generator=fn(common,'MString *pgraph_glsl_get_vtx_header(')+'\n'+'\n'.join(fn(g,s) for s in ['static const char *get_vertex_order(', 'static void append_ui_prepare_impl(', 'static void append_ui_prepare(', 'static void append_ui_prepare_dynamic(', 'static void geom_rewrite_inputs(', 'static MString *pgraph_glsl_gen_geom_impl(', 'MString *pgraph_glsl_gen_geom_dynamic('])
mapping='\n'.join(fn(g,s) for s in ['int pgraph_glsl_geom_dynamic_variant(', 'void pgraph_glsl_geom_dynamic_state(', 'void pgraph_glsl_geom_dynamic_controls('])
production=(r/'hw/xbox/nv2a/pgraph/vk/ready-geometry.inc').read_text()
code=Path(__file__).with_suffix('.c').read_text().replace('/* STRINGS */',strings).replace('/* TYPES */',types).replace('/* GENERATOR */',generator).replace('/* MAPPING */',mapping).replace('/* FACTORY */',fn(native,'ShaderModuleInfo *pgraph_vk_create_optional_geometry_module(')).replace('/* PRODUCTION */',production)
env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vk-ready-geometry-') as tmp:
 d=Path(tmp);src=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
 command=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-sign-compare','-Wno-missing-field-initializers','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),'-I'+str(r/'deps/vulkan-headers'),str(src),str(r/'hw/xbox/nv2a/pgraph/spv/glslc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),'-lm','-o',str(exe)]
 def run(text,negative=False):
  src.write_text(text);subprocess.run(command,check=True,env=env);done=subprocess.run([str(exe),str(d)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env,timeout=120)
  if negative:assert done.returncode and b'FAIL GS' in done.stdout,done.stdout.decode(errors='replace')
  else:assert done.returncode==0,done.stdout.decode(errors='replace');print(done.stdout.decode(),end='')
 run(code)
 files=sorted(d.glob('*.spv'));assert len(files)==32
 if '--structural-only' in sys.argv[3:]:
  print('STRUCTURAL_ONLY:32 modules emitted; SPIR-V validation UNQUALIFIED in this invocation')
 else:
  validator=shutil.which('spirv-val',path=env['PATH'])
  if not validator:raise SystemExit('UNQUALIFIED: spirv-val unavailable (CPU source/layout/ownership checked)')
  for f in files:subprocess.run([validator,'--target-env','vulkan1.3',str(f)],check=True,env=env)
  print('All16 Vulkan dynamic GS families, both SPIR-V targets validated')
 for label,before,after in [('raw-wide-bit-controls-unused-metadata','module->ready_geometry_index == 3 || module->ready_geometry_index == 4','binding->state.geom.wide_lines'),('wrongslot','r->ready_geometry.stages[variant].module_info','r->ready_geometry.stages[(variant+1)%16].module_info'),('lostnativekind','key->native_kind != 1','false'),('badlayoutaccepted','uniform->offset != offsets[j]','(uniform->offset != offsets[j] && false)'),('lostreservation','lru_add_free(&r->shader_module_cache, &reserved[i].node);','(void)reserved;'),('stalesnapshot','module->uniform_staged_valid = false;','(void)module;'),('lostlastref','pgraph_vk_unref_shader_module(r, entry->module_info);','(void)entry;')]:
  assert before in production,label;run(code.replace(production,production.replace(before,after,1)),True);print('Required geometry fault rejected:',label)
