# SPDX-License-Identifier: GPL-2.0-or-later
"""Check LRU promotions/count gates against original scans and protected metadata."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='lru-promote-') as tmp:
    p = Path(tmp)
    (p / 'qemu').mkdir()
    source = Path(os.environ.get('LRU_PROMOTE_SOURCE', r / 'include/qemu/lru.h'))
    (p / 'qemu/lru.h').write_bytes(source.read_bytes())
    binary = os.environ.get('LRU_PROMOTE_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('LRU_PROMOTE_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc') +
                   ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function'] + flags +
                   ['-I'+str(p), '-I'+str(r/'include'), '-I'+str(r/'libretro-common/include'),
                    str(Path(__file__).with_suffix('.c')), '-o', binary], check=True)
    if not os.environ.get('LRU_PROMOTE_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=60)
