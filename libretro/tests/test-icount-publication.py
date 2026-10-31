# SPDX-License-Identifier: GPL-2.0-or-later
"""Production instruction-clock publication, ownership and restored state."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc');baseline=os.environ.get('ICOUNT_TEST_BASELINE')
def read(name):return subprocess.check_output(['git','-C',str(r),'show',baseline+':'+name],text=True) if baseline else (r/name).read_text()
def function(s,token):
 a=s.index(token);i=s.index('{',a)+1;depth=1
 while depth:depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
s=read('accel/tcg/icount-common.c');h=read('include/system/cpu-timers-internal.h');c=read('system/cpu-timers.c')
a=h.index('typedef struct TimersState {');b=h.index('} TimersState;',a)+len('} TimersState;')
names=['static int64_t icount_get_executed(', 'static void icount_update_locked(', 'void icount_update(', 'static int64_t icount_get_raw_locked(', 'static int64_t icount_get_locked(', 'int64_t icount_get(', 'int64_t icount_to_ns(', 'void icount_skip_to(']
production='\n'.join(function(s,n) for n in names)+'\n'+function(c,'void cpu_enable_ticks(')
if not baseline:
 production=function(s,'void icount_publish_time(')+'\n'+production+'\n'+function(c,'static int cpu_timers_post_load(')
 for match in re.finditer(r'TS_STORE64\(qemu_icount_bias,.*?\);',s,re.S):assert s[match.end():].lstrip().startswith('icount_publish_time();')
 assert 'icount_publish_time();' in function(c,'void cpu_enable_ticks(')
 assert '.post_load = cpu_timers_post_load,' in c
# Preserve out-of-line call costs equally, with no role/TLS assertions in either getter.
production=production.replace('int64_t icount_get(void)','__attribute__((noinline,noclone)) int64_t icount_get(void)').replace('void icount_update(CPUState *cpu)','__attribute__((noinline,noclone)) void icount_update(CPUState *cpu)')
code=Path(__file__).with_suffix('.c').read_text().replace('/* TIMERS */',h[a:b]).replace('/* PRODUCTION */',production)
windows=os.name=='nt' or 'mingw' in cc[0]
with tempfile.TemporaryDirectory(prefix='icount-publication-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('ICOUNT_TEST_BINARY',str(p/('test.exe' if windows else 'test')))
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-DHAVE_THREADS','-DHAVE_THREAD_STORAGE','-I'+str(r/'libretro-common/include')]+shlex.split(os.environ.get('ICOUNT_TEST_CFLAGS',''))
 if baseline:cmd+=['-DICOUNT_BASELINE']
 if windows:cmd+=['-Wno-cast-function-type']
 cmd+=[str(p/'test.c')]+[str(r/x) for x in ['libretro-common/rthreads/rthreads.c','libretro-common/features/features_cpu.c','libretro-common/time/rtime.c']]+['-pthread','-o',binary]
 subprocess.run(cmd,check=True)
 if not os.environ.get('ICOUNT_TEST_SKIP_RUN'):subprocess.run([binary,*shlex.split(os.environ.get('ICOUNT_TEST_ARGS',''))],check=True,timeout=60)
