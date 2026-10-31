# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real Vulkan finish/begin code with delayed fence retirement."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / 'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
header = (repo / 'hw/xbox/nv2a/pgraph/vk/renderer.h').read_text()


def function(name):
    match = re.search(r'^(?:static )?(?:void|bool) ' + name + r'\(', source, re.M)
    assert match, name
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


enum = re.search(r'typedef enum FinishReason \{.*?\} FinishReason;', header, re.S)[0]
code = Path(__file__).with_suffix('.c').read_text()
code = code.replace('/* ENUM */', enum).replace('/* PRODUCTION */', '\n'.join(
    function(name) for name in ('pgraph_vk_trash_push', 'pipeline_cache_entry_post_evict',
                               'finish_is_async', 'pgraph_vk_finish',
                               'pgraph_vk_begin_command_buffer')))
with tempfile.TemporaryDirectory(prefix='vk-recording-epoch-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(code)
    exe = folder / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([sys.argv[2] if len(sys.argv) > 2 else 'gcc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                    '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                    '-I' + str(repo / 'deps/vulkan-headers'), str(folder / 'test.c'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
