#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real reservations and count tail refreshes in an extracted copy."""
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
reserve = function(r'^bool retro_spsc_write_reserve\(').replace('retro_spsc_write_reserve(', 'observed_reserve(', 1)
code = Path(__file__).with_suffix('.c').read_text(encoding='utf-8').replace('/* OBSERVED RESERVE */', helper + '\n' + reserve)
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='spsc-reserve-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_bytes(code.encode())
    exe = p / ('test.exe' if os.name == 'nt' else 'test')
    for checked in (True, False):
        flags = ['-fsanitize=undefined', '-fsanitize-undefined-trap-on-error'] if checked else []
        subprocess.run([cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I' + str(repo / 'libretro-common/include'), str(p / 'test.c'),
                        str(repo / 'libretro-common/queues/retro_spsc.c'), '-o', str(exe)], env=env, check=True)
        print('UBSan' if checked else 'optimized', flush=True)
        subprocess.run([str(exe)], env=env, check=True, timeout=120)
    for name, mutated in [
        ('missing acquire observer', code.replace(
            'q->cached_tail = retro_atomic_load_acquire_size(&q->tail);',
            'q->cached_tail = retro_atomic_load_relaxed_size(&q->tail);')),
        ('premature publication', code.replace(
            '*ptr = q->buffer + index;',
            'retro_atomic_store_release_size(&q->head, head + bytes);\n   *ptr = q->buffer + index;')),
    ]:
        assert mutated != code, name
        (p / 'test.c').write_bytes(mutated.encode())
        subprocess.run([cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                        '-I' + str(repo / 'libretro-common/include'), str(p / 'test.c'),
                        str(repo / 'libretro-common/queues/retro_spsc.c'), '-o', str(exe)], env=env, check=True)
        result = subprocess.run([str(exe)], env=env, capture_output=True, timeout=120)
        assert result.returncode != 0 and b'FAIL line' in result.stderr, name
        print('Negative control rejected: ' + name)
