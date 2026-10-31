# SPDX-License-Identifier: GPL-2.0-or-later
"""Production atomic TLS key publication with real libretro-common providers."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
windows=os.name=='nt' or 'mingw' in cc[0]
with tempfile.TemporaryDirectory(prefix='tls-publication-') as t:
 binary=os.environ.get('TLS_PUBLICATION_BINARY',str(Path(t)/('test.exe' if windows else 'test')))
 source=os.environ.get('TLS_PUBLICATION_SOURCE','test-tls-publication.c')
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DHAVE_THREADS','-DHAVE_THREAD_STORAGE','-I'+str(r/'include'),'-I'+str(r/'libretro-common/include')]
 cmd+=shlex.split(os.environ.get('TLS_PUBLICATION_CFLAGS',''))
 cmd += [str(r/'libretro/tests'/source),str(r/'libretro-common/rthreads/rthreads.c')]
 if windows:
  cmd+=['-Wno-cast-function-type']+[str(r/p) for p in ['libretro-common/features/features_cpu.c','libretro-common/time/rtime.c']]
 cmd+=['-pthread','-o',binary];subprocess.run(cmd,check=True)
 if not os.environ.get('TLS_PUBLICATION_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
