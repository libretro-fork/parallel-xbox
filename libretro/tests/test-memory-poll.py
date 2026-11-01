# SPDX-License-Identifier: GPL-2.0-or-later
"""Production RAM-counter wait proof for the NGB slot-0 throughput regression."""
from pathlib import Path
import ctypes,os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'target/i386/tcg/system/misc_helper.c').read_text()
a=s.index('static const uint8_t *lr_idle_ram_ptr(');b=s.index('static bool lr_idle_read_word(',a)
c=s.index('bool xemu_libretro_memory_poll(');d=s.index('/* An eligible block',c)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s[a:b]+s[c:d])
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
if os.name=='nt':ctypes.WinDLL('kernel32').SetErrorMode(0x8003)
with tempfile.TemporaryDirectory(prefix='memory-poll-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=30)
