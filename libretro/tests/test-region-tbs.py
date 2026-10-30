# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the production TB index of a code region against a reference.
Usage: python test-region-tbs.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'tcg/region.c').read_text()
def block(m,semi=False):
 at=s.index('{',m.start());end=at+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]+(';' if semi else '')
def function(name):
 m=re.search(r'^(?:static )?[\w ]+?\*?\s*'+name+r'\(',s,re.M);assert m,name
 return block(m)
def struct(name):
 m=re.search(r'^struct '+name+r' \{',s,re.M);assert m,name
 return block(m,True)
parts=[struct('tcg_region_tb'),struct('tcg_region_tree')]+[function(n) for n in
 ['tb_destroy','region_tb_upper','tcg_tb_insert','tcg_tb_remove','tcg_tb_lookup',
  'tcg_region_tree_lock_all','tcg_region_tree_unlock_all',
  'tcg_region_tree_reset_all']]
fixture=Path(__file__).with_suffix('.c').read_text()
code=fixture.replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='region-tbs-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-missing-field-initializers','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
