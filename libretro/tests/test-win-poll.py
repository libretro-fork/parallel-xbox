# SPDX-License-Identifier: GPL-2.0-or-later
"""Production Windows notification polling, descriptor limits and guards."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
s=(r/'libretro/glib/gmain.c').read_text();a=s.index('#ifdef _WIN32\n/* Poll caller-owned');b=s.index('\ngboolean g_main_context_iteration',a)
h=(r/'libretro/glib/glib.h').read_text();p=h.index('typedef struct _GPollFD');q=h.index('} GPollFD;',p)+len('} GPollFD;')
code=Path(__file__).with_suffix('.c').read_text().replace('/* POLLTYPE */',h[p:q]).replace('/* POLL */',s[a:b])
windows=os.name=='nt' or 'mingw' in cc[0]
with tempfile.TemporaryDirectory(prefix='win-poll-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('WIN_POLL_BINARY',str(p/('test.exe' if windows else 'test')))
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-type-limits']+shlex.split(os.environ.get('WIN_POLL_CFLAGS',''))+[str(p/'test.c'),'-o',binary]
 subprocess.run(cmd,check=True)
 if not os.environ.get('WIN_POLL_SKIP_RUN'):
  for args in [[]]:subprocess.run([binary,*args],check=True,timeout=30)
