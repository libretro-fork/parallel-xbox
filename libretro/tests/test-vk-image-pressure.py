# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production allocation recovery without a driver, window or game."""
from pathlib import Path

vk_loader_root = Path(__file__).resolve().parents[2]
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()

def function(path, name):
    source = (repo / path).read_text()
    match = re.search(r'(?:static )?void ' + name + r'\([^;]*?\)\s*\{', source)
    assert match, name
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]

parts = [function('hw/xbox/nv2a/pgraph/vk/draw.c', name) for name in
         ('trash_drain_retired', 'poll_completed_draw_submissions', 'pgraph_vk_reclaim_retired_images')]
parts.append(function('hw/xbox/nv2a/pgraph/vk/instance.c', 'reclaim_retired_images'))
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', '\n'.join(parts))
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
env = dict(os.environ)
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='vk-image-pressure-') as tmp:
    source = Path(tmp) / 'test.c'
    exe = Path(tmp) / ('test.exe' if os.name == 'nt' else 'test')
    source.write_text(code)
    subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-type-limits', '-fsanitize=undefined',
                    '-fsanitize-undefined-trap-on-error', '-I' + str(repo),
                    '-I' + str(vk_loader_root / 'libretro/tests'),
                    '-I' + str(vk_loader_root / 'libretro'), '-I' + str(repo / 'deps/vulkan-headers'),
                    str(source), str(repo / 'hw/xbox/nv2a/pgraph/vk/memory.c'),
                    '-o', str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env)
