# SPDX-License-Identifier: GPL-2.0-or-later
"""Production null sink and generic chardev write semantics with observable lock/log stubs."""
from pathlib import Path
import argparse,os,re,shlex,shutil,subprocess,tempfile
p=argparse.ArgumentParser();p.add_argument('checkout',type=Path);p.add_argument('cc',nargs='?',default='cc');a=p.parse_args();r=a.checkout.resolve()
s=(r/'chardev/char.c').read_text(encoding='utf-8');parts=[]
for name in ['qemu_chr_write_log','null_chr_write','qemu_chr_write_buffer','qemu_chr_write']:
 m=re.search(r'^(?:static )?(?:void|int) '+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name;lo=m.start();hi=s.index('{',lo)+1;depth=1
 while depth:
  depth+=(s[hi]=='{')-(s[hi]=='}');hi+=1
 parts.append(s[lo:hi])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='null-chardev-') as tmp:
 d=Path(tmp);(d/'test.c').write_text(code);exe=d/('test.exe' if os.name=='nt' or 'mingw' in a.cc else 'test')
 flags=shlex.split(os.environ.get('NULL_CHARDEV_CFLAGS','-fsanitize=address,undefined' if os.name!='nt' else '-g'))
 env=os.environ.copy();env['PATH']=str(Path(a.cc).resolve().parent)+os.pathsep+env.get('PATH','')
 subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare','-I'+str(r/'libretro-common/include'),*flags,str(d/'test.c'),'-o',str(exe)],check=True,env=env)
 if os.environ.get('NULL_CHARDEV_BINARY'):shutil.copy2(exe,os.environ['NULL_CHARDEV_BINARY'])
 if not os.environ.get('NULL_CHARDEV_SKIP_RUN'):subprocess.run([str(exe)],check=True,env=env)
