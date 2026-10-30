# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production arithmetic helpers against the old per-lane gate."""
from pathlib import Path
import subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
macros=subprocess.run([cc,'-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
if '__x86_64__' not in macros:
 print('SSE native gate: skipped (x86-64 host fast path only)');sys.exit(0)
s=(repo/'target/i386/ops_sse.h').read_text(encoding='utf-8',errors='surrogateescape')
a=s.index('#define SSE_HELPER_P(');body=s[a:s.index('void glue(helper_sqrtps',a)]
# Count actual eligibility decisions without replacing the production gate.
body=body.replace('static inline bool sse_native_ok(', 'static inline bool sse_native_ok_impl(')
needle='#define XEMU_SSE_NATIVE_INIT'
body=body.replace(needle, 'static bool sse_native_ok(CPUX86State *env) { gates++; return sse_native_ok_impl(env); }\n'+needle,1)
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
with tempfile.TemporaryDirectory(prefix='sse-native-gate-') as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/'test.exe'
 includes=['libretro/gen','.','libretro/glib','include','host/include/x86_64','host/include/generic','libretro-common/include']
 args=[cc,'-O2','-std=gnu11','-fno-strict-aliasing','-fwrapv','-DXBOX=1','-D_GNU_SOURCE','-Werror','-Wall','-Wextra','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
 for inc in includes:args+=['-I'+str(repo/inc)]
 # Compile real SoftFloat too: no arithmetic mocks, and no dependency on build .o files.
 subprocess.run([x for x in args if x not in ['-Wextra','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']]+['-c',str(repo/'fpu/softfloat.c'),'-o',str(p/'softfloat.o')],check=True)
 for shift in [1,2,3]:
  subprocess.run(args+['-DSHIFT='+str(shift),str(p/'test.c'),str(p/'softfloat.o'),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
