# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'target/i386/tcg/translate.c').read_text();a=s.index('static bool xemu_ram_wait_candidate(');b=s.index('static void i386_tr_tb_start(',a)
h=(r/'target/i386/tcg/system/misc_helper.c').read_text();c=h.index('uint32_t helper_xemu_memory_wait(');d=h.index('/* A port read',c)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:b]+h[c:d])
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='early-memory-wait-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=30)
