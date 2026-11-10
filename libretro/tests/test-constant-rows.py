# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare the production batched upload with the frozen word-at-a-time path."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
s=(r/'hw/xbox/nv2a/pgraph/pgraph.c').read_text()
a=s.index('static inline void set_transform_constant_word(')
b=s.index('DEF_METHOD_INC(NV097, SET_VERTEX3F)',a)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:b])
if os.name!='nt':
 import resource
 resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='constant-rows-') as d:
 p=Path(d);src=p/'test.c';binary=os.environ.get('CONSTANT_ROWS_BINARY',str(p/'test'))
 args=cc+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r)]+shlex.split(os.environ.get('CONSTANT_ROWS_CFLAGS','-fsanitize=address,undefined'))
 def build(text):
  src.write_text(text);subprocess.run(args+[str(src),'-o',binary],check=True)
 build(code)
 if not os.environ.get('CONSTANT_ROWS_SKIP_RUN'):
  runner=shlex.split(os.environ.get('CONSTANT_ROWS_RUNNER',''))
  subprocess.run(runner+[binary],check=True,timeout=120)
  for name,mutant in [
   ('partial row treated as whole',code.replace('slot % 4 || count - i < 4','slot % 4')),
   ('changed row misses dirty mark',code.replace('pg->vsh_constants_dirty[const_load] = true;', 'pg->vsh_constants_dirty[const_load] = false;')),
   ('wrong row advance',code.replace('const_load + 1','const_load + 2'))]:
   assert mutant!=code,name
   build(mutant)
   result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
   assert result.returncode,(name,result.stdout)
   print('Required negative control rejected:',name,flush=True)
