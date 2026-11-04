# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production callback geometry and dispatch against 128-bit spans."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=Path(os.environ.get('MEM_CALLBACK_SOURCE',repo/'system/physmem.c')).read_text()
functions=[]
for name in ['access_callback_address_matches','mem_access_callback_address_matches','mem_check_access_callback_ramaddr']:
 m=re.search(r'^(?:static inline )?(?:bool|int|void) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m
 end=s.index('{',m.start())+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 functions.append(s[m.start():end])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(functions))
with tempfile.TemporaryDirectory(prefix='memory-callback-overlap-') as d:
 p=Path(d);(p/'test.c').write_text(code)
 exe=Path(os.environ.get('MEM_CALLBACK_BINARY',p/'test'))
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*shlex.split(os.environ.get('MEM_CALLBACK_CFLAGS','')),str(p/'test.c'),'-o',str(exe)],check=True)
 if not os.environ.get('MEM_CALLBACK_SKIP_RUN'):subprocess.run([str(exe)],check=True)
