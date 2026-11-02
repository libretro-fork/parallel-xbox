# SPDX-License-Identifier: GPL-2.0-or-later
"""Isolate production DMA ownership bookkeeping; no payload I/O is timed."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=Path(os.environ.get('DMA_SCOPE_SOURCE',r/'system/dma-helpers.c')).read_text()
a=(r/'util/async.c').read_text();start=a.index('AioContext *qemu_get_current_aio_context(void)');end=a.index('void qemu_set_current_aio_context',start)
old='AioContext *ctx;' in s
begin='dbs->ctx = qemu_get_current_aio_context();' if old else re.search(r'    assert\(sg->as->map_client_owner.*?;',s)[0]
check='AioContext *ctx = dbs->ctx; assert(ctx == qemu_get_current_aio_context());' if old else ''
code=Path(__file__).with_suffix('.c').read_text().replace('/* BEGIN */',begin).replace('/* CHECK */',check)
with tempfile.TemporaryDirectory(prefix='dma-scope-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 support='#include <stddef.h>\ntypedef struct {int unused;} AioContext;\nstatic AioContext main_context;\n#include <stdlib.h>\n#define g_malloc0(n) calloc(1,n)\n#include "qemu/thread-local.h"\nQEMU_DEFINE_STATIC_THREAD_LOCAL(AioContext *, my_aiocontext)\nstatic int bql_locked(void){return 1;}\nstatic AioContext *qemu_get_aio_context(void){return &main_context;}\n'+a[start:end]+'\nvoid initialize_context(void){set_my_aiocontext(&main_context);}\n'
 (p/'support.c').write_text(support);binary=os.environ.get('DMA_SCOPE_BINARY',str(p/'test'));l=r/'libretro-common'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-cast-function-type','-DHAVE_THREADS','-DHAVE_THREAD_STORAGE','-I'+str(l/'include'),'-I'+str(r/'include'),str(p/'test.c'),str(p/'support.c'),str(l/'rthreads/rthreads.c'),str(l/'features/features_cpu.c'),str(l/'time/rtime.c'),str(l/'compat/compat_strl.c'),'-pthread','-o',binary],check=True)
 if not os.environ.get('DMA_SCOPE_SKIP_RUN'):subprocess.run([binary],check=True)
