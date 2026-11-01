# SPDX-License-Identifier: GPL-2.0-or-later
"""Production TB lookup must recover class entries despite primary collisions."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'accel/tcg/cpu-exec.c').read_text();a=s.index('static inline TranslationBlock *tb_lookup(');b=s.index('static void log_cpu_exec(',a)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:b])
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='secondary-cache-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
