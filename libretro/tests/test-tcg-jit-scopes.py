# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production invalidation scopes with protected block metadata and code."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve()
if os.name!='nt':
    import resource
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
def function(path,key):
    s=(r/path).read_text();a=s.index(key);b=s.index('{',a)+1;depth=1
    while depth:
        depth+=(s[b]=='{')-(s[b]=='}');b+=1
    return s[a:b]
maint='accel/tcg/tb-maint.c'
pieces=[function(maint,'static void tb_phys_invalidate_impl('),
        function(maint,'static void tb_phys_invalidate__locked('),
        function(maint,'void tb_phys_invalidate('),
        function(maint,'void tb_phys_invalidate_recompile(')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(pieces))
with tempfile.TemporaryDirectory(prefix='tcg-jit-scope-') as d:
    p=Path(d);src=p/'test.c';binary=os.environ.get('JIT_SCOPE_BINARY',str(p/'test'))
    args=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')+['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-sign-compare']
    args+=shlex.split(os.environ.get('JIT_SCOPE_CFLAGS','-fsanitize=address,undefined'))
    runner=shlex.split(os.environ.get('JIT_SCOPE_RUNNER',''))
    src.write_text(code);subprocess.run(args+[str(src),'-o',binary],check=True)
    if not os.environ.get('JIT_SCOPE_SKIP_RUN'):
        subprocess.run(runner+[binary],check=True,timeout=30)
        controls=[('single-block invalidation without write access',code.replace(pieces[0],pieces[0].replace('    qemu_thread_jit_write();','',1),1)),
                  ('single-block invalidation left writable',code.replace(pieces[0],pieces[0].replace('    qemu_thread_jit_execute();','',1),1)),
                  ('range invalidation without write access',code.replace(pieces[1],pieces[1].replace('    qemu_thread_jit_write();','',1),1))]
        for name,mutant in controls:
            assert mutant!=code,name
            src.write_text(mutant);subprocess.run(args+[str(src),'-o',binary],check=True)
            result=subprocess.run(runner+[binary],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            assert result.returncode,(name,result.stdout)
            print('Required negative control rejected:',name,flush=True)
