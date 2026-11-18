#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Ordinary source equality for the finite register-driven FF bank."""
from pathlib import Path
import ast,os,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
native=len(sys.argv)>3 and sys.argv[3]=='native'
script=(r/'libretro/tests/test-spv-vsh-gpu.py').read_text()
tree=ast.parse(script);nodes=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='function'];assert len(nodes)==1
ns=dict(Path=Path);exec(compile(ast.Module(body=nodes,type_ignores=[]),'<ordinary function extractor>','exec'),ns);function=ns['function']
env=dict(os.environ);env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='gl-ready-ff-source-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir()
 (d/'qemu/osdep.h').write_text('''#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define stringify1(x) #x
#define stringify(x) stringify1(x)
''')
 ref='''#include "hw/xbox/nv2a/pgraph/glsl/vsh.h"
#include "hw/xbox/nv2a/pgraph/glsl/vsh-prog.h"
#include "hw/xbox/nv2a/pgraph/glsl/vsh-ff.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#define DECL_NAME(t) #t,
const char *uniform_element_type_to_str[]={UNIFORM_ELEMENT_TYPE_X(DECL_NAME)};
DEF_UNIFORM_INFO_ARR(VshUniform,VSH_UNIFORM_DECL_X)
'''
 ref+='#include "'+(r/'hw/xbox/nv2a/pgraph/glsl/vsh-controls.inc').as_posix()+'"\n'
 ref+=function(r/'hw/xbox/nv2a/pgraph/glsl/common.c','pgraph_glsl_get_vtx_header')+'\n'
 ref+=function(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c','pgraph_glsl_gen_vsh')+'\n'
 (d/'reference.c').write_text(ref)
 driver=Path(__file__).with_suffix('.c')
 sources=[d/'reference.c',r/'hw/xbox/nv2a/pgraph/glsl/vsh-prog.c',r/'hw/xbox/nv2a/pgraph/glsl/vsh-ff.c',r/'libretro/glib/gcore.c']
 libs=[]
 if native:
  # Extract unchanged ordinary geometry and prepared fragment builders. The
  # context and native APIs are real; only the LRU records are bounded mocks.
  import importlib.util
  path=Path(__file__).with_name('test-geom-dynamic-gl.py')
  spec=importlib.util.spec_from_file_location('geometry_fixture',path);geometry=importlib.util.module_from_spec(spec);spec.loader.exec_module(geometry)
  take=geometry.function
  geom=(r/'hw/xbox/nv2a/pgraph/glsl/geom.c').read_text()
  g='#include "hw/xbox/nv2a/pgraph/glsl/geom.h"\n'
  for signature in ['static const char *get_vertex_order(', 'static void append_ui_prepare_impl(', 'static void append_ui_prepare(', 'static void append_ui_prepare_dynamic(', 'static void geom_rewrite_inputs(', 'static MString *pgraph_glsl_gen_geom_impl(', 'MString *pgraph_glsl_gen_geom(', 'MString *pgraph_glsl_gen_geom_dynamic(', 'int pgraph_glsl_geom_dynamic_variant(', 'void pgraph_glsl_geom_dynamic_state(', 'void pgraph_glsl_geom_dynamic_controls(']:
   g+=take(geom,signature)+'\n'
  (d/'geometry.c').write_text(g)
  psh=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text()
  p='#include "hw/xbox/nv2a/pgraph/glsl/psh.h"\n#include "hw/xbox/nv2a/nv2a_regs.h"\n#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"\n#define NV2A_UNIMPLEMENTED(...) ((void)0)\n#define DECL_NAME(t) #t,\nDEF_UNIFORM_INFO_ARR(PshUniform,PSH_UNIFORM_DECL_X)\n'
  p+=psh[psh.index('struct InputInfo {'):psh.index('void pgraph_glsl_set_psh_uniform_values(')]
  (d/'fragment.c').write_text(p)
  context=(r/'libretro/tests/test-geom-dynamic-gl.c').read_text()
  context=take(context,'static void quiet_abort(')+'\n'+take(context,'static void init_gl(')
  code=Path(__file__).with_name('test-gl-ready-ff-native.c').read_text()
  code=code.replace('/* CONTEXT */',context)
  code=code.replace('/* PRODUCTION */',(r/'hw/xbox/nv2a/pgraph/gl/pipeline-owners.inc').read_text()+'\n'+(r/'hw/xbox/nv2a/pgraph/gl/ready-ff.inc').read_text())
  driver=d/'native.c';driver.write_text(code)
  sources += [d/'geometry.c',d/'fragment.c']
  if os.name=='nt':
   sources += [r/p for p in ['ui/gl-dispatch.c','libretro-common/glsym/rglgen.c','libretro-common/glsym/glsym_gl.c','hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c']]
   libs=['-lopengl32','-lgdi32','-luser32']
  else:libs=['-lEGL','-lGL']
  env.update(EGL_PLATFORM='surfaceless',LP_NUM_THREADS='1')
 sources.insert(0,driver)
 if os.name=='nt':
  sources += [r/'libretro/glib/gcontainers.c']+[r/'libretro-common'/f for f in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']]
 flags=[cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro/glib'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen')]
 exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run(flags+list(map(str,sources))+['-Wl,--gc-sections','-lm',*libs,'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=300 if native else 120)
