# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual file backend scalar/vector range and mapped-byte oracle."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
with tempfile.TemporaryDirectory(prefix='file-vector-') as tmp:
 p=Path(tmp);binary=os.environ.get('FILE_VECTOR_BINARY',str(p/'test'))
 shared=bool(os.environ.get('FILE_VECTOR_SHARED'))
 win='mingw' in cc or os.name=='nt'
 flags=shlex.split(os.environ.get('FILE_VECTOR_CFLAGS','-static' if win else '' if shared else '-fsanitize=address,undefined'))
 if os.environ.get('FILE_VECTOR_SOURCE'):flags+=['-DFILE_VECTOR_SOURCE="'+str(Path(os.environ['FILE_VECTOR_SOURCE']).resolve()).replace('\\','/')+'"']
 if shared:flags+=['-shared','-DFILE_VECTOR_SHARED']+([] if win else ['-fPIC','-Wl,-Bsymbolic'])
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r),'-I'+str(r/'block'),*flags,str(Path(__file__).with_suffix('.c')),'-o',binary],check=True)
 if shared:
  loader=os.environ.get('FILE_VECTOR_LOADER',str(p/'loader'))
  subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-cast-function-type',str(Path(__file__).with_name('test-file-vector-loader.c'))]+(['-static'] if win else ['-ldl'])+['-o',loader],check=True)
 if not os.environ.get('FILE_VECTOR_SKIP_RUN'):
  args=sys.argv[3:] or [str(p/'native.img')]
  subprocess.run(([loader,binary] if shared else [binary])+args,check=True,timeout=120)
