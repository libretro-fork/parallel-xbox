# SPDX-License-Identifier: GPL-2.0-or-later
"""Run production CPUID selection and emitted comparison flag snapshots."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
macros=subprocess.run([cc,'-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
if '__x86_64__' not in macros:
 print('FP flags: skipped (x86-64 execution fixture)');sys.exit(0)
def function(text,start):
 a=text.index(start);b=text.index('{',a);depth=1;i=b+1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[a:i]
backend=(r/'tcg/i386/tcg-target.c.inc').read_text()
helper=function(backend,'static void tcg_out_com_flags(')
cpuid=function((r/'util/cpuinfo-i386.c').read_text(),'unsigned __attribute__((constructor)) cpuinfo_init(')
a=backend.index('    case INDEX_op_com_f32:',backend.index('static TCGConstraintSetIndex')) if 'static TCGConstraintSetIndex' in backend else backend.index('    case INDEX_op_com_f32:')
b=backend.index('    case INDEX_op_cvt32f_i32:',a)
constraint='static int constraint_for(int op) { switch(op) {\n'+backend[a:b]+'default: abort(); } }'
source=Path(__file__).with_suffix('.c').read_text().replace('/* CPUINFO */',cpuid).replace('/* EMITTER */',helper).replace('/* CONSTRAINT */',constraint)
with tempfile.TemporaryDirectory(prefix='fp-flags-') as td:
 d=Path(td);(d/'test.c').write_text(source)
 for bits in [64,32]:
  exe=d/'test.exe';args=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-D_GNU_SOURCE','-DTGC_TEST_BITS='+str(bits)]
  for inc in ['libretro/gen','.','libretro/glib','include','host/include/x86_64','host/include/generic','libretro-common/include']:args+=['-I'+str(r/inc)]
  subprocess.run(args+[str(d/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
