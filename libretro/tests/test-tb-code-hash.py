# SPDX-License-Identifier: GPL-2.0-or-later
"""Test the production TB code hash against byte-fetch and fault oracles.
Usage: python test-tb-code-hash.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'accel/tcg/tb-code-hash.h').read_text();s='\n'.join(line for line in s.split('\n') if not line.startswith('#include'))
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='tb-code-hash-',dir=repo.parent) as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),'-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),str(repo/'util/fast-hash.c'),str(repo/'libretro-common/encodings/encoding_xxh3.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env)
