# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare scoped native SQRT helpers with a software-only SoftFloat oracle."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'target/i386/ops_sse.h').read_text()
a=s.index('#include "target/i386/tcg/sse-native.h"');body=s[a:s.index('/* float to float conversions */',a)]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
with tempfile.TemporaryDirectory(prefix='sse-native-sqrt-') as d:
 p=Path(d);(p/'test.c').write_text(code)
 macros=subprocess.run([cc,'-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
 host='aarch64' if '__aarch64__' in macros else 'x86_64'
 includes=['libretro/gen','.','libretro/glib','include','host/include/'+host,'host/include/generic','libretro-common/include']
 flags=[cc,'-O2','-std=gnu11','-fno-strict-aliasing','-fwrapv','-D_GNU_SOURCE','-Wall','-Werror','-Wno-unused-parameter']
 for inc in includes:flags+=['-I'+str(r/inc)]
 # Disable SoftFloat's opportunistic host-FP shortcuts in the oracle, without
 # enabling fast-math compiler optimizations. This exercises actual software
 # rounding/flags even when the host MXCSR deliberately disagrees with guest.
 for software in [0,1]:
  oracle_flags=['-D__FAST_MATH__=1','-Wno-error=cpp'] if software else []
  subprocess.run(flags+oracle_flags+['-c',str(r/'fpu/softfloat.c'),'-o',str(p/'softfloat.o')],check=True)
  for shift in [1,2]:
   exe=p/('test'+str(shift)+'.exe')
   subprocess.run(flags+['-DSHIFT='+str(shift),'-DSOFTWARE_ORACLE='+str(software),'-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),str(p/'softfloat.o'),'-lm','-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)

 if os.name != 'nt' and '__x86_64__' in macros:
  import resource
  resource.setrlimit(resource.RLIMIT_CORE,(0,0))
  header=(r/'target/i386/tcg/sse-native.h').read_text()
  assert '0x7020u' in header
  dest=p/'target/i386/tcg';dest.mkdir(parents=True)
  (dest/'sse-native.h').write_text(header.replace('0x7020u','0x1020u'))
  exe=p/'negative'
  subprocess.run(flags+['-DSHIFT=1','-DSOFTWARE_ORACLE=1',str(p/'test.c'),str(p/'softfloat.o'),'-lm','-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True)
  assert result.returncode != 0 and b'sqrt mismatch' in result.stderr
  print('Negative control rejected: sticky flags alone cannot bypass host rounding isolation')
