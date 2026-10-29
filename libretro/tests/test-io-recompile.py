# SPDX-License-Identifier: GPL-2.0-or-later
"""Regress I/O-block resurrection using production learning/invalidation code.
Usage: python test-io-recompile.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
def function(path,name):
 s=(repo/path).read_text();m=re.search(r'^(?:static )?(?:inline )?(?:void|bool|unsigned) '+name+r'\(',s,re.M);assert m,name
 at=s.index('{',m.start());end=at+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
maint='accel/tcg/tb-maint.c';trans='accel/tcg/translate-all.c'
s=(repo/trans).read_text();cache=s[s.index('#define IO_INSN_SLOTS'):s.index('void cpu_io_recompile(')]
production='\n'.join(function(maint,n) for n in ['do_tb_phys_invalidate','tb_phys_invalidate_impl','tb_phys_invalidate','tb_phys_invalidate_recompile'])
production+='\n'+cache+'\n'+function(trans,'cpu_io_recompile')
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',production)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='io-recompile-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
