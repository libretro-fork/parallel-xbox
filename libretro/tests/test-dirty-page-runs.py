# SPDX-License-Identifier: GPL-2.0-or-later
"""Production dirty-run traversal against an independent page oracle."""
from pathlib import Path
import argparse,os,re,shlex,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');p.add_argument('--real-block-size',action='store_true');a=p.parse_args();r=a.checkout.resolve()
s=(r/'system/physmem.c').read_text(encoding='utf-8');start=s.index('bool physical_memory_test_and_clear_dirty_ranges(');end=s.index('{',start)+1;depth=1
while depth:
 depth+=(s[end]=='{')-(s[end]=='}');end+=1
atomic=(r/'include/qemu/bitops.h').read_text();lo=atomic.index('#if ULONG_MAX');hi=atomic.index('\n#endif',lo)+len('\n#endif')
code=Path(__file__).with_suffix('.c').read_text().replace('/* BITMAP ACCESS */',atomic[lo:hi]).replace('/* PRODUCTION */',s[start:end])
with tempfile.TemporaryDirectory(prefix='dirty-runs-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' else 'test')
 cmd=[a.cc,'-O2','-std=gnu11','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-Wno-sign-compare','-I'+str(r/'libretro-common/include'),*shlex.split(os.environ.get('DIRTY_RUN_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' else '-g'))]
 if a.real_block_size:cmd+=['-DREAL_BLOCK_SIZE']
 env=os.environ.copy();env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run(cmd+[str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 if os.environ.get('DIRTY_RUN_BINARY'):
  import shutil
  shutil.copy2(exe,os.environ['DIRTY_RUN_BINARY'])
 if not os.environ.get('DIRTY_RUN_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env)
