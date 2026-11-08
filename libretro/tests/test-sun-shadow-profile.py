# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate shadow profiles without a renderer; optional local XBE stays private."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo', type=Path)
parser.add_argument('--cc', default='gcc')
parser.add_argument('--xbe', type=Path)
parser.add_argument('--ram', type=Path, help='optional private 64-MiB RAM snapshot')
parser.add_argument('--cr3', type=lambda v: int(v, 0), help='snapshot CR3, required with --ram')
args = parser.parse_args()
if args.ram and args.cr3 is None:
    parser.error('--ram requires --cr3')
repo = args.repo.resolve()
with tempfile.TemporaryDirectory(prefix='sun-profile-') as folder:
    exe = Path(folder) / 'test.exe'
    subprocess.run([args.cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                    '-I' + str(repo), '-I' + str(repo / 'libretro-common/include'),
                    str(repo / 'libretro/tests/test-sun-shadow-profile.c'),
                    str(repo / 'libretro-common/hash/lrc_hash.c'),
                    str(repo / 'libretro-common/features/features_cpu.c'),
                    '-pthread', '-o', str(exe)],
                   check=True)
    subprocess.run([str(exe), *([str(args.xbe.resolve())] if args.xbe else [])],
                   check=True)
    if args.ram:
        subprocess.run([str(exe),'--ram',str(args.ram.resolve()),hex(args.cr3)],check=True)
