#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile real SPSC pair writes; check publication, wrap and concurrency.

Usage: python libretro/tests/test-spsc-pair.py REPO [CC]
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='spsc-pair-') as tmp:
    p = Path(tmp)
    common = [cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-pthread',
              '-I' + str(repo / 'libretro-common/include')]
    common += shlex.split(env.get('SPSC_PAIR_CFLAGS', ''))
    runner = shlex.split(env.get('SPSC_PAIR_RUNNER', ''))
    source = repo / 'libretro-common/queues/retro_spsc.c'
    production = source.read_text(encoding='utf8')
    marker = '#include <string.h>'
    assert production.count(marker) == 1
    # Define the redirect after string.h: otherwise fortified libc headers
    # rename their inline memcpy wrapper and bypass our external observer.
    observed_source = p / 'observed-queue.c'
    observed_source.write_text(production.replace(marker, marker + '\n'
        'extern void *observed_memcpy(void *, const void *, size_t);\n'
        '#define memcpy observed_memcpy', 1), encoding='utf8')
    for checked in (True, False):
        flags = ['-fsanitize=undefined', '-fsanitize-undefined-trap-on-error'] if checked else []
        obj = p / 'queue.o'
        exe = p / ('test.exe' if os.name == 'nt' else 'test')
        subprocess.run([*common, *flags, *(['-fno-builtin-memcpy'] if checked else []),
                        '-c', str(observed_source if checked else source), '-o', str(obj)], env=env, check=True)
        subprocess.run([*common, *flags, *(['-DOBSERVE_COPIES'] if checked else []),
                        str(Path(__file__).with_suffix('.c')), str(obj),
                        '-o', str(exe)], env=env, check=True)
        print('UBSan + copy-publication observer' if checked else 'optimized', flush=True)
        subprocess.run([*runner, str(exe)], env=env, check=True, timeout=120)

    # Both failures must be caught at runtime, including with fortify enabled.
    assert production.count('   if (first_bytes)\n') == 1
    for name, expected, text in (
        ('missing observer', b'copies >=', production),
        ('premature publication', b'== unpublished_head', observed_source.read_text(encoding='utf8').replace(
            '   if (first_bytes)\n',
            '   retro_atomic_store_release_size(&q->head, head + bytes);\n'
            '   if (first_bytes)\n', 1)),
    ):
        negative = p / 'negative-queue.c'
        negative.write_text(text, encoding='utf8')
        subprocess.run([*common, '-c', str(negative), '-o', str(obj)], env=env, check=True)
        subprocess.run([*common, '-DOBSERVE_COPIES', str(Path(__file__).with_suffix('.c')),
                        str(obj), '-o', str(exe)], env=env, check=True)
        result = subprocess.run([*runner, str(exe)], env=env, capture_output=True, timeout=120)
        assert result.returncode and b'SPSC pair:' in result.stderr and expected in result.stderr, name
        print('Negative control rejected: ' + name, flush=True)
