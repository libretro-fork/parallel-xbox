#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile real SPSC pair writes; check publication, wrap and concurrency.

Usage: python libretro/tests/test-spsc-pair.py REPO [CC]
"""
from pathlib import Path
import os
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
    for checked in (True, False):
        flags = ['-fsanitize=undefined', '-fsanitize-undefined-trap-on-error'] if checked else []
        obj = p / 'queue.o'
        exe = p / ('test.exe' if os.name == 'nt' else 'test')
        subprocess.run([*common, *flags, *(['-Dmemcpy=observed_memcpy', '-fno-builtin-memcpy'] if checked else []),
                        '-c', str(repo / 'libretro-common/queues/retro_spsc.c'), '-o', str(obj)], env=env, check=True)
        subprocess.run([*common, *flags, *(['-DOBSERVE_COPIES'] if checked else []),
                        str(Path(__file__).with_suffix('.c')), str(obj),
                        '-o', str(exe)], env=env, check=True)
        print('UBSan + copy-publication observer' if checked else 'optimized', flush=True)
        subprocess.run([str(exe)], env=env, check=True, timeout=120)
