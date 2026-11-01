# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production PFIFO idle report parks, kicks and frame acknowledgement."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pfifo.c').read_text();a=s.index('static inline void pfifo_idle_wait(');b=s.index('{',a)+1;depth=1
while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:b])
with tempfile.TemporaryDirectory(prefix='pfifo-report-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/'test'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30)
