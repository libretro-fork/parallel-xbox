# SPDX-License-Identifier: GPL-2.0-or-later
"""Portable root parsing and native comparison with the former Windows API."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc';s=(r/'util/cutils.c').read_text();a=s.index('static const char *win_skip_path_root(');b=s.index('\n}\n',a)+2
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:b])
with tempfile.TemporaryDirectory(prefix='win-root-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('WIN_ROOT_BINARY',str(p/'test'))
 subprocess.run([cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('WIN_ROOT_CFLAGS','-fsanitize=address,undefined')),str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('WIN_ROOT_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)