# SPDX-License-Identifier: GPL-2.0-or-later
"""Concurrent production timer deadline reads versus cancellation/free."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc');baseline=os.environ.get('TIMER_READ_BASELINE')
def read(name):return subprocess.check_output(['git','-C',str(r),'show',baseline+':'+name],text=True) if baseline else (r/name).read_text()
s=read('util/qemu-timer.c');h=read('include/qemu/timer.h')
production=s[s.index('/* All core timers belong'):s.index('int qemu_timeout_ns_to_ms(')]+s[s.index('void timer_init_full('):]
# Real core callers are in other translation units; keep these APIs out of line.
for signature in ['int64_t qemu_clock_deadline_ns_all(', 'void timer_mod_ns(', 'bool qemu_clock_expired(']:
 production=production.replace(signature, '__attribute__((noinline,noclone)) '+signature)
enum=h[h.index('typedef enum {'):h.index('} QEMUClockType;')+len('} QEMUClockType;')]
a=h.index('struct QEMUTimer {');timer=h[a:h.index('\n};',a)+3]
code=Path(__file__).with_suffix('.c').read_text().replace('/* ENUM */',enum).replace('/* TIMER */',timer).replace('/* PRODUCTION */',production)
windows=os.name=='nt' or 'mingw' in cc[0]
with tempfile.TemporaryDirectory(prefix='timer-readers-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('TIMER_READ_BINARY',str(p/('test.exe' if windows else 'test')))
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DHAVE_THREADS','-I'+str(r/'include'),'-I'+str(r/'libretro-common/include')]
 cmd+=shlex.split(os.environ.get('TIMER_READ_CFLAGS',''))
 if baseline:cmd+=['-DTIMER_READ_BASELINE']
 if windows:cmd+=['-Wno-cast-function-type']
 cmd+=[str(p/'test.c')]+[str(r/x) for x in ['libretro-common/rthreads/rthreads.c','libretro-common/features/features_cpu.c','libretro-common/time/rtime.c']]+['-pthread','-o',binary]
 subprocess.run(cmd,check=True)
 if not os.environ.get('TIMER_READ_SKIP_RUN'):subprocess.run([binary,*shlex.split(os.environ.get('TIMER_READ_ARGS',''))],check=True,timeout=60)
