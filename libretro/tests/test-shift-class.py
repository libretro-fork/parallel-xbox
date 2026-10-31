# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the production proof against both possible runtime flag classes."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
cpu=(r/'target/i386/cpu.h').read_text();emit=(r/'target/i386/tcg/emit.c.inc').read_text()
def function(s,needle):
 a=s.index(needle);i=s.index('{',a)+1;depth=1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[a:i]
a=cpu.index('#define TB_CF_UNKNOWN');b=cpu.index('static inline uint32_t tb_cf_flags',a)
defs=cpu[a:b]
a=cpu.rindex('typedef enum {',0,cpu.index('CC_OP_EFLAGS ='));b=cpu.index('} CCOp;',a)+len('} CCOp;')
defs+=cpu[a:b]+'\n'+function(cpu,'static inline int x86_cc_op_cf_class')+'\n'
defs+=function(emit,'static bool can_preserve_shift_cf_class')
source=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#if TEST_BITS == 64
typedef uint64_t target_ulong;
#else
typedef uint32_t target_ulong;
#endif
typedef struct { int cc_op,cf_entry_class; bool cc_op_entry; } DisasContext;
static bool xemu_cpu_boost;
/* PRODUCTION */
int main(void) {
 unsigned kept=0,checked=0;
 for(int old=0;old<CC_OP_DYNAMIC;old++)
 for(int width=0;width<4;width++)
 for(int kind=0;kind<2;kind++)
 for(int boost=0;boost<2;boost++)
 for(int entry=0;entry<2;entry++)
 for(int dynamic=0;dynamic<2;dynamic++) {
   int incoming=x86_cc_op_cf_class(old);
   int next=(kind?CC_OP_SARB:CC_OP_SHLB)+width;
   DisasContext s={.cc_op=dynamic?CC_OP_DYNAMIC:old,
                  .cf_entry_class=incoming,.cc_op_entry=entry};
   xemu_cpu_boost=boost;
   bool keep=can_preserve_shift_cf_class(&s,next);
   if(!boost||!entry||!dynamic) assert(!keep);
   if(keep) {
     kept++;
     for(unsigned cl=0;cl<256;cl++) {
       unsigned count=cl & (width==3?63:31);
       int actual=count?next:old;
       assert(x86_cc_op_cf_class(actual)==incoming);
       checked++;
     }
   }
   if(boost&&entry&&dynamic&&incoming==x86_cc_op_cf_class(next)) assert(keep);
   s.cf_entry_class=TB_CF_ANY;
   assert(!can_preserve_shift_cf_class(&s,next));
 }
 assert(kept && checked);
 printf("shift class: %u retained proofs, %u runtime count outcomes, target=%d passed\n",kept,checked,TEST_BITS);
}
'''.replace('/* PRODUCTION */',defs)
with tempfile.TemporaryDirectory(prefix='shift-class-') as td:
 p=Path(td);(p/'test.c').write_text(source)
 for bits in [32,64]:
  exe=p/'test.exe'
  subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-DTEST_BITS='+str(bits),str(p/'test.c'),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
