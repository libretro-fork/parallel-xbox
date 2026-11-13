#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production constant-attribute staging, reuse and vertex bindings."""
from pathlib import Path
import os, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'

def function(path, signature):
    text = path.read_text()
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

shader = r / 'hw/xbox/nv2a/pgraph/vk/shaders.c'
draw = r / 'hw/xbox/nv2a/pgraph/vk/draw.c'
instance = r / 'hw/xbox/nv2a/pgraph/vk/instance.c'
vertex = (r / 'hw/xbox/nv2a/pgraph/vk/vertex.c').read_text()
assert vertex.count('r->constant_vertex_inputs ? pg->uniform_attrs : 0') == 2
buffer = (r / 'hw/xbox/nv2a/pgraph/vk/buffer.c').read_text()
uniform = buffer[buffer.index('r->storage_buffers[BUFFER_UNIFORM] ='):]
uniform = uniform[:uniform.index('r->storage_buffers[BUFFER_UNIFORM_STAGING] =')]
assert 'VK_BUFFER_USAGE_VERTEX_BUFFER_BIT' in uniform
barrier = draw.read_text().split('case BUFFER_UNIFORM:', 1)[1].split('break;', 1)[0]
assert 'VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT' in barrier
assert 'VK_PIPELINE_STAGE_VERTEX_INPUT_BIT' in barrier
production = '\n'.join([
    function(instance, 'static bool constant_vertex_inputs_supported('),
    function(shader, 'static bool constant_vertex_snapshot_valid('),
    function(shader, 'static void stage_constant_vertex_values('),
    function(shader, 'static VkDeviceSize uniform_staging_span('),
    function(draw, 'static void bind_constant_vertex_descriptions('),
    function(draw, 'static void bind_vertex_buffer('),
])
template = Path(__file__).with_suffix('.c').read_text()
env = dict(os.environ)
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='constant-vertex-') as tmp:
    d = Path(tmp)
    src = d / 'test.c'
    exe = d / ('test.exe' if os.name == 'nt' else 'test')
    flags = [cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             '-Wno-sign-compare', '-fsanitize=undefined',
             '-fsanitize-undefined-trap-on-error', '-I'+str(r/'deps/vulkan-headers'),
             str(src), '-o', str(exe)]
    def build(text):
        src.write_text(template.replace('/* PRODUCTION */', text))
        subprocess.run(flags, check=True, env=env)
    if os.name != 'nt':
        flags += ['-fsanitize=address', '-fno-omit-frame-pointer']
    build(production)
    subprocess.run([str(exe)], check=True, env=env, timeout=30)
    for label, before, after in [
        ('portability', '!is_extension_available(extensions, "VK_KHR_portability_subset")', 'is_extension_available(extensions, "VK_KHR_portability_subset")'),
        ('generation', 'r->constant_vertex_staged_epoch != r->uniform_upload_epoch', 'false'),
        ('snapshot bytes', 'if (memcmp(row, pg->vertex_attributes[i].inline_value,', 'if (0 && memcmp(row, pg->vertex_attributes[i].inline_value,'),
        ('stride', '.stride = 0,', '.stride = 16,'),
        ('pressure span', 'return end - start;', 'return end - start + 1;'),
        ('packed row', '.offset = offset,', '.offset = offset + 16,'),
    ]:
        assert production.count(before) == 1, label
        build(production.replace(before, after, 1))
        p = subprocess.run([str(exe)], capture_output=True, env=env, timeout=30)
        assert p.returncode, label
        print('Required constant-vertex control rejected:', label)
