# SPDX-License-Identifier: GPL-2.0-or-later
"""Production IDE restart behavior and idle-resume cost."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(s,name):
 m=re.search(r'^(?:static )?(?:void|bool|int|QEMUBH \*|BlockAIOCB \*)\s*'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
s=Path(os.environ.get('IDE_RESTART_SOURCE',r/'hw/ide/core.c')).read_text(encoding='utf-8')
a=(r/'util/async.c').read_text(encoding='utf-8');legacy='!running || !bus->error_status' not in s
bh=a[a.index('enum {',a.index('/* QEMUBH::flags')):a.index('/* Called concurrently')]
funcs=[function(a,n) for n in ['aio_notify','aio_notify_accept','aio_bh_enqueue','aio_bh_dequeue','aio_bh_call','aio_bh_poll','qemu_bh_schedule','qemu_bh_delete','aio_bh_new_full']]
cpu=function((r/'accel/tcg/cpu-exec.c').read_text(encoding='utf-8'),'tcg_kick_vcpu_thread')+'\n'+function((r/'accel/tcg/tcg-accel-ops-libretro.c').read_text(encoding='utf-8'),'lr_vcpu_exit')
prod=[function(s,n) for n in ['ide_restart_bh','ide_restart_cb']]
h=(r/'hw/ide/ide-internal.h').read_text();flags=h[h.index('#define IDE_RETRY_MASK'):h.index('static inline uint8_t ide_dma_cmd_to_retry')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* CPU FUNCTIONS */',cpu).replace('/* BH TYPES */',bh).replace('/* BH FUNCTIONS */','\n'.join(funcs)).replace('/* FLAGS */',flags).replace('/* RESTART FUNCTIONS */','\n'.join(prod))
with tempfile.TemporaryDirectory(prefix='ide-restart-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('IDE_RESTART_BINARY',str(p/'test'))
 args=shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare','-Wno-unused-parameter','-Wno-unused-function','-Wno-cast-function-type','-DHAVE_THREADS','-I'+str(r/'include'),'-I'+str(r/'libretro-common/include')]
 args+=['-DLEGACY'] if legacy else []
 args+=shlex.split(os.environ.get('IDE_RESTART_CFLAGS',''))
 args+=[str(p/'test.c')]+[str(r/'libretro-common'/f) for f in ['rthreads/retro_eventcount.c','rthreads/rthreads.c','features/features_cpu.c','time/rtime.c','compat/compat_strl.c']]+['-pthread','-o',binary]
 subprocess.run(args,check=True)
 if not os.environ.get('IDE_RESTART_SKIP_RUN'):subprocess.run([binary]+sys.argv[3:],check=True,timeout=60)
