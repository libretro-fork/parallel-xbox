# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare the production integer x87 quiet comparison with real SoftFloat."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
s=(r/'target/i386/tcg/fpu_helper.c').read_text()
a=s.index('static inline bool floatx80_is_normal_or_zero__hard(')
b=s.index('#define floatx80_compare_quiet ',a)
body=s[a:b].replace('return floatx80_compare_quiet(a, b, status);','return fallback_compare(a, b, status);')
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',body)
assert '#define floatx80_compare_quiet floatx80_compare_quiet__hard' in s
macros=subprocess.run(cc+['-dM','-E','-'],input='',text=True,capture_output=True,check=True).stdout
host='aarch64' if '__aarch64__' in macros else 'x86_64'
if os.name!='nt':
    import resource
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
with tempfile.TemporaryDirectory(prefix='x87-quiet-compare-') as d:
    p=Path(d);src=p/'test.c';binary=os.environ.get('X87_COMPARE_BINARY',str(p/'test'))
    args=cc+['-O2','-std=gnu11','-fno-strict-aliasing','-fwrapv','-D_GNU_SOURCE','-Wall','-Werror','-Wno-unused-parameter']
    for path in ['libretro/gen','.','libretro/glib','include','host/include/'+host,'host/include/generic','libretro-common/include']:
        args+=['-I'+str(r/path)]
    args+=shlex.split(os.environ.get('X87_COMPARE_CFLAGS','-fsanitize=address,undefined'))
    subprocess.run(args+['-c',str(r/'fpu/softfloat.c'),'-o',str(p/'softfloat.o')],check=True)
    link=[str(p/'softfloat.o'),'-lm','-o',binary]
    src.write_text(code);subprocess.run(args+[str(src)]+link,check=True)
    if not os.environ.get('X87_COMPARE_SKIP_RUN'):
        runner=shlex.split(os.environ.get('X87_COMPARE_RUNNER',''))
        subprocess.run(runner+[binary],check=True,timeout=120)
        for name,mutant in [('denormals accepted as zero',code.replace('e == 0 && a.low == 0','e == 0')),
                            ('negative order not reversed',code.replace('(ka < kb) ^ sa','(ka < kb)')),
                            ('zero signs distinguished',code.replace('if (az && bz)', 'if (false)')),
                            ('fallback loses exception flags',code.replace('return floatx80_compare_quiet(a,b,s);','float_status discarded=*s; return floatx80_compare_quiet(a,b,&discarded);'))]:
            assert mutant!=code,name
            src.write_text(mutant);subprocess.run(args+[str(src)]+link,check=True)
            result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            assert result.returncode and b'Assertion' in result.stdout,(name,result.stdout)
            print('Required negative control rejected:',name,flush=True)
