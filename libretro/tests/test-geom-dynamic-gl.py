#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact native GS outputs and source identity against a pinned ordinary builder.

Requires the f3d5d52760 parent object, or --reference with that geom.c file.
The numerical fixture uses real uniform controls and separable native stages;
its transform-feedback readbacks and reference compiles are test-only work.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import tempfile

BASE = 'f3d5d52760bccdf0d48cfc175f311267d5e3866e'


def function(source, signature):
    start = source.index(signature)
    p = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[p] == '{') - (source[p] == '}')
        p += 1
    return source[start:p]


def prepare(root, baseline, mutation=''):
    geom = (root/'hw/xbox/nv2a/pgraph/glsl/geom.c').read_text(encoding='utf-8')
    common = (root/'hw/xbox/nv2a/pgraph/glsl/common.c').read_text(encoding='utf-8')
    header = (root/'hw/xbox/nv2a/pgraph/glsl/geom.h').read_text(encoding='utf-8')
    enums = (root/'hw/xbox/nv2a/pgraph/vsh_regs.h').read_text(encoding='utf-8')
    types = '\n'.join(re.search(r'enum '+n+r' \{[^}]*\};', enums)[0]
                      for n in ['ShaderPrimitiveMode', 'ShaderPolygonMode'])
    types += '\n'+'\n'.join(re.search(r'typedef struct (?:\w+ )?\{[^{}]*\} '+n+r';', header)[0]
                            for n in ['GeomState', 'GenGeomGlslOptions'])
    old = (root/'libretro/tests/test-spv-geom.c').read_text(encoding='utf-8')
    strings = old[old.index('typedef struct {char *str;'):old.index('/* TYPES */')]
    generators = function(common, 'MString *pgraph_glsl_get_vtx_header(')+'\n'
    for signature in ['static const char *get_vertex_order(', 'static void append_ui_prepare_impl(',
                      'static void append_ui_prepare(', 'static void append_ui_prepare_dynamic(',
                      'static void geom_rewrite_inputs(',
                      'static MString *pgraph_glsl_gen_geom_impl(',
                      'MString *pgraph_glsl_gen_geom(', 'MString *pgraph_glsl_gen_geom_dynamic(',
                      'int pgraph_glsl_geom_dynamic_variant(', 'void pgraph_glsl_geom_dynamic_state(',
                      'void pgraph_glsl_geom_dynamic_controls(']:
        generators += function(geom, signature)+'\n'
    reference = function(baseline, 'MString *pgraph_glsl_gen_geom(')
    reference = reference.replace('pgraph_glsl_gen_geom(', 'baseline_gen_geom(', 1)
    reference = reference.replace('append_ui_prepare(', 'baseline_append_ui_prepare(')
    reference = function(baseline, 'static void append_ui_prepare(').replace('append_ui_prepare(', 'baseline_append_ui_prepare(', 1)+'\n'+reference
    text = Path(__file__).with_suffix('.c').read_text(encoding='utf-8')
    constant = re.search(r'#define GEOM_DYNAMIC_VARIANTS \d+', header)[0]
    text = text.replace('#define GEOM_DYNAMIC_VARIANTS 22', constant)
    for marker, value in [('STRING_HELPERS', strings), ('TYPES', types),
                          ('GENERATORS', generators), ('BASELINE', reference)]:
        assert text.count('/* '+marker+' */') == 1
        text = text.replace('/* '+marker+' */', value)
    mutations = {
        'depth': ('if ((gsControl.x & 1) != 0)', 'if (false)'),
        'ui_enable': ('/* MUTATE_CONTROLS */', 'controls[0]&=1;'),
        'ui_stage': ('/* MUTATE_CONTROLS */', 'if((controls[0]>>1)&7)controls[0]=(controls[0]&1)|((((controls[0]>>1)&7)%4+1)<<1);'),
        'provoking': ('/* MUTATE_CONTROLS */', 'controls[1]=0;'),
        'rotation': ('/* MUTATE_CONTROLS */', 'controls[2]=controls[3]=0;'),
    }
    if mutation:
        before, after = mutations[mutation]
        assert text.count(before) == 1
        text = text.replace(before, after)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkout', type=Path)
    parser.add_argument('cc', nargs='?', default='cc')
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--keep', type=Path)
    args = parser.parse_args()
    root = args.checkout.resolve()
    baseline = args.reference.read_text(encoding='utf-8') if args.reference else subprocess.check_output(
        ['git', '-C', str(root), 'show', BASE+':hw/xbox/nv2a/pgraph/glsl/geom.c']).decode('utf-8')
    env = dict(os.environ, EGL_PLATFORM='surfaceless', LP_NUM_THREADS='1')
    env['PATH'] = str(Path(shutil.which(args.cc) or args.cc).resolve().parent)+os.pathsep+env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='native-geom-controls-') as temporary:
        directory = args.keep.resolve() if args.keep else Path(temporary)
        directory.mkdir(exist_ok=True, parents=True)
        for mutation in ['', 'depth', 'provoking', 'rotation', 'ui_enable', 'ui_stage']:
            tag = mutation or 'positive'
            c = directory/(tag+'.c')
            c.write_text(prepare(root, baseline, mutation), encoding='utf-8')
            exe = directory/(tag+('.exe' if os.name == 'nt' else ''))
            command = [args.cc, '-O2', '-std=gnu11', '-ffunction-sections', '-fdata-sections',
                       '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                       '-I'+str(root), '-I'+str(root/'libretro-common/include'),
                       '-I'+str(root/'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen'), str(c)]
            if os.name == 'nt':
                # WGL exports use PROC; the platform loader casts those handles.
                command += ['-Wno-cast-function-type']
                command += [str(root/p) for p in ['ui/gl-dispatch.c',
                    'libretro-common/glsym/rglgen.c', 'libretro-common/glsym/glsym_gl.c',
                    'hw/xbox/nv2a/pgraph/thirdparty/gloffscreen/gloffscreen_libretro.c']]
                command += ['-lopengl32', '-lgdi32', '-luser32']
            else:
                command += ['-lEGL', '-lGL']
            subprocess.run(command+['-Wl,--gc-sections', '-lm', '-o', str(exe)], check=True, env=env)
            with (directory/(tag+'.log')).open('w') as stream:
                result = subprocess.run([str(exe)], env=env, stdout=stream,
                                        stderr=subprocess.STDOUT, timeout=300)
            text = (directory/(tag+'.log')).read_text(errors='replace')
            if not mutation:
                print(text, end='', flush=True)
                assert result.returncode == 0, result.returncode
            else:
                assert result.returncode != 0 and ('difference lane=' in text or
                                                   'CPU color oracle path=1' in text), text
                print('Required geometry control rejected:', mutation, flush=True)


if __name__ == '__main__':
    main()
