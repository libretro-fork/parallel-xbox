# SPDX-License-Identifier: GPL-2.0-or-later
"""Production CPU copy helper against ordered row memmove semantics."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='blit-copy-',dir=r.parent) as d:
    binary=os.environ.get('BLIT_COPY_BINARY',str(Path(d)/'test'))
    flags=shlex.split(os.environ.get('BLIT_COPY_CFLAGS','' if win else '-fsanitize=address,undefined'))
    subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(r),str(Path(__file__).with_suffix('.c')),'-o',binary],check=True)
    if not os.environ.get('BLIT_COPY_SKIP_RUN'):
        subprocess.run([binary],check=True,timeout=30)
