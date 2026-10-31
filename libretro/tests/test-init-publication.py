#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile production immutable-layout and optional Windows API publication."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve()
cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
def function(s,key):
 a=s.index(key);z=s.index('{',a)+1;depth=1
 while depth:
  depth+=(s[z]=='{')-(s[z]=='}');z+=1
 return s[a:z]
t=(r/'tcg/tcg.c').read_text()
w=(r/'util/qemu-thread-win32.c').read_text()
tcg=function(t,'static TCGHelperInfo *tcg_resolve_call_layout(')
a=w.index('static retro_atomic_ptr_t thread_description_func;')
z=w.index('void qemu_thread_naming(',a)
test=Path(__file__).with_suffix('.c').read_text().replace('/* TCG */',tcg).replace('/* WIN32 */',w[a:z])
with tempfile.TemporaryDirectory(prefix='init-publication-') as temporary:
 p=Path(temporary);(p/'test.c').write_text(test)
 binary=Path(os.environ.get('INIT_PUBLICATION_BINARY',str(p/'test')))
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DHAVE_THREADS',
 '-I'+str(r/'include'),'-I'+str(r/'tcg/i386'),'-I'+str(r/'libretro-common/include')]
 cmd+=shlex.split(os.environ.get('INIT_PUBLICATION_CFLAGS',''))
 cmd+=[str(p/'test.c'),str(r/'libretro-common/rthreads/rthreads.c')]
 if 'mingw' in cc[0]:
  cmd+=['-Wno-cast-function-type',str(r/'libretro-common/features/features_cpu.c'),str(r/'libretro-common/time/rtime.c')]
 cmd+=['-pthread','-o',str(binary)]
 subprocess.run(cmd,check=True)
 if not os.environ.get('INIT_PUBLICATION_SKIP_RUN'):
  subprocess.run([str(binary)],check=True,timeout=30)
