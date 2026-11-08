# SPDX-License-Identifier: GPL-2.0-or-later
"""Fixed x86 TB exit PC synchronization against the frozen legacy rule."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
s=(r/'target/i386/tcg/tcg-cpu.h').read_text(encoding='utf8')
a=s.index('static inline void x86_cpu_synchronize_from_tb(')
e=s.index('\n}\n',a)+3
helper=s[a:e]
s=(r/'accel/tcg/cpu-exec.c').read_text(encoding='utf8')
a=s.index('    if (*tb_exit > TB_EXIT_IDX1) {',s.index('trace_exec_tb_exit'))
# Extract the complete guarded statement; logging after it is optional.
e=s.index('{',a)+1;depth=1
while depth:depth+=(s[e]=='{')-(s[e]=='}');e+=1
wrapper='static void sync_exit(CPUState *cpu, TranslationBlock *last_tb, int *tb_exit) {\n'+s[a:e]+'\n}\n'
code=Path(__file__).with_suffix('.c').read_text(encoding='utf8').replace('/* PRODUCTION */',helper+'\n'+wrapper)
with tempfile.TemporaryDirectory(prefix='fixed-tb-sync-') as d:
 p=Path(d);exe=p/'test.exe';flags=[sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']+shlex.split(os.environ.get('TB_SYNC_CFLAGS',''))
 def run(text):
  (p/'test.c').write_text(text,encoding='utf8');subprocess.run(flags+[str(p/'test.c'),'-o',str(exe)],check=True);return subprocess.run(shlex.split(os.environ.get('TB_SYNC_RUNNER',''))+[str(exe)],capture_output=True)
 result=run(code);assert result.returncode==0,result.stderr;print(result.stdout.decode(),end='')
 for old,new in [('if (!(tb_cflags(tb) & CF_PCREL))','if ((tb_cflags(tb) & CF_PCREL) || 1)'),('(uint32_t)(tb->pc - tb->cs_base)','(uint32_t)tb->pc'),('if (*tb_exit > TB_EXIT_IDX1)','if (*tb_exit >= TB_EXIT_IDX1)')]:
  assert old in code;result=run(code.replace(old,new,1));assert result.returncode and b'tb sync:' in result.stderr
 print('Negative controls rejected: PC-relative overwrite, missing segment subtraction, normal-exit overwrite')
