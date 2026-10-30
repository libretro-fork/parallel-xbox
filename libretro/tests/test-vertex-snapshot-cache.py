# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production vertex snapshot lookup, invalidation and arena recycling.
Usage: python test-vertex-snapshot-cache.py CHECKOUT [GCC]
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
draw = (repo / 'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
vertex = (repo / 'hw/xbox/nv2a/pgraph/vk/vertex.c').read_text()
header = (repo / 'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()

def function(source, name):
    match = re.search(r'^[A-Za-z_][A-Za-z0-9_ *]*\b' + name + r'\(', source, re.M)
    assert match, name
    start = source.index('{', match.start())
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

entry = re.search(r'typedef struct VertexCowCacheEntry \{.*?\} VertexCowCacheEntry;', header, re.S).group()
production = '\n'.join(function(draw, name) for name in [
    'vertex_cow_remember', 'vertex_cow_contains', 'vertex_cow_lookup',
    'vertex_cow_store'])
production += '\n' + function(vertex, 'pgraph_vk_vertex_cow_cache_invalidate')
code = Path(__file__).with_suffix('.c').read_text()
code = code.replace('/* ENTRY */', entry).replace('/* PRODUCTION */', production)
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='vertex-snapshot-', dir=repo.parent) as directory:
    path = Path(directory)
    (path / 'test.c').write_text(code)
    exe = path / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-sign-compare', '-fsanitize=undefined',
                    '-fsanitize-undefined-trap-on-error', str(path / 'test.c'),
                    '-o', str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env)
