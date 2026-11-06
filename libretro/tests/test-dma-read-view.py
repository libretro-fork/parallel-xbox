# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual owned-source DMA state machine and backend range/capability query."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'block/libretro-disk.c').read_text()
a=s.index('size_t lr_disk_read_view_max(');b=s.index('BlockAIOCB *lr_disk_aio_read_view(',a)
query=s[a:b].replace('lr_disk_read_view_max(', 'actual_view_limit(')
core=(r/'hw/ide/core.c').read_text()
a=core.index('static bool ide_abandon_dma_view(');b=core.index('/**',a)
ide=core[a:b]
a=core.index('static void ide_cd_change_cb(');b=core.index('static void ide_cd_eject_request_cb(',a)
media=core[a:b].replace('Error **errp', 'Error **errp __attribute__((unused))')
code=Path(__file__).with_suffix('.c').read_text().replace('/* QUERY */',query).replace('/* IDE HELPER */',ide).replace('/* MEDIA CALLBACK */',media)
with tempfile.TemporaryDirectory(prefix='dma-read-view-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(code)
    binary=os.environ.get('DMA_VIEW_BINARY',str(p/'test'))
    flags=shlex.split(os.environ.get('DMA_VIEW_CFLAGS',
                        '-static' if 'mingw' in cc else '-fsanitize=address,undefined'))
    subprocess.run([cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror',*flags,
                    '-I'+str(r),str(p/'test.c'),'-o',binary],check=True)
    if not os.environ.get('DMA_VIEW_SKIP_RUN'):subprocess.run([binary],check=True,timeout=60)
