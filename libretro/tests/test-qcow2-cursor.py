# SPDX-License-Identifier: GPL-2.0-or-later
"""Differential QCOW2 memory-cursor bytes/state and optional CPU timing."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=Path(os.environ.get('QCOW_CURSOR_SOURCE',r/'block/libretro-qcow2.c')).read_text()
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s)
with tempfile.TemporaryDirectory(prefix='qcow-cursor-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('QCOW_CURSOR_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('QCOW_CURSOR_CFLAGS','-fsanitize=address,undefined'))
 l=r/'libretro-common'
 sources=[str(r/'block/libretro-file.c')]+[str(l/x) for x in ['encodings/encoding_deflate.c','encodings/encoding_crc32.c','features/features_cpu.c','time/rtime.c','compat/compat_strl.c']]
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-cast-function-type','-DLR_FILE_FILESTREAM=0','-I'+str(r),'-I'+str(r/'block'),'-I'+str(l/'include'),*flags,str(p/'test.c'),*sources,'-pthread','-o',binary],check=True)
 if not os.environ.get('QCOW_CURSOR_SKIP_RUN'):subprocess.run([binary,*sys.argv[3:]],check=True,timeout=120)
