# SPDX-License-Identifier: GPL-2.0-or-later
"""Production callback publication/walk/unlink with controlled RCU reclamation.
The real lock-free queue macros and libretro-common threads are used; the
RCU grace provider is controlled here and tested independently by rcu-owner.
"""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(repo/'system/physmem.c').read_text();header=(repo/'include/hw/core/cpu.h').read_text()
def function(name):
 m=re.search(r'^(?:static )?(?:inline )?(?:bool|int|void) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m
 end=s.index('{',m.start())+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
functions='\n'.join(function(n) for n in ['access_callback_address_matches','mem_access_callback_address_matches','do_mem_access_callback_insert','do_mem_access_callback_remove_by_ref','mem_check_access_callback_ramaddr'])
if os.environ.get('MEM_CALLBACK_RCU_MUTATION')=='immediate-free':functions=functions.replace('g_free_rcu(cb, rcu);','free(cb);')
if os.environ.get('MEM_CALLBACK_RCU_MUTATION')=='unguarded':functions=functions.replace('RCU_READ_LOCK_GUARD();','/* missing read guard */')
type_=re.search(r'typedef struct MemAccessCallback \{.*?\} MemAccessCallback;',header,re.S).group()
rcu=(repo/'include/qemu/rcu.h').read_text();rcu=re.search(r'struct rcu_head \{.*?\};',rcu,re.S).group()
fixture=Path(__file__).with_suffix('.c').read_text().replace('/* CALLBACK TYPE */',rcu+'\n'+type_).replace('/* PRODUCTION */',functions)
windows='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='memory-callback-rcu-') as d:
 p=Path(d);(p/'test.c').write_text(fixture);exe=Path(os.environ.get('MEM_CALLBACK_RCU_BINARY',p/'test'))
 flags=shlex.split(os.environ.get('MEM_CALLBACK_RCU_CFLAGS',''))
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-DHAVE_THREADS',*flags,'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),str(p/'test.c')]
 files=['rthreads/rthreads.c','rthreads/retro_eventcount.c']
 if windows:
  cmd+=['-Wno-cast-function-type','-Wno-unused-parameter'];files+=['compat/compat_strl.c','features/features_cpu.c','time/rtime.c']
 subprocess.run(cmd+[str(repo/'libretro-common'/f) for f in files]+['-pthread','-o',str(exe)],check=True)
 if not os.environ.get('MEM_CALLBACK_RCU_SKIP_RUN'):subprocess.run([str(exe)],check=True,timeout=60)
