# SPDX-License-Identifier: GPL-2.0-or-later
"""Run production blend mapping, pipeline keys and dynamic-state caching."""
from pathlib import Path
import re
import os
import subprocess
import sys
import tempfile
import shlex

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv)>2 and sys.argv[2]!='--gl' else 'cc'
source = (repo/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
parts = [re.search(r'static const unsigned pipeline_registers\[\] = \{.*?\};',source,re.S)[0]]
for name in ('pipeline_cache_entry_init', 'pipeline_reg_values', 'check_render_pass_dirty', 'check_pipeline_dirty', 'init_pipeline_key', 'color_write_mask', 'emit_dynamic_state'):
    match = re.search(r'^static (?:inline )?(?:bool|void|VkColorComponentFlags) '+name+r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    end=source.index('{', match.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    parts.append(source[match.start():end])
# The real fast-path prefix must return before key construction when clean.
a=source.index('    NV2A_VK_DGROUP_BEGIN("Creating pipeline")',source.index('static void create_pipeline'))
b=source.index('    PipelineKey key;',a)
parts.append('static void pipeline_prefix(PGRAPHState *pg) {\n'+source[a:b]+'\n    key_rebuilds++;\n}')
constants = (repo/'hw/xbox/nv2a/pgraph/vk/constants.h').read_text()
maps = '\n'.join(re.search(r'static const [^;]+ '+name+r'\[\] = \{.*?\};', constants, re.S)[0]
                 for name in ('pgraph_blend_factor_vk_map','pgraph_blend_equation_vk_map'))
code = Path(__file__).with_suffix('.c').read_text().replace('/* MAPS */', maps).replace('/* PRODUCTION */', '\n'.join(parts))
gl = (repo/'hw/xbox/nv2a/pgraph/gl/draw.c').read_text()
gl_constants = (repo/'hw/xbox/nv2a/pgraph/gl/constants.h').read_text()
gl_maps = '\n'.join(re.search(r'static const [^;]+ '+name+r'\[\] = \{.*?\};', gl_constants, re.S)[0]
                    for name in ('pgraph_blend_factor_gl_map','pgraph_blend_equation_gl_map'))
gl_masks = []
lines = gl.splitlines()
for i, line in enumerate(lines):
    if line.startswith('#define GLST_'):
        gl_masks.append(line)
        while lines[i].rstrip().endswith('\\'):
            i += 1
            gl_masks.append(lines[i])
gl_masks = '\n'.join(gl_masks)
start = gl.index('    uint32_t blend = pgraph_surface_blend(')
end = gl.index('    /* Face culling */', start)
gl_function = ('static void apply_gl_draw_state(PGRAPHState *pg) {\n'
               'PGRAPHVkState *r=pg->vk_renderer_state;\n'
               'bool mask_red=true,mask_green=true,mask_blue=true,mask_alpha=true;\n'
               'uint32_t control_0=pgraph_reg_r(pg,NV_PGRAPH_CONTROL_0);\n'
               + gl[start:end] + '\ngl_state_done: return;\n}')
code = code.replace('/* GL PRODUCTION */', gl_masks+'\n'+gl_maps+'\n'+gl_function)
with tempfile.TemporaryDirectory(prefix='surface-blend-') as directory:
    folder=Path(directory);(folder/'test.c').write_text(code)
    exe=Path(os.environ.get('SURFACE_BLEND_BINARY',str(folder/'test')))
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                    '-Wno-unused-parameter','-Wno-sign-compare','-Wno-unused-variable',
                    *shlex.split(os.environ.get('SURFACE_BLEND_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error')),'-I'+str(repo),
                    '-I'+str(repo/'deps/vulkan-headers'),str(folder/'test.c'),'-o',str(exe)],check=True)
    if not os.environ.get('SURFACE_BLEND_SKIP_RUN'):subprocess.run([str(exe)],check=True,timeout=30)
    if '--gl' in sys.argv:
        subprocess.run([cc,
                        '-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                        '-Wno-sign-compare','-Wno-unused-variable','-Wno-unused-function','-DREAL_GL',
                        '-I'+str(repo),'-I'+str(repo/'libretro/tests'),'-I'+str(repo/'deps/vulkan-headers'),
                        str(folder/'test.c'),'-lEGL','-lGL','-lm','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=30,env=dict(os.environ,EGL_PLATFORM='surfaceless'))
