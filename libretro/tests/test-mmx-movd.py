# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the production MOVD generator's values, clearing and store count."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();s=(r/'target/i386/tcg/emit.c.inc').read_text()
a=s.index('static void gen_MOVD_to(');b=s.index('{',a)+1;depth=1
while depth:
    depth+=(s[b]=='{')-(s[b]=='}');b+=1
body=s[a:b]
code=Path(__file__).with_suffix('.c').read_text().replace('/* GENERATOR */',body)
with tempfile.TemporaryDirectory(prefix='mmx-movd-') as d:
    p=Path(d);src=p/'test.c';binary=os.environ.get('MMX_MOVD_BINARY',str(p/'test'))
    args=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter']
    args+=shlex.split(os.environ.get('MMX_MOVD_CFLAGS','-fsanitize=address,undefined'))
    src.write_text(code);subprocess.run(args+[str(src),'-o',binary],check=True)
    if not os.environ.get('MMX_MOVD_SKIP_RUN'):
        runner=shlex.split(os.environ.get('MMX_MOVD_RUNNER',''))
        subprocess.run(runner+[binary],check=True,timeout=30)
        for name,mutant in [('old double store retained',code.replace('vec_len == 8 && ot == MO_32','false')),
                            ('sign extension used',code.replace('*d=*s;', '*d=(int64_t)(int32_t)*s;')),
                            ('SSE clearing bypassed',code.replace('vec_len == 8 && ot == MO_32','vec_len <= 16 && ot == MO_32'))]:
            assert mutant!=code,name
            src.write_text(mutant);subprocess.run(args+[str(src),'-o',binary],check=True)
            result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            assert result.returncode and b'Assertion' in result.stdout,(name,result.stdout)
            print('Required negative control rejected:',name,flush=True)
