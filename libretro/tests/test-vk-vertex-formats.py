# SPDX-License-Identifier: GPL-2.0-or-later
"""Production format initialization, array remapping and inline layout conversion."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
draw = (repo / 'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
vertex = (repo / 'hw/xbox/nv2a/pgraph/vk/vertex.c').read_text()
common = (repo / 'hw/xbox/nv2a/pgraph/vertex.c').read_text()
header = (repo / 'hw/xbox/nv2a/pgraph/pgraph.h').read_text()


def function(source, name):
    match = re.search(r'^(?:static )?(?:inline )?(?:__attribute__\(\(noinline\)\) )?(?:unsigned )?\w+ ' + name + r'\(', source, re.M)
    assert match, name
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


tables = '\n'.join(re.search(r'static const VkFormat ' + name + r'\[\] = \{.*?\};', vertex, re.S)[0]
                    for name in ('float_to_count', 'ub_to_count', 's1_to_count', 's32k_to_count'))
parts = [tables, function(common, 'pgraph_update_inline_value'), function(header, 'pgraph_inline_attr_size')]
parts += [function(vertex, name) for name in ('pgraph_vk_init_vertex_formats', 'pgraph_vk_bind_vertex_attributes')]
parts += [function(draw, name) for name in ('get_size_and_count_for_format', 'remap_unaligned_attributes',
           'copy_remapped_attributes_to_inline_buffer', 'upload_converted_inline_array')]
remap = re.search(r'typedef struct VertexBufferRemap \{.*?\} VertexBufferRemap;', draw, re.S)[0]
code = Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */', remap).replace('/* PRODUCTION */', '\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='vk-vertex-formats-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(code)
    exe = folder / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-sign-compare',
                    '-fno-strict-aliasing', '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                    '-I' + str(repo), '-I' + str(repo / 'deps/vulkan-headers'),
                    str(folder / 'test.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
