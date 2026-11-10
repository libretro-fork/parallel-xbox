# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare the production software-FPU select with its exact image-copy helper."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
def function(path,key):
    s=(r/path).read_text();a=s.index(key);b=s.index('{',a)+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
code=Path(__file__).with_suffix('.c').read_text()
code=code.replace('/* POINTER */',function('target/i386/tcg/translate.c','static TCGv_ptr gen_stn_ptr('))
code=code.replace('/* SELECT */',function('target/i386/tcg/translate.c','static bool gen_fcmov_soft('))
code=code.replace('/* HELPER */',function('target/i386/tcg/fpu_helper.c','void helper_fmov_ST0_STN('))
with tempfile.TemporaryDirectory(prefix='soft-fcmov-') as d:
    p=Path(d);src=p/'test.c';binary=os.environ.get('SOFT_FCMOV_BINARY',str(p/'test'))
    args=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-missing-field-initializers']
    args+=shlex.split(os.environ.get('SOFT_FCMOV_CFLAGS','-fsanitize=address,undefined'))
    runner=shlex.split(os.environ.get('SOFT_FCMOV_RUNNER',''))
    src.write_text(code);subprocess.run(args+[str(src),'-o',binary],check=True)
    if not os.environ.get('SOFT_FCMOV_SKIP_RUN'):
        subprocess.run(runner+[binary],check=True,timeout=60)
        for name,mutant in [('condition reversed',code.replace('test64(c,a,b)?*v1:*v2','test64(c,a,b)?*v2:*v1')),
                            ('upper image lost',code.replace('tcg_gen_st16_i32(hi0, p0, offsetof(floatx80, high));','')),
                            ('hard-FPU cache bypassed',code.replace('if (g_use_hard_fpu)', 'if (false)'))]:
            assert mutant!=code,name
            src.write_text(mutant);subprocess.run(args+[str(src),'-o',binary],check=True)
            result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            assert result.returncode and b'Assertion' in result.stdout,(name,result.stdout)
            print('Required negative control rejected:',name,flush=True)
