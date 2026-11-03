# SPDX-License-Identifier: GPL-2.0-or-later
"""Check directed-rounding arithmetic against actual SoftFloat."""
from pathlib import Path
import os,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
macros=subprocess.run([cc,'-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
if '__x86_64__' not in macros:
 print('Directed SSE: skipped (x86-64 host fast path only)');sys.exit(0)
s=(repo/'target/i386/ops_sse.h').read_text(encoding='utf-8',errors='surrogateescape')
a=s.index('#if defined(__x86_64__)\n#define XEMU_SSE_DIRECTED_ENTRY');body=s[a:s.index('#include "target/i386/tcg/sse-native.h"',a)]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
with tempfile.TemporaryDirectory(prefix='sse-directed-') as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/'test.exe'
 includes=['libretro/gen','.','libretro/glib','include','host/include/x86_64','host/include/generic','libretro-common/include']
 args=[cc,'-O2','-std=gnu11','-fno-strict-aliasing','-fwrapv','-D_GNU_SOURCE','-Werror','-Wall','-Wextra','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
 for inc in includes:args+=['-I'+str(repo/inc)]
 # Compile real SoftFloat too: no arithmetic mocks, and no dependency on build .o files.
 subprocess.run([x for x in args if x not in ['-Wextra','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']]+['-c',str(repo/'fpu/softfloat.c'),'-o',str(p/'softfloat.o')],check=True)
 for shift in [1,2,3]:
  subprocess.run(args+['-DSHIFT='+str(shift),str(p/'test.c'),str(p/'softfloat.o'),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)

 if os.name != 'nt':
  import resource
  resource.setrlimit(resource.RLIMIT_CORE,(0,0))
  header=(repo/'target/i386/tcg/sse-directed.h').read_text()
  needle='if (csr.raised & 0x20u)'
  assert needle in header
  dest=p/'target/i386/tcg';dest.mkdir(parents=True)
  (dest/'sse-directed.h').write_text(header.replace(needle,'if (0)',1))
  negative=p/'negative'
  subprocess.run(args+['-DSHIFT=1',str(p/'test.c'),str(p/'softfloat.o'),'-lm','-o',str(negative)],check=True)
  result=subprocess.run([str(negative)],capture_output=True)
  assert result.returncode != 0 and b'Assertion' in result.stderr
  print('Negative control rejected: native arithmetic must merge guest inexact flags')
