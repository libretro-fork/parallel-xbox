# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production TLB dirty-range reprotection against an exhaustive scan.
Usage: python test-tlb-dirty.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'accel/tcg/cputlb.c').read_text()
def function(name):
 m=re.search(r'^(?:static )?(?:inline )?void '+name+r'\(',s,re.M);assert m
 at=s.index('{',m.start());end=at+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
fixture=Path(__file__).with_suffix('.c').read_text()
code=fixture.replace('/* PRODUCTION */','\n'.join(function(name) for name in ['tlb_reset_dirty_range_locked','tlb_reset_dirty','tlb_set_dirty1_locked','tlb_set_dirty']))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='tlb-dirty-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(repo/'include'),str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
