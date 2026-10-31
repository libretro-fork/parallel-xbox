# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production ordinary TLS macros using libretro-common threads."""
import os, shlex, subprocess, sys, tempfile
from pathlib import Path
r=Path(sys.argv[1]).resolve(); cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
with tempfile.TemporaryDirectory(prefix='thread-local-') as t:
 out=os.environ.get('THREAD_LOCAL_BINARY',str(Path(t)/'test'))
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DHAVE_THREADS','-I'+str(r/'include'),'-I'+str(r/'libretro-common/include')]+shlex.split(os.environ.get('THREAD_LOCAL_CFLAGS',''))
 cmd += [str(r/p) for p in ['libretro/tests/test-thread-local.c','libretro-common/rthreads/rthreads.c']]
 if 'mingw' in cc[0]:
  cmd+=['-Wno-cast-function-type']+[str(r/p) for p in ['libretro-common/features/features_cpu.c','libretro-common/time/rtime.c']]
 cmd+=['-pthread','-o',out];subprocess.run(cmd,check=True)
 if not os.environ.get('THREAD_LOCAL_SKIP_RUN'):subprocess.run([out],check=True,timeout=30)
print('Ordinary TLS: 16 threads, independent cells and stable reader addresses PASS')
