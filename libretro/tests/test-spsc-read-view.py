#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Real two-span consumer views, observed cursor refreshes and failing controls."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
source = (repo / 'libretro-common/queues/retro_spsc.c').read_text(encoding='utf-8')

def function(pattern):
    m = re.search(pattern, source, re.M)
    assert m, pattern
    a = source.index('{', m.start())
    b, depth = a + 1, 1
    while depth:
        depth += (source[b] == '{') - (source[b] == '}')
        b += 1
    return source[m.start():b]
helper = function(r'^static INLINE size_t spsc_contig\(')
begin = function(r'^size_t retro_spsc_read_begin_pair\(').replace('retro_spsc_read_begin_pair(', 'observed_begin(', 1)
code = Path(__file__).with_suffix('.c').read_text(encoding='utf-8-sig').replace('/* OBSERVED BEGIN */', helper + '\n' + begin)

env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='spsc-read-view-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_bytes(code.encode())
    exe = p / ('test.exe' if os.name == 'nt' else 'test')
    command = [cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
               '-I' + str(repo / 'libretro-common/include'), str(p / 'test.c'),
               str(repo / 'libretro-common/queues/retro_spsc.c'), '-o', str(exe)]
    for checked in (True, False):
        flags = ['-fsanitize=undefined', '-fsanitize-undefined-trap-on-error'] if checked else []
        subprocess.run(command + flags, env=env, check=True)
        print('UBSan' if checked else 'optimized', flush=True)
        subprocess.run([str(exe)], env=env, check=True, timeout=120)
    for name, old, new in [
        ('missing acquire observer', 'q->cached_head = retro_atomic_load_acquire_size(&q->head);',
         'q->cached_head = retro_atomic_load_relaxed_size(&q->head);'),
        ('premature consumption', 'return bytes;',
         'retro_atomic_store_release_size(&q->tail, tail + bytes);\n   return bytes;'),
        ('wrong wrapped span', '*second = q->buffer;', '*second = q->buffer + 1;'),
        ('eager cursor refresh', 'if (avail < bytes)', 'if (avail < q->capacity)'),
    ]:
        assert old in begin, name
        mutated = code.replace(begin, begin.replace(old, new, 1), 1)
        assert mutated != code, name
        (p / 'test.c').write_bytes(mutated.encode())
        subprocess.run(command + ['-Wno-unused-function'], env=env, check=True)
        result = subprocess.run([str(exe)], env=env, capture_output=True, timeout=120)
        assert result.returncode != 0 and b'FAIL line' in result.stderr, name
        print('Negative control rejected: ' + name, flush=True)
