# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production COMIS helpers against real SoftFloat and count fallbacks."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'target/i386/ops_sse.h').read_text()
a=s.index('#ifdef XBOX\n/* Normal values and signed zero')
b=s.index('void helper_comisd(',a);b=s.index('}',b)+1
body=s[a:b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
with tempfile.TemporaryDirectory(prefix='sse-ordered-compare-') as d:
 p=Path(d);(p/'test.c').write_text(code)
 macros=subprocess.run([cc,'-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
 host='aarch64' if '__aarch64__' in macros else 'x86_64'
 includes=['libretro/gen','.','libretro/glib','include','host/include/'+host,'host/include/generic','libretro-common/include']
 flags=[cc,'-O2','-std=gnu11','-fno-strict-aliasing','-fwrapv','-D_GNU_SOURCE','-Wall','-Werror','-Wno-unused-parameter']
 for inc in includes:flags+=['-I'+str(r/inc)]
 subprocess.run(flags+['-c',str(r/'fpu/softfloat.c'),'-o',str(p/'softfloat.o')],check=True)
 for xbox in [0,1]:
  target=p/('test'+str(xbox)+'.exe')
  subprocess.run(flags+(['-DXBOX=1'] if xbox else [])+['-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),str(p/'softfloat.o'),'-lm','-o',str(target)],check=True)
  subprocess.run([str(target)],check=True)
