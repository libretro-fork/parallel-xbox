# SPDX-License-Identifier: GPL-2.0-or-later
"""Production fused RGB655 helper and GL/Vulkan caller oracle."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
def function(path, name):
    text = (r / path).read_text()
    m = re.search(r'^(?:static )?(?:const uint8_t \*|uint8_t \*|void |size_t )' + name + r'\([^;{]*\)\s*\{', text, re.M)
    assert m, name
    end = text.index('{', m.start()) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end]

common = 'hw/xbox/nv2a/pgraph/texture.c'
gl = 'hw/xbox/nv2a/pgraph/gl/texture.c'
vk = 'hw/xbox/nv2a/pgraph/vk/texture.c'
parts = [function(common, n) for n in ('convert_r6g5b5_pixel', 'pgraph_convert_swizzled_rgb655_into', 'pgraph_converted_texture_size', 'pgraph_convert_texture_data_into')]
parts += [function(gl, n) for n in ('tex_scratch', 'unswizzle_level')]
parts += [function(vk, 'stage_level')]
# stage_swizzled returns void, but its declaration has the standard spacing.
parts += [function(vk, 'stage_swizzled')]
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', '\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='rgb655-unswizzle-') as tmp:
    p = Path(tmp); (p / 'test.c').write_text(code)
    cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
    binary = os.environ.get('RGB655_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('RGB655_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run([cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-sign-compare', *flags, '-I'+str(r),
                    '-I'+str(r/'hw/xbox/nv2a/pgraph'), str(p/'test.c'),
                    str(r/'hw/xbox/nv2a/pgraph/swizzle.c'), '-o', binary], check=True)
    if not os.environ.get('RGB655_SKIP_RUN'):
        subprocess.run([binary, *sys.argv[3:]], check=True, timeout=120)
