# SPDX-License-Identifier: GPL-2.0-or-later
"""Production Windows polling against its previous implementation and guards."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
def function(s,token):
 a=s.index(token);i=s.index('{',a)+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
s=(r/'libretro/glib/gmain.c').read_text();a=s.index('#ifdef _WIN32\n/* Poll caller-owned');b=s.index('\ngboolean g_main_context_iteration',a)
old=subprocess.check_output(['git','-C',str(r),'show','cc55ff05918975e4ff0409535bfd6094ae9b1182:libretro/glib/gmain.c'],text=True)
aold=old.index('gint g_poll(');bold=old.index('\ngboolean g_main_context_iteration',aold)
baseline=old[aold:bold].replace('gint g_poll(', 'gint baseline_g_poll(')
h=(r/'libretro/glib/glib.h').read_text();p=h.index('typedef struct _GPollFD');q=h.index('} GPollFD;',p)+len('} GPollFD;')
t=(r/'util/qemu-timer.c').read_text();x=t.index('#if defined(XBOX) && defined(_WIN32) && !defined(CONFIG_PPOLL)');y=t.index('int qemu_poll_ns(',x)
code=Path(__file__).with_suffix('.c').read_text().replace('/* POLLTYPE */',h[p:q]).replace('/* BASELINE */',baseline).replace('/* POLL */',s[a:b]).replace('/* TIMEOUT */',function(t,'int qemu_timeout_ns_to_ms(')).replace('/* HR */',t[x:y])
windows=os.name=='nt' or 'mingw' in cc[0]
with tempfile.TemporaryDirectory(prefix='win-poll-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);binary=os.environ.get('WIN_POLL_BINARY',str(p/('test.exe' if windows else 'test')))
 cmd=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-type-limits']+shlex.split(os.environ.get('WIN_POLL_CFLAGS',''))+[str(p/'test.c'),'-o',binary]
 subprocess.run(cmd,check=True)
 if not os.environ.get('WIN_POLL_SKIP_RUN'):
  for args in ([[]] if windows else [[],['create-fail'],['set-fail']]):subprocess.run([binary,*args],check=True,timeout=30)
