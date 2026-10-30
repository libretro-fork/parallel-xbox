# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the production TB page collection against a reference.
Usage: python test-page-collection.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'accel/tcg/tb-maint.c').read_text()
def block(m):
 at=s.index('{',m.start());end=at+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
def function(name):
 m=re.search(r'^static (?:inline )?(?:void|bool) '+name+r'\(',s,re.M);assert m,name
 return block(m)
def struct(name):
 m=re.search(r'^struct '+name+r' \{',s,re.M);assert m,name
 return block(m)+';'
def define(name):
 # the last one: the system build's, after the user-only build's
 m=list(re.finditer(r'^#define '+name+r'\b.*?(?<!\\)\n',s,re.M|re.S));assert m,name
 return m[-1].group(0)
parts=['typedef int PageForEachNext;',define('TB_FOR_EACH_TAGGED'),define('PAGE_FOR_EACH_TB'),define('PAGE_COLLECTION_INLINE'),
 struct('page_entry'),struct('page_collection')]+[function(n) for n in
 ['page_entry_trylock','do_page_entry_lock','page_collection_lock_all','page_collection_unlock_all',
  'page_trylock_add','page_collection_lock','page_collection_unlock']]
fixture=Path(__file__).with_suffix('.c').read_text()
code=fixture.replace('/* PRODUCTION */','\n'.join(parts))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='page-collection-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
