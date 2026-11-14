#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual reserved native geometry stage owner orchestration."""
from pathlib import Path
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

root=Path(sys.argv[1]).resolve()
cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
path=Path(__file__).with_name('test-geom-dynamic-gl.py')
spec=importlib.util.spec_from_file_location('geometry_fixture',path)
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
geom=(root/'hw/xbox/nv2a/pgraph/glsl/geom.c').read_text(encoding='utf-8')
header=(root/'hw/xbox/nv2a/pgraph/glsl/geom.h').read_text(encoding='utf-8')
enums=(root/'hw/xbox/nv2a/pgraph/vsh_regs.h').read_text(encoding='utf-8')
types='\n'.join(re.search(r'enum '+n+r' \{[^}]*\};',enums)[0]
                for n in ['ShaderPrimitiveMode','ShaderPolygonMode'])
types+='\n'+'\n'.join(re.search(r'typedef struct (?:\w+ )?\{[^{}]*\} '+n+r';',header)[0]
                     for n in ['GeomState','GenGeomGlslOptions'])
mapping='\n'.join(fixture.function(geom,signature) for signature in [
    'int pgraph_glsl_geom_dynamic_variant(', 'void pgraph_glsl_geom_dynamic_state(',
    'void pgraph_glsl_geom_dynamic_controls('])
template=Path(__file__).with_suffix('.c').read_text(encoding='utf-8')
template=template.replace('/* TYPES */',types).replace('/* MAPPING */',mapping)
template=template.replace('#define GEOM_DYNAMIC_VARIANTS 22',re.search(r'#define GEOM_DYNAMIC_VARIANTS \d+',header)[0])
source=(root/'hw/xbox/nv2a/pgraph/gl/ready-geom.inc').read_text(encoding='utf-8')
shader=(root/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text(encoding='utf-8')
final=fixture.function(shader,'void pgraph_gl_finalize_shaders(')
assert final.index('lru_flush(&r->shader_cache)')<final.index('ready_geom_finalize(r)')<final.index('g_free(r->shader_module_cache_entries)')
eviction=fixture.function(shader,'static void shader_cache_entry_post_evict(')
assert eviction.index('ready_geom_invalidate(r, binding)')<eviction.index('memset(&binding->state')
env=dict(os.environ)
env['PATH']=str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='ready-geom-owners-') as temporary:
    d=Path(temporary);c=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
    def run(text):
        c.write_text(template.replace('/* PRODUCTION */',text),encoding='utf-8')
        subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                        '-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(c),'-o',str(exe)],check=True,env=env)
        return subprocess.run([str(exe)],env=env,capture_output=True,timeout=30)
    result=run(source);assert result.returncode==0,result.stderr
    print(result.stdout.decode(),end='')
    for name,before,after in [
        ('lost invalidation','r->ready_geom.uploaded_binding = NULL;','(void)r;'),
        ('wrong finite slot','&r->ready_geom.stages[variant];','&r->ready_geom.stages[(variant+1)%GEOM_DYNAMIC_VARIANTS];'),
        ('lost partial reservation','lru_add_free(&r->shader_module_cache, &reserved[j].node);','(void)reserved;'),
        ('linked shader object retained','glDetachShader(stage->gl_stage_program, stage->gl_shader);','(void)glDetachShader;'),
        ('lost native deletion','delete_program(r, stage->gl_stage_program);','(void)delete_program;'),
        ('premature publication','for (unsigned i = 0; i < GEOM_DYNAMIC_VARIANTS; i++) {\n        GeomState state;',
         'r->ready_geom.ready=true;\n    for (unsigned i = 0; i < GEOM_DYNAMIC_VARIANTS; i++) {\n        GeomState state;'),
    ]:
        assert before in source,name
        result=run(source.replace(before,after));assert result.returncode!=0,(name,result.stdout)
        print('Required geometry ownership control rejected:',name)
