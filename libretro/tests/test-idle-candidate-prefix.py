# SPDX-License-Identifier: GPL-2.0-or-later
"""Keep every live idle-proof entry eligible without probing a second page."""
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'accel/tcg/translator.c').read_text();a=s.index('static bool lr_quiet_candidate_prefix(');b=s.index('static TCGOp *gen_tb_start(',a)
h=(r/'target/i386/tcg/system/misc_helper.c').read_text();k=h[h.index('bool xemu_libretro_kernel_idle('):h.index('bool xemu_libretro_memory_poll(')]
positions=re.search(r'static const unsigned positions\[\] = \{([^}]+)\}',k)[1]
pattern=re.search(r'memcmp\(code, ("[^"]+"), 8\)',k)[1]
m=h[h.index('bool xemu_libretro_memory_poll('):];poll=re.search(r'static const uint8_t loop\[\] = (\{.*?\});',m,re.S)[1]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:b]).replace('/* POSITIONS */',positions).replace('/* KERNEL */',pattern).replace('/* POLL */',poll)
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='idle-candidate-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
