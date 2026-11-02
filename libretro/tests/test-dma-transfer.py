# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production DMA lifecycle against independently checked guest/disk bytes."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=Path(os.environ.get('DMA_TRANSFER_SOURCE',r/'system/dma-helpers.c')).read_text()
a=s.index('typedef struct {');z=s.index('static\nBlockAIOCB *dma_blk_read_io_func',a)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:z])
with tempfile.TemporaryDirectory(prefix='dma-transfer-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('DMA_TRANSFER_BINARY',str(p/'test'));l=r/'libretro-common'
 flags=shlex.split(os.environ.get('DMA_TRANSFER_CFLAGS','-fsanitize=address,undefined'))
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-cast-function-type','-DHAVE_THREADS','-I'+str(l/'include'),*flags,str(p/'test.c'),str(l/'rthreads/rthreads.c'),str(l/'features/features_cpu.c'),str(l/'time/rtime.c'),str(l/'compat/compat_strl.c'),'-pthread','-o',binary],check=True)
 if not os.environ.get('DMA_TRANSFER_SKIP_RUN'):subprocess.run([binary],check=True,timeout=60)
