# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production quiet-reset rewrite with safe and bypassing prefixes."""
from pathlib import Path
import os, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve(); cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'accel/tcg/translator.c').read_text();a=s.index('void lr_quiet_store_emitted(void)');b=s.index('static void gen_tb_end(',a)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:b])
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='quiet-store-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
