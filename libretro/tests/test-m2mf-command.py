# SPDX-License-Identifier: GPL-2.0-or-later
"""Class 0x39 decoding and retained FIFO command ownership."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='m2mf-command-',dir=r.parent) as d:
 binary=os.environ.get('M2MF_COMMAND_BINARY',str(Path(d)/'test'))
 flags=shlex.split(os.environ.get('M2MF_COMMAND_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(r),str(Path(__file__).with_suffix('.c')),'-o',binary],check=True)
 if not os.environ.get('M2MF_COMMAND_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
