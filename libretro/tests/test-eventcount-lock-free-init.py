# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
with tempfile.TemporaryDirectory(prefix='eventcount-init-') as tmp:
    binary=os.environ.get('EC_INIT_BINARY',str(Path(tmp)/'test'))
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                    *shlex.split(os.environ.get('EC_INIT_CFLAGS','')),
                    '-DRETRO_EVENTCOUNT_FORCE_SCOND','-I'+str(root/'libretro-common/include'),
                    str(root/'libretro/tests/test-eventcount-lock-free-init.c'),
                    str(root/'libretro-common/rthreads/retro_eventcount.c'),'-o',binary],check=True)
    if not os.environ.get('EC_INIT_SKIP_RUN'):subprocess.run([binary],check=True,timeout=15)
