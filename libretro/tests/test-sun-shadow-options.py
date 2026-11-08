# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate actual option registration, including frontends without hiding support."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo',type=Path)
parser.add_argument('--cc',default='gcc')
args=parser.parse_args();repo=args.repo.resolve()
with tempfile.TemporaryDirectory(prefix='sun-options-') as folder:
    for intl in (False,True):
        exe=Path(folder)/('intl.exe' if intl else 'english.exe')
        subprocess.run([args.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
            *([] if intl else ['-DHAVE_NO_LANGEXTRA']),'-I'+str(repo),
            '-I'+str(repo/'libretro-common/include'),
            str(repo/'libretro/tests/test-sun-shadow-options.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
