# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production surface resolution, including the render-target memo."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
surface = (repo / 'hw/xbox/nv2a/pgraph/vk/surface.c').read_text()
header = (repo / 'hw/xbox/nv2a/pgraph/pgraph.h').read_text()


def function(source, name):
    match = re.search(r'^(?:static )?(?:inline )?(?:void|bool) ' + name + r'\(', source, re.M)
    assert match, name
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


parts = [function(header, 'pgraph_set_surface_binding_dim')]
parts += [function(surface, name) for name in ('check_surface_compatibility',
           'zeta_covers_color', 'set_surface_binding_dimensions', 'update_surface_part')]
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', '\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='vk-depth-reuse-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(code)
    exe = folder / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-sign-compare', '-fsanitize=undefined',
                    '-fsanitize-undefined-trap-on-error', '-I' + str(repo),
                    str(folder / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
