# SPDX-License-Identifier: GPL-2.0-or-later
"""Production timer queues versus an independent ordered-array oracle."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
baseline=os.environ.get('CORE_TIMERS_BASELINE')
def read(name):
 return subprocess.check_output(['git','-C',str(r),'show',baseline+':'+name],text=True) if baseline else (r/name).read_text()
s=read('util/qemu-timer.c');h=read('include/qemu/timer.h')
legacy='typedef struct QEMUClock {' in s
a=s.index('typedef struct QEMUClock {') if legacy else s.index('/* All core timers belong')
production=s[a:s.index('int qemu_timeout_ns_to_ms(')]+s[s.index('void timer_init_full('):]
enum=h[h.index('typedef enum {'):h.index('} QEMUClockType;')+len('} QEMUClockType;')]
timer=h[h.index('struct QEMUTimer {'):h.index('\n};',h.index('struct QEMUTimer {'))+3]
code=Path(__file__).with_suffix('.c').read_text().replace('/* ENUM */',enum).replace('/* TIMER */',timer).replace('/* PRODUCTION */',production)
with tempfile.TemporaryDirectory(prefix='core-timers-') as t:
 p=Path(t);(p/'test.c').write_text(code);binary=Path(os.environ.get('CORE_TIMERS_BINARY',str(p/'test')))
 flags=shlex.split(os.environ.get('CORE_TIMERS_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
 cmd=[cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),str(p/'test.c'),'-o',str(binary)]
 if legacy:cmd+=['-DTIMER_BASELINE']
 cmd+=['-DEXPECTED_TIMER_EVENTS='+('4' if 'timers_done_ev' in production else '0')]
 subprocess.run(cmd,check=True)
 if not os.environ.get('CORE_TIMERS_SKIP_RUN'):
  subprocess.run([str(binary)],check=True,timeout=30)
