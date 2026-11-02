# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the owner CPU boundary, exceptions, interrupts and BQL guards."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve()
def fn(file,signature):
 s=(r/file).read_text(encoding='utf8');a=s.index(signature);e=s.index('{',a)+1;depth=1
 while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
c='accel/tcg/cpu-exec.c';v='accel/tcg/tcg-accel-ops-libretro.c';h='include/qemu/main-loop.h'
parts=[fn(h,'static inline BQLLockAuto *bql_auto_lock('),fn(h,'static inline void bql_auto_unlock(')]
parts += [fn(c,'static void cpu_exec_longjmp_cleanup('),fn(c,'static inline bool cpu_handle_exception('),fn(c,'static inline bool icount_exit_request('),fn(c,'static inline bool cpu_handle_interrupt(')]
parts += [fn(v,'void lr_vcpu_process_work('),fn(v,'bool lr_vcpu_runnable('),fn(v,'int lr_vcpu_run_slice(')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* GUARDS */','\n'.join(parts[:2])).replace('/* PRODUCTION */','\n'.join(parts[2:]))
code=code.replace('/* FERR */',fn('target/i386/tcg/system/fpu_helper.c','void fpu_check_raise_ferr_irq('))
with tempfile.TemporaryDirectory(prefix='owner-cpu-boundary-') as directory:
 p=Path(directory);(p/'test.c').write_text(code);exe=p/'test.exe'
 flags=[sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
 flags+=shlex.split(os.environ.get('OWNER_CPU_CFLAGS',''))
 subprocess.run(flags+[str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

 # These changes restore the old cross-thread ownership assumptions.
 mutations=[
  ('    g_assert(bql_locked());\n    SET_current_cpu(cpu);',
   '    bql_unlock();\n    SET_current_cpu(cpu);'),
  ('    g_assert(bql_locked());\n    assert_no_pages_locked();',
   '    if (bql_locked()) { bql_unlock(); }\n    assert_no_pages_locked();'),
 ]
 ferr=fn('target/i386/tcg/system/fpu_helper.c','void fpu_check_raise_ferr_irq(')
 mutations.append((ferr,ferr.replace('BQL_LOCK_GUARD();','bql_lock();')))
 for old,new in mutations:
  assert old in code
  (p/'negative.c').write_text(code.replace(old,new,1))
  subprocess.run(flags+[str(p/'negative.c'),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True)
  assert result.returncode and b'owner CPU:' in result.stderr
 print('Negative controls rejected: CPU ownership drop, fault-time release and nested FERR acquisition')
