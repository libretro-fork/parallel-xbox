# SPDX-License-Identifier: GPL-2.0-or-later
"""Fixed x86 binding must preserve the legacy execution-state boundary."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve()
def fn(file,signature):
 s=(r/file).read_text(encoding='utf8');a=s.index(signature);e=s.index('{',a)+1;depth=1
 while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
parts=[fn('target/i386/cpu.h','static inline uint32_t cpu_compute_eflags(')]
for name in ['enter','exit']:parts.append(fn('target/i386/tcg/tcg-cpu.h','static inline void x86_cpu_exec_'+name+'('))
for name in ['enter','exit']:parts.append(fn('accel/tcg/cpu-exec.c','static inline void cpu_exec_'+name+'('))
parts.append(fn('accel/tcg/cpu-exec.c','int cpu_exec('))
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='fixed-x86-entry-') as directory:
 p=Path(directory);exe=p/'test.exe'
 flags=[sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
 flags+=shlex.split(os.environ.get('X86_ENTRY_CFLAGS',''))
 (p/'test.c').write_text(code);subprocess.run(flags+[str(p/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 for old,new in [
  ('    x86_cpu_exec_enter(cpu);','    (void)cpu; /* missing prepare */'),
  ('    x86_cpu_exec_exit(cpu);','    (void)cpu; /* missing completion */'),
  ('env->eflags &= ~(DF_MASK | CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);','env->eflags &= ~(CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C);'),
 ]:
  assert old in code
  (p/'negative.c').write_text(code.replace(old,new,1));subprocess.run(flags+[str(p/'negative.c'),'-o',str(exe)],check=True)
  result=subprocess.run([str(exe)],capture_output=True);assert result.returncode and b'fixed x86:' in result.stderr
 print('Negative controls rejected: missing entry, missing exit and retained direction flag')
