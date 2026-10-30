# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production seed-name timing and Chihiro title precedence.
Usage: python test-retail-seed-name.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/pgraph.c').read_text()
def function(name):
 m=re.search(r'^(?:static )?(?:void|const char \*)\s*'+name+r'\(',s,re.M);assert m,name
 a=s.index('{',m.start());b=a+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[m.start():b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(function(n) for n in ['pgraph_game_tag_of','nv2a_set_game_name','pgraph_seed_game_tag']))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='retail-seed-name-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
