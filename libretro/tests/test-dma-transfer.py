# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production DMA lifecycle against independently checked guest/disk bytes."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=Path(os.environ.get('DMA_TRANSFER_SOURCE',r/'system/dma-helpers.c')).read_text()
a=s.index('typedef struct {');z=s.index('static\nBlockAIOCB *dma_blk_read_io_func',a)
h=(r/'include/block/aio.h').read_text();types=h[h.index('typedef struct BlockAIOCB BlockAIOCB;'):h.index('void *qemu_aio_get(')]
q=(r/'include/qemu/iov.h').read_text();iov=q[q.index('typedef struct QEMUIOVector'):q.index('void qemu_iovec_init(QEMUIOVector')]
m=(r/'include/system/memory.h').read_text();import re
client=re.search(r'typedef struct AddressSpaceMapClient \{.*?\} AddressSpaceMapClient;',m,re.S)[0]
cache=(r/'util/aiocb.c').read_text();cache=cache[cache.index('#define AIOCB_CACHE_KINDS'):]
v=(r/'util/iov.c').read_text();impl=v[v.index('void qemu_iovec_init('):v.index('void qemu_iovec_destroy(')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:z]).replace('/* AIOCB TYPES */',types).replace('/* IOV TYPE */',iov).replace('/* CLIENT TYPE */',client).replace('/* REQUEST CACHE */',cache).replace('/* IOV IMPLEMENTATION */',impl)
code=('#define DMA_OWNER_DESCRIPTORS 1\n' if 'void lr_dma_iov_cache_clear(' in s else '')+('#define DMA_CANCEL_RELEASE 1\n' if 'dma_complete(dbs, -ECANCELED);' in s or os.environ.get('DMA_TRANSFER_REQUIRE_CANCEL_RELEASE') else '')+code
with tempfile.TemporaryDirectory(prefix='dma-transfer-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('DMA_TRANSFER_BINARY',str(p/'test'));l=r/'libretro-common'
 flags=shlex.split(os.environ.get('DMA_TRANSFER_CFLAGS','-fsanitize=address,undefined'))
 shared=bool(os.environ.get('DMA_TRANSFER_SHARED'))
 if shared:flags+=['-shared','-DDMA_SHARED_BENCH']+(['-fPIC','-Wl,-Bsymbolic'] if 'mingw' not in cc and os.name!='nt' else [])
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-cast-function-type','-DHAVE_THREADS','-I'+str(l/'include'),'-I'+str(r/'include'),*flags,str(p/'test.c'),str(l/'rthreads/rthreads.c'),str(l/'features/features_cpu.c'),str(l/'time/rtime.c'),str(l/'compat/compat_strl.c'),'-pthread','-o',binary],check=True)
 if shared:
  loader=os.environ.get('DMA_TRANSFER_LOADER',str(p/'loader'))
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-cast-function-type',str(Path(__file__).with_name('test-dma-transfer-loader.c'))]+(['-ldl'] if 'mingw' not in cc and os.name!='nt' else ['-static'])+['-o',loader],check=True)
 if not os.environ.get('DMA_TRANSFER_SKIP_RUN'):subprocess.run(([loader,binary] if shared else [binary])+sys.argv[3:],check=True,timeout=60)
