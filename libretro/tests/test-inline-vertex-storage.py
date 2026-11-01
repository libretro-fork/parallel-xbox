# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production inline backfill/append and allocation lifetime."""
from pathlib import Path
import argparse,os,shlex,shutil,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');a=p.parse_args();r=a.checkout.resolve()
s=(r/'hw/xbox/nv2a/pgraph/vertex.c').read_text(encoding='utf-8')
def function(name):
 lo=s.index('void '+name+'(');end=s.index('{',lo)+1;depth=1
 while depth:
  depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[lo:end]
init=(r/'hw/xbox/nv2a/pgraph/pgraph.c').read_text(encoding='utf-8');lo=init.index('void pgraph_init(');hi=init.index('void pgraph_clear_dirty_reg_map(',lo);init=init[lo:hi]
assert 'attribute->inline_buffer = NULL;' in init
assert 'NV2A_MAX_BATCH_LENGTH' not in init
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(function(n) for n in ['pgraph_allocate_inline_buffer_vertices','pgraph_finish_inline_buffer_vertex']))
with tempfile.TemporaryDirectory(prefix='inline-storage-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' or 'mingw' in a.cc else 'test')
 flags=shlex.split(os.environ.get('INLINE_STORAGE_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' else '-g'))
 env=os.environ.copy();env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-sign-compare',*flags,str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 if os.environ.get('INLINE_STORAGE_BINARY'):shutil.copy2(exe,os.environ['INLINE_STORAGE_BINARY'])
 if not os.environ.get('INLINE_STORAGE_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env)
