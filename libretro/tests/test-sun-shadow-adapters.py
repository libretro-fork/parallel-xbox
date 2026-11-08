# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise FIFO camera correlation and the bounded native cache lease owner."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo', type=Path)
parser.add_argument('--cc', default='gcc')
args = parser.parse_args()
repo = args.repo.resolve()
with tempfile.TemporaryDirectory(prefix='sun-adapters-') as directory:
    exe = Path(directory) / 'test.exe'
    subprocess.run([args.cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-const-variable', '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
        '-I'+str(repo), '-I'+str(repo/'deps/volk'), '-I'+str(repo/'deps/vulkan-headers'),
        '-I'+str(repo/'libretro-common/include'),
        str(repo/'libretro/tests/test-sun-shadow-adapters.c'), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
