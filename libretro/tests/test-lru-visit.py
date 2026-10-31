# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production LRU container against traversal/eviction oracles."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='lru-visit-') as directory:
    exe = Path(directory) / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                    '-I' + str(repo / 'include'), '-I' + str(repo / 'libretro-common/include'),
                    str(Path(__file__).with_suffix('.c')), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
