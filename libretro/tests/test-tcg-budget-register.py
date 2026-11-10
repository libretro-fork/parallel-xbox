# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import os,re,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'tcg/i386/tcg-target.c.inc').read_text()
def function(name,source=s):
 a=source.index('static void '+name+'(');brace=source.index('{',a);depth=1;end=brace+1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[a:end]+'\n'
names=['tcg_lr_budget_publish','tcg_lr_budget_reload','tcg_out_call','tgen_ld16u','tgen_st16_r','tgen_st16_i']
# Every emitted C call, including software-MMU slow paths, must publish
# before entering C and reload after it can change the canonical budget.
calls=list(re.finditer(r'    tcg_out_branch\(s, 1, [^;]+;',s));assert len(calls)==4
diagnostic=[m for m in calls if '(const tcg_insn_unit *)tlbm_lost' in m[0]]
assert len(diagnostic)==1 and 'static G_NORETURN void tlbm_lost(' in s
calls=[m for m in calls if m not in diagnostic];assert len(calls)==3
for m in calls:
 assert s[:m.start()].rstrip().endswith('tcg_lr_budget_publish(s);')
 assert s[m.end():].lstrip().startswith('tcg_lr_budget_reload(s);')
context=(r/'tcg/tcg.c').read_text()
assert 'tcg_regset_set_reg(s->reserved_regs, TCG_TARGET_LR_ICOUNT_REG);' in context
prologue=s[s.index('static void tcg_target_qemu_prologue('):s.index('static void tcg_target_init(')]
assert prologue.index('tcg_lr_budget_reload(s);') < prologue.index('/* jmp *tb.  */',prologue.index('tcg_out_mov(s, TCG_TYPE_PTR, TCG_AREG0,'))
assert re.search(r'tb_ret_addr = [^;]+;\s*tcg_lr_budget_publish\(s\);',prologue)
macro=re.search(r'^#define LR_ICOUNT_OFFSET .+$',s,re.M)[0]
forget=function('tcg_tlbm_forget',(r/'tcg/tcg-tlbm.c.inc').read_text())
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',macro+'\n'+forget+''.join(map(function,names)))
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='tcg-budget-register-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 for bits,enabled in [(64,True),(32,False)]:
  probe=f'#define TCG_TARGET_REG_BITS {bits}\n#include "tcg-target.h"\n'
  if enabled:
   probe+='#ifndef TCG_TARGET_LR_ICOUNT_REG\n#error missing budget register\n#endif\n_Static_assert(TCG_TARGET_LR_ICOUNT_REG == TCG_REG_R15, "callee-saved budget");\n_Static_assert(TCG_TARGET_LR_ICOUNT_REG != TCG_AREG0, "separate environment register");\n'
  else:
   probe+='#ifdef TCG_TARGET_LR_ICOUNT_REG\n#error unexpected register reservation\n#endif\n'
  (p/'config.c').write_text(probe)
  subprocess.run([cc,'-fsyntax-only','-std=gnu11','-I'+str(r/'tcg/i386'),str(p/'config.c')],env=env,check=True)
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(p/'test.c'),'-o',str(exe)],env=env,check=True)
 subprocess.run([str(exe)],env=env,check=True,timeout=30)
