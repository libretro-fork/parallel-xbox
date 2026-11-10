# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare production scalar MMX generators with the current MMX helpers."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve(); s=(r/'target/i386/tcg/emit.c.inc').read_text()
def function(key):
    a=s.index(key); b=s.index('{',a)+1; depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}'); b+=1
    return s[a:b]
a=s.index('typedef void (*GenMMX)');b=s.index('#define BINARY_INT_MMX(',a)
generators=s[a:b]
a=s.index('#define BINARY_INT_MMX_I64(');b=s.index('BINARY_INT_MMX_I64(PUNPCKLBW',a)
generators+=s[a:b]
names=['PUNPCKLBW','PUNPCKLWD','PUNPCKLDQ','PUNPCKHBW','PUNPCKHWD','PUNPCKHDQ','PMULHW']
for name in names:
    import re
    m=re.search(r'^BINARY_INT_MMX_I64\('+name+r',.*$',s,re.M); assert m,name
    generators+=m.group(0)+'\n'
helper=(r/'target/i386/ops_sse.h').read_text()
a=helper.index('#define SSE_HELPER_2(');b=helper.index('#if SHIFT == 0\nstatic inline int satub',a)
oracle=helper[a:b]
oracle+=next(line for line in helper.splitlines() if line.startswith('#define FMULHW('))+'\nSSE_HELPER_W(helper_pmulhw, FMULHW)\n'
a=helper.index('#define UNPCK_OP(');b=helper.index('#undef PACK_WIDTH',a)
oracle+=helper[a:b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* ORACLE */',oracle)
code=code.replace('/* OFFSET */',function('static int vector_reg_offset('))
code=code.replace('/* DISPATCH */',function('static inline void gen_binary_int_sse('))
code=code.replace('/* GENERATORS */',generators)
with tempfile.TemporaryDirectory(prefix='mmx-scalar-') as d:
    p=Path(d);src=p/'test.c';binary=os.environ.get('MMX_SCALAR_BINARY',str(p/'test'))
    flags=shlex.split(os.environ.get('MMX_SCALAR_CFLAGS','-fsanitize=address,undefined'))
    cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
    args=cc+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter']+flags
    src.write_text(code);subprocess.run(args+[str(src),'-o',binary],check=True)
    if not os.environ.get('MMX_SCALAR_SKIP_RUN'):
        runner=shlex.split(os.environ.get('MMX_SCALAR_RUNNER',''))
        subprocess.run(runner+[binary],check=True,timeout=60)
        for name,mutant in [('high-half base lost',code.replace('MO_8, 32)','MO_8, 0)')),
                            ('signed multiply lost',code.replace('tcg_gen_sextract_i64(x, a, 16 * i, 16);','tcg_gen_shri_i64(x, a, 16 * i);')),
                            ('SSE prefix accepted',code.replace('PREFIX_DATA | PREFIX_VEX','PREFIX_VEX'))]:
            assert mutant!=code,name
            src.write_text(mutant);subprocess.run(args+[str(src),'-o',binary],check=True)
            result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            assert result.returncode,name
            assert b'Assertion' in result.stdout,result.stdout
            print('Required negative control rejected:',name,flush=True)
