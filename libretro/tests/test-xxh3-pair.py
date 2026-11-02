#!/usr/bin/env python3
"""Compare the real two-span hash with an independent concatenation oracle.

Usage: python libretro/tests/test-xxh3-pair.py REPO [CC]
Each backend runs in a separate process because dispatch is cached.
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
with tempfile.TemporaryDirectory(prefix='xxh3-pair-') as tmp:
    exe = Path(tmp) / ('test.exe' if os.name == 'nt' else 'test')
    for checked in (True, False):
        flags = ['-fsanitize=undefined', '-fsanitize-undefined-trap-on-error'] if checked else []
        subprocess.run([
            cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', *flags,
            '-I' + str(repo / 'libretro-common/include'),
            str(Path(__file__).with_suffix('.c')),
            str(repo / 'libretro-common/encodings/encoding_xxh3.c'),
            '-o', str(exe),
        ], env=env, check=True)
        print('UBSan' if checked else 'optimized', flush=True)
        for backend in range(4):
            result = subprocess.run([str(exe), str(backend)], env=env, timeout=120)
            if result.returncode == 77:
                print(f'backend={backend}: unsupported on host; skipped', flush=True)
            elif result.returncode:
                raise SystemExit(result.returncode)
