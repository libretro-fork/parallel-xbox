# SPDX-License-Identifier: GPL-2.0-or-later
"""Production pusher accounting: no-progress yields without advancing GET."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
s=Path(os.environ.get('PFIFO_PROGRESS_SOURCE',r/'hw/xbox/nv2a/pfifo.c')).read_text()
a=s.index('            ssize_t num_words_processed =')
b=s.index('        } else {',a)
body=s[a:b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
win='mingw' in cc or os.name=='nt'
with tempfile.TemporaryDirectory(prefix='pfifo-progress-',dir=r.parent) as d:
 p=Path(d);(p/'test.c').write_text(code)
 binary=os.environ.get('PFIFO_PROGRESS_BINARY',str(p/'test'))
 flags=shlex.split(os.environ.get('PFIFO_PROGRESS_CFLAGS','' if win else '-fsanitize=address,undefined'))
 subprocess.run(shlex.split(cc)+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('PFIFO_PROGRESS_SKIP_RUN'):subprocess.run([binary],check=True,timeout=10)
