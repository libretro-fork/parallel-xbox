# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production cpu_exec FP boundary, including longjmp/reset."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
macros=subprocess.run([cc,'-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
if '__x86_64__' not in macros:
 print('SSE context: skipped (x86-64 host scope only)');sys.exit(0)
def function(file,signature):
 s=(r/file).read_text();a=s.index(signature);b=s.index('{',a)+1;depth=1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
prod=function('target/i386/ops_sse.h','static inline bool sse_native_ok(')
prod+='\n'+function('accel/tcg/cpu-exec.c','int cpu_exec(CPUState *cpu)')
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',prod)
with tempfile.TemporaryDirectory(prefix='sse-context-') as d:
 p=Path(d);(p/'test.c').write_text(code)
 flags=[cc,'-O2','-std=gnu11','-DXBOX=1','-D_GNU_SOURCE','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
 for inc in ['libretro/gen','.','libretro/glib','include','host/include/x86_64','host/include/generic','libretro-common/include']:flags+=['-I'+str(r/inc)]
 subprocess.run(flags+[str(p/'test.c'),'-o',str(p/'test.exe')],check=True)
 subprocess.run([str(p/'test.exe')],check=True)
 if os.name!='nt':
  import resource
  resource.setrlimit(resource.RLIMIT_CORE,(0,0))
  needle='xemu_sse_context_exit(cpu_env(cpu), saved_mxcsr);';assert needle in code
  (p/'negative.c').write_text(code.replace(needle,'(void)saved_mxcsr;',1))
  subprocess.run(flags+[str(p/'negative.c'),'-o',str(p/'negative')],check=True)
  result=subprocess.run([str(p/'negative')],capture_output=True)
  assert result.returncode!=0 and b'Assertion' in result.stderr
  print('Negative control rejected: returning without FP restoration leaks host state')
