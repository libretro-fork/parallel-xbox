# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the real execution loop and flush body with forced code-cache reuse."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
def function(path,key):
    s=(r/path).read_text(); a=s.index(key); b=s.index('{',a)+1; depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}'); b+=1
    return s[a:b]
loop=function('accel/tcg/cpu-exec.c','static int __attribute__((noinline))\ncpu_exec_loop(')
flush=function('accel/tcg/tb-maint.c','void tb_flush__exclusive_or_serial(')
code=Path(__file__).with_suffix('.c').read_text().replace('/* FLUSH */',flush).replace('/* LOOP */',loop)
with tempfile.TemporaryDirectory(prefix='tb-flush-chain-') as d:
    p=Path(d); src=p/'test.c'; binary=os.environ.get('TB_FLUSH_BINARY',str(p/'test'))
    flags=shlex.split(os.environ.get('TB_FLUSH_CFLAGS','-fsanitize=address,undefined'))
    cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
    args=cc+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-sign-compare']+flags
    src.write_text(code); subprocess.run(args+[str(src),'-o',binary],check=True)
    if not os.environ.get('TB_FLUSH_SKIP_RUN'):
        subprocess.run([binary],check=True,timeout=30)
        for name,mutant in [('stale chain retained',code.replace('generation != tb_ctx.flush_generation','false')),
                            ('flush generation unchanged',code.replace('tb_ctx.flush_generation++;',''))]:
            assert mutant!=code
            src.write_text(mutant)
            subprocess.run(args+['-Wno-unused-variable',str(src),'-o',binary],check=True)
            result=subprocess.run([binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            assert result.returncode, name
            assert b'Assertion' in result.stdout, result.stdout
            print('Required negative control rejected:',name,flush=True)
