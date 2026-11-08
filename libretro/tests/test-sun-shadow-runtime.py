# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the production RAM-only virtual reader and shadow identity lifecycle."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo',type=Path)
parser.add_argument('--cc',default='gcc')
args=parser.parse_args();repo=args.repo.resolve()
source=(repo/'xemu-xbe.c').read_text()
start=source.index('bool xemu_read_virtual_ram(')
end=source.index('\n}\n',start)+3
test=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION RAM READER */',source[start:end])
with tempfile.TemporaryDirectory(prefix='sun-runtime-') as directory:
    directory=Path(directory);path=directory/'test.c';path.write_text(test)
    exe=directory/'test.exe'
    subprocess.run([args.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
        '-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(repo),str(path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
