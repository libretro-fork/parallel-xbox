# SPDX-License-Identifier: GPL-2.0-or-later
"""Evaluate production entry-carry IR and the actual class/key definitions."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
cpu=(r/'target/i386/cpu.h').read_text();tr=(r/'target/i386/tcg/translate.c').read_text()
def function(s,needle):
 a=s.index(needle);b=s.index('{',a);depth=1;i=b+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
a=cpu.index('#define TB_CF_UNKNOWN');b=cpu.index('static inline uint32_t tb_cf_flags',a)
prod=cpu[a:b]+function(cpu,'static inline uint32_t tb_cf_flags')+'\n'+function(cpu,'static inline int tb_cf_class_of_flags')+'\n'
a=cpu.rindex('typedef enum {',0,cpu.index('CC_OP_EFLAGS ='));b=cpu.index('} CCOp;',a)+len('} CCOp;')
prod+=cpu[a:b]+'\n'+function(cpu,'static inline int x86_cc_op_cf_class')+'\n'
a=tr.index('typedef struct CCPrepare {');b=tr.index('} CCPrepare;',a)+len('} CCPrepare;')
prod+=tr[a:b]+'\n'+function(tr,'static CCPrepare gen_prepare_eflags_c_entry')+'\n'
source=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',prod)
with tempfile.TemporaryDirectory(prefix='byte-carry-') as td:
 p=Path(td);(p/'test.c').write_text(source)
 for bits in [32,64]:
  exe=p/'test.exe'
  subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-DTEST_TARGET_BITS='+str(bits),str(p/'test.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
