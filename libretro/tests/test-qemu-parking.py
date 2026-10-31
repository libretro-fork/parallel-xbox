#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Production event/counter-gate tests; optional pre-change revision benchmark."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
baseline=os.environ.get('QEMU_PARKING_BASELINE')
def read(name):
 if baseline:return subprocess.check_output(['git','-C',str(r),'show',baseline+':'+name],text=True)
 return (r/name).read_text()
def struct(name,source):
 a=source.index(name);z=source.index('\n}',a)+2
 return source[a:z]+(' QemuEvent;' if name.startswith('typedef') else ';')
event=read('util/event.c');gate=read('util/lockcnt.c')
event='\n'.join(line for line in event.splitlines() if not line.startswith('#include'))
gate='\n'.join(line for line in gate.splitlines() if not line.startswith('#include'))
event=event.replace('        int key = retro_eventcount_prepare_wait(&ev->waiters);',
 '        test_park_hook(ev, 1);\n        int key = retro_eventcount_prepare_wait(&ev->waiters);\n        test_park_hook(ev, 2);')
event=event.replace('            retro_eventcount_commit_wait(&ev->waiters, key);',
 '            test_park_hook(ev, 3);\n            retro_eventcount_commit_wait(&ev->waiters, key);')
event=event.replace('        qemu_futex_wait(ev, EV_BUSY);','        test_park_hook(ev, 3);\n        qemu_futex_wait(ev, EV_BUSY);')
event=event.replace('        int old = retro_atomic_exchange_int(&ev->value, EV_SET);',
 '        int old = retro_atomic_exchange_int(&ev->value, EV_SET);\n        test_park_hook(ev, 5);')
event=event.replace('        int key = retro_eventcount_prepare_wait(waiters);',
 '        test_park_hook(ev, 1);\n        int key = retro_eventcount_prepare_wait(waiters);\n        test_park_hook(ev, 2);')
event=event.replace('            retro_eventcount_commit_wait(waiters, key);',
 '            test_park_hook(ev, 3);\n            retro_eventcount_commit_wait(waiters, key);')
gate=gate.replace('            int key = retro_eventcount_prepare_wait(&lockcnt->waiters);',
 '            test_park_hook(lockcnt, 4);\n            int key = retro_eventcount_prepare_wait(&lockcnt->waiters);')
gate=gate.replace('            qemu_futex_wait(&lockcnt->count, *val);',
 '            test_park_hook(lockcnt, 4);\n            qemu_futex_wait(&lockcnt->count, *val);')
gate=gate.replace('            int key = retro_eventcount_prepare_wait(*waiters);',
 '            test_park_hook(lockcnt, 4);\n            int key = retro_eventcount_prepare_wait(*waiters);')
traces='\n'.join('#define '+name+'(...) ((void)0)' for name in sorted(set(re.findall(r'\btrace_\w+',gate))))
code=Path(__file__).with_suffix('.c').read_text()
futex=read('include/qemu/futex.h') if baseline else ''
code=code.replace('/* FUTEX */',futex).replace('/* EVENT TYPE */',struct('typedef struct QemuEvent {',read('include/qemu/thread.h')))
code=code.replace('/* GATE TYPE */',struct('struct QemuLockCnt {',read('include/qemu/lockcnt.h')))
code=code.replace('/* EVENT */',event).replace('/* GATE */',traces+'\n'+gate)
with tempfile.TemporaryDirectory(prefix='qemu-parking-') as t:
 p=Path(t);(p/'test.c').write_text(code)
 binary=Path(os.environ.get('QEMU_PARKING_BINARY',str(p/'test')))
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DHAVE_THREADS',
 '-I'+str(r/'include'),'-I'+str(r/'libretro-common/include')]
 if baseline:cmd+=['-DPARKING_BASELINE']
 cmd+=shlex.split(os.environ.get('QEMU_PARKING_CFLAGS',''))
 cmd+=[str(p/'test.c'),str(r/'libretro-common/rthreads/retro_eventcount.c'),str(r/'libretro-common/rthreads/rthreads.c')]
 if 'mingw' in cc[0]:
  cmd+=['-Wno-cast-function-type',str(r/'libretro-common/features/features_cpu.c'),str(r/'libretro-common/time/rtime.c')]
  if baseline:cmd+=['-lsynchronization']
 cmd+=['-pthread','-o',str(binary)]
 subprocess.run(cmd,check=True)
 if not os.environ.get('QEMU_PARKING_SKIP_RUN'):
  subprocess.run([str(binary)],check=True,timeout=90)
