# SPDX-License-Identifier: GPL-2.0-or-later
"""Replay/concurrent RCU initialization with the production queue and once code."""
from pathlib import Path
import subprocess,sys,tempfile,os
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=Path(os.environ.get('RCU_TEST_SOURCE',repo/'util/rcu.c')).read_text()
a=source.index('static struct rcu_head dummy;');b=source.index('static void call_rcu_thread' if 'static void call_rcu_thread' in source else 'static void *call_rcu_thread',a);queue=source[a:b].replace('static QemuEvent rcu_call_ready_event;','')
# Force the exact two-consumer schedule from the reported empty-queue abort.
needle='    if (!next) {';queue=queue.replace(needle,'    queue_read_hook(next);\n'+needle,1)
a=source.index('static void __attribute__((__constructor__)) rcu_init(void)');ctor=source[a:].replace('__attribute__((__constructor__))','').replace('rcu_init_complete();','rcu_init_complete(true);')
gcore=(repo/'libretro/glib/gcore.c').read_text();a=gcore.index('/* Once location registry.');once=gcore[a:]
a=gcore.index('static void g_compat_lock(');b=gcore.index('/* ---- memory',a);once=gcore[a:b]+once
a=source.index('static void rcu_init_child(void)');child=source[a:source.index('\n}',a)+2].replace('rcu_init_complete();','rcu_init_complete(true);')
code=Path(__file__).with_suffix('.c').read_text().replace('/* ONCE */',once).replace('/* QUEUE */',queue).replace('/* INIT */',ctor).replace('/* CHILD */',child)
code=('#define HAVE_CLAIM '+str(int('rcu_claim_callback_worker' in queue))+'\n')+code
with tempfile.TemporaryDirectory(prefix='rcu-startup-') as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/'test.exe'
 flags=['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-I'+str(repo/'libretro-common/include')]
 if os.name!='nt':flags+=['-pthread']
 subprocess.run([cc,*flags,str(p/'test.c'),'-o',str(exe)],check=True)
 for mode in ['replay','concurrent','worker','nested']:
  subprocess.run([str(exe),mode],check=True,timeout=15)
