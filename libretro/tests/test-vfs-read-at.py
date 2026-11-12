# SPDX-License-Identifier: GPL-2.0-or-later
"""Production VFS version negotiation and cursor-free reads, plus v3 fallback."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv)>2 else 'cc'
default = '-static' if os.name == 'nt' or 'mingw' in cc else '-fsanitize=address,undefined'
flags = shlex.split(os.environ.get('VFS_READ_AT_CFLAGS', default))
with tempfile.TemporaryDirectory(prefix='vfs-read-at-') as tmp:
    exe = Path(tmp) / 'test.exe'
    subprocess.run([cc, '-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(repo), '-I'+str(repo/'libretro-common/include'),
                    *flags, str(Path(__file__).with_suffix('.c')), '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
