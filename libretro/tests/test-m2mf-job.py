# SPDX-License-Identifier: GPL-2.0-or-later
"""Owner-thread M2MF lifecycle with controlled backend retirement."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='m2mf-job-',dir=r.parent) as d:
 binary=os.environ.get('M2MF_JOB_BINARY',str(Path(d)/'test'))
 flags=shlex.split(os.environ.get('M2MF_JOB_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,'-I'+str(r),str(Path(__file__).with_suffix('.c')),'-o',binary],check=True)
 if not os.environ.get('M2MF_JOB_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
