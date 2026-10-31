# SPDX-License-Identifier: GPL-2.0-or-later
"""Owner-only elapsed clock against an independent lifecycle model."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc');baseline=os.environ.get('OWNER_CLOCK_BASELINE')
def read(name):return subprocess.check_output(['git','-C',str(r),'show',baseline+':'+name],text=True) if baseline else (r/name).read_text()
def function(s,token):
 a=s.index(token);i=s.index('{',a)+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
s=read('system/cpu-timers.c');h=read('include/system/cpu-timers-internal.h');a=h.index('typedef struct TimersState {');b=h.index('} TimersState;',a)+len('} TimersState;')
names=['int64_t cpu_get_clock(', 'void cpu_enable_ticks(', 'void cpu_disable_ticks(', 'void cpu_clock_skip_to(', 'bool cpu_ticks_enabled(']
if 'int64_t cpu_get_clock_locked(' in s:names.insert(0,'int64_t cpu_get_clock_locked(')
production='\n'.join(function(s,n) for n in names).replace('int64_t cpu_get_clock(void)','__attribute__((noinline,noclone)) int64_t cpu_get_clock(void)')
code=Path(__file__).with_suffix('.c').read_text().replace('/* TIMERS */',h[a:b]).replace('/* PRODUCTION */',production)
windows=os.name=='nt' or 'mingw' in cc[0]
with tempfile.TemporaryDirectory(prefix='owner-clock-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('OWNER_CLOCK_BINARY',str(p/('test.exe' if windows else 'test')))
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(r/'libretro-common/include')]+shlex.split(os.environ.get('OWNER_CLOCK_CFLAGS',''))
 if windows:cmd+=['-Wno-cast-function-type']
 cmd+=[str(p/'test.c')]+[str(r/x) for x in ['libretro-common/features/features_cpu.c','libretro-common/time/rtime.c']]+['-o',binary]
 subprocess.run(cmd,check=True)
 if not os.environ.get('OWNER_CLOCK_SKIP_RUN'):subprocess.run([binary,*shlex.split(os.environ.get('OWNER_CLOCK_ARGS',''))],check=True,timeout=30)
