# SPDX-License-Identifier: GPL-2.0-or-later
"""Run real SoftFloat loads and production hard-FPU TCG generators."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
macros=subprocess.run([cc,'-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
if '__x86_64__' not in macros:
    print('x87 load precision: skipped (native x86-64 oracle)');sys.exit(0)


def function(source,name):
    m=re.search(r'^(?:static )?(?:inline )?(?:void|int|bool|FloatX80RoundPrec) '+name+r'\([^;{]*\)\s*\{',source,re.M);assert m,name
    end=source.index('{',m.start())+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]


helper=(repo/'target/i386/tcg/fpu_helper.c').read_text()
soft='\n'.join(function(helper,n) for n in ('fpu_set_exception','save_exception_flags','merge_exception_flags',
              'tmp_maximise_precision','helper_fldl_FT0','helper_fldl_ST0'))
translate=(repo/'target/i386/tcg/translate.c').read_text()
hard='\n'.join(function(translate,n) for n in ('fpu_rounding_to_single','gen_round_to_single','gen_flush_fp',
       'gen_fpush','gen_fpop','gen_fldl_FT0','gen_fldl_ST0','gen_fmov_FT0_STN',
       'gen_fmov_ST0_STN','gen_fmov_STN_ST0','gen_fxchg_ST0_STN','gen_fchs_ST0','gen_fabs_ST0','gen_fsqrt',
       'gen_helper_fp_arith_ST0_FT0','gen_helper_fp_arith_STN_ST0','gen_fildl_ST0','gen_fildll_ST0',
       'gen_fistl_ST0','gen_fistll_ST0','gen_fstl_ST0','gen_fsts_ST0'))
code=Path(__file__).with_suffix('.c').read_text().replace('/* SOFT HELPERS */',soft).replace('/* TRANSLATOR */',hard)
with tempfile.TemporaryDirectory(prefix='x87-load-precision-') as directory:
    p=Path(directory);(p/'test.c').write_text(code)
    args=[cc,'-O2','-std=gnu11','-fno-strict-aliasing','-frounding-math','-fwrapv','-DXBOX=1','-D_GNU_SOURCE',
          '-Wall','-Werror','-Wno-unused-function','-Wno-unused-parameter']
    for inc in ['libretro/gen','.','libretro/glib','include','host/include/x86_64','host/include/generic','libretro-common/include']:
        args+=['-I'+str(repo/inc)]
    subprocess.run(args+['-c',str(repo/'fpu/softfloat.c'),'-o',str(p/'softfloat.o')],check=True)
    subprocess.run(args+['-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),str(p/'softfloat.o'),'-lm','-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True,timeout=30)
