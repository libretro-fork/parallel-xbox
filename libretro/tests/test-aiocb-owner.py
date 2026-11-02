# SPDX-License-Identifier: GPL-2.0-or-later
"""Production AIOCB allocation/refcount/cache lifecycle and warmed API cost."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=Path(os.environ.get('AIOCB_SOURCE',r/'util/aiocb.c')).read_text();legacy='static __thread AIOCBCache' in s
h=(r/'include/block/aio.h').read_text();types=h[h.index('typedef struct BlockAIOCB BlockAIOCB;'):h.index('void *qemu_aio_get(')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */',types).replace('/* PRODUCTION */',s[s.index('#define AIOCB_CACHE_KINDS'):])
with tempfile.TemporaryDirectory(prefix='aiocb-owner-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('AIOCB_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('AIOCB_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' and 'mingw' not in cc else '-static'))
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-variable','-Wno-unused-function',*(['-DLEGACY'] if legacy else []),*flags,str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('AIOCB_SKIP_RUN'):subprocess.run([binary]+sys.argv[3:],check=True,timeout=60)
