# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production Vulkan ready-cache FIFO, slot reuse and reference ownership."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
s = Path(os.environ.get('VK_READY_SOURCE', r / 'hw/xbox/nv2a/pgraph/vk/shaders.c')).read_text()
def function(name):
    m = re.search(r'^static [^\n]*\b' + name + r'\([^;{]*\)\s*\{', s, re.M)
    if not m: return None
    a = m.start(); b = s.index('{', a) + 1; depth = 1
    while depth:
        depth += (s[b] == '{') - (s[b] == '}'); b += 1
    return s[a:b]
names = ('vk_spec_ready_position', 'vk_spec_ready_remove_order', 'vk_spec_ready_clear', 'vk_spec_ready_push', 'vk_spec_ready_take_at', 'vk_spec_take_ready')
pieces = [function(n) for n in names]
baseline = '#define VK_SPEC_READY_HEADROOM' not in s
if pieces[0] is None:
    pieces[0] = 'static unsigned vk_spec_ready_position(unsigned i) { return i; }'
if baseline:
    a = s.index('    for (unsigned i = 0; i < vk_spec_ready_n; i++) {', s.index('static void vk_spec_quiesce('))
    b = s.index('    vk_spec_modules_clear();', a)
    pieces[2] = 'static void vk_spec_ready_clear(void) {\n' + s[a:b] + '\n}'
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', '\n'.join(p for p in pieces if p))
with tempfile.TemporaryDirectory(prefix='vk-ready-') as tmp:
    p = Path(tmp); (p / 'test.c').write_text(code)
    binary = os.environ.get('VK_READY_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('VK_READY_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc') +
                   ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror'] + flags +
                   (['-DBASELINE'] if baseline else []) + [str(p / 'test.c'), '-o', binary], check=True)
    if not os.environ.get('VK_READY_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=60)
