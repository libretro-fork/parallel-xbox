# SPDX-License-Identifier: GPL-2.0-or-later
"""Production DMA snapshot validation and post-load resubmission boundaries."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
core=(root/'hw/ide/core.c').read_text();atapi=(root/'hw/ide/atapi.c').read_text()
a=core.index('static bool ide_dma_replay_needed(');b=core.index('static const VMStateDescription vmstate_ide_dma_replay_data',a)
code=core[a:b]
a=atapi.index('void ide_atapi_dma_resume(');b=atapi.index('void ide_atapi_dma_restart(',a)
code+=atapi[a:b]
h=(root/'include/hw/ide/ide-dev.h').read_text();a=h.index('#define IDE_DMA_REPLAY_SG');b=h.index('/* NOTE: IDEState',a)
fixture=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',h[a:b]).replace('/* PRODUCTION */',code)
assert '&vmstate_ide_dma_replay,' in core
pci=(root/'hw/ide/pci.c').read_text();assert 'bm->cur_prd_len != 0 || bm->dma.aiocb != NULL' in pci
with tempfile.TemporaryDirectory(prefix='dma-replay-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture)
 binary=os.environ.get('DMA_REPLAY_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('DMA_REPLAY_CFLAGS','-static' if 'mingw' in cc else '-fsanitize=address,undefined'))
 subprocess.run([cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('DMA_REPLAY_SKIP_RUN'):subprocess.run([binary],check=True,timeout=60)
