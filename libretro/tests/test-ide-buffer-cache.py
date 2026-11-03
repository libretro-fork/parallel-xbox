# SPDX-License-Identifier: GPL-2.0-or-later
"""Production buffered-read allocation/lifetime and failure-preservation checks."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=Path(os.environ.get('IDE_CACHE_SOURCE',r/'hw/ide/core.c')).read_text();h=Path(os.environ.get('IDE_CACHE_HEADER',r/'hw/ide/ide-internal.h')).read_text();q=(r/'include/qemu/iov.h').read_text()
a=s.index('static void ide_buffered_readv_cb(');z=s.index('/**\n * Cancel all pending DMA requests.',a)
cache=s[s.index('/* One completed request per owner'):a] if '/* One completed request per owner' in s else 'static void ide_buffered_read_cache_clear(void) {}\n'
req=re.search(r'typedef struct IDEBufferedRequest \{.*?\} IDEBufferedRequest;',h,re.S)[0]
qiov=q[q.index('typedef struct QEMUIOVector'):q.index('void qemu_iovec_init(QEMUIOVector')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* QIOV */',qiov).replace('/* REQUEST */',req).replace('/* PRODUCTION */',cache+s[a:z])
with tempfile.TemporaryDirectory(prefix='ide-cache-') as tmp:
 p=Path(tmp);binary=os.environ.get('IDE_CACHE_BINARY',str(p/'test'))
 code=('#define HAVE_IDE_BUFFER_CACHE 1\n' if '/* One completed request per owner' in s else '')+('#define IDE_RELEASE_BEFORE_CALLBACK 1\n' if 'cb(cb_opaque, ret);' in s else '')+code
 (p/'test.c').write_text(code)
 flags=shlex.split(os.environ.get('IDE_CACHE_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' and 'mingw' not in cc else '-static'))
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,'-I'+str(r/'include'),'-I'+str(r/'libretro-common/include'),str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('IDE_CACHE_SKIP_RUN'):subprocess.run([binary]+sys.argv[3:],check=True,timeout=60)
