# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production primary/secondary lookup and invalidation."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve()
def fn(file,signature):
 s=(r/file).read_text();a=s.index(signature);end=s.index('{',a)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[a:end]
h='accel/tcg/tb-hash.h'
prod='\n'.join(fn(h,'static inline unsigned int '+n+'(') for n in ['tb_jmp_cache_hash_page','tb_jmp_cache_hash_func','tb_jmp_cache_class_hash'])
prod+='\n'+fn(h,'static inline void tb_jmp_cache_record(')
prod+='\n'+fn(h,'static inline void tb_jmp_cache_store(')
prod+='\n'+fn('accel/tcg/translate-all.c','void tcg_flush_jmp_cache(')
prod+='\n'+fn('accel/tcg/cputlb.c','static void tb_jmp_cache_clear_page(')
prod+='\n'+fn('accel/tcg/cpu-exec.c','static inline TranslationBlock *tb_lookup(')
prod+='\n'+fn('accel/tcg/tb-maint.c','static void tb_jmp_cache_inval_tb(')
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',prod)
with tempfile.TemporaryDirectory(prefix='class-secondary-') as d:
 p=Path(d);(p/'test.c').write_text(code);exe=p/'test.exe'
 flags=[sys.argv[2] if len(sys.argv)>2 else 'cc','-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error']
 flags+=shlex.split(os.environ.get('TB_SECONDARY_CFLAGS',''))
 subprocess.run(flags+[str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

 if os.name!='nt':
  import resource
  resource.setrlimit(resource.RLIMIT_CORE,(0,0))
  before='    retro_atomic_store_relaxed_ptr(&jc->array[h].tb, tb);\n    tb_jmp_cache_record(jc, h, 0);'
  after='    tb_jmp_cache_record(jc, h, 0);\n    retro_atomic_store_relaxed_ptr(&jc->array[h].tb, tb);'
  assert before in code
  (p/'negative.c').write_text(code.replace(before,after,1))
  negative=p/'negative'
  subprocess.run(flags+[str(p/'negative.c'),'-o',str(negative)],check=True)
  result=subprocess.run([str(negative)],capture_output=True)
  assert result.returncode!=0 and b'Assertion' in result.stderr
  print('Negative control rejected: tracking before pointer publication loses an overlapping insertion')
