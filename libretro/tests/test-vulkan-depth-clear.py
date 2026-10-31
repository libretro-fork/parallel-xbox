# SPDX-License-Identifier: GPL-2.0-or-later
"""Production Vulkan framebuffers/passes/clears with a larger depth image."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / 'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
parts = []
for name in ('create_render_pass', 'create_frame_buffer', 'begin_render_pass', 'pgraph_vk_clear_surface'):
    match = re.search(r'^(?:static )?(?:void|VkRenderPass) ' + name + r'\(', source, re.M)
    assert match, name
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    parts.append(source[match.start():end])
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', '\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='vulkan-depth-clear-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(code)
    exe = folder / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-sign-compare', '-Wno-type-limits', '-I' + str(repo), str(folder / 'test.c'),
                    '-lvulkan', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
