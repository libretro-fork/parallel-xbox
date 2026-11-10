# SPDX-License-Identifier: GPL-2.0-or-later
"""Run production watchpoint paths with and without an existing BQL owner."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
root = Path(sys.argv[1]).resolve()
def function(file, signature):
    text = (root / file).read_text()
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
header = 'include/qemu/main-loop.h'
guards = '\n'.join(function(header, name) for name in (
    'static inline BQLLockAuto *bql_auto_lock(', 'static inline void bql_auto_unlock('))
file = 'accel/tcg/watchpoint.c'
production = function(file, 'void cpu_check_watchpoint(')
matcher = function(file, 'static inline bool watchpoint_address_matches(')
code = Path(__file__).with_suffix('.c').read_text().replace('/* GUARDS */', guards).replace('/* MATCHER */', matcher).replace('/* PRODUCTION */', production)
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
runner = shlex.split(os.environ.get('WATCHPOINT_OWNER_RUNNER', ''))
flags = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-I' + str(root / 'include'), '-I' + str(root / 'libretro-common/include')]
flags += shlex.split(os.environ.get('WATCHPOINT_OWNER_CFLAGS', '-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
with tempfile.TemporaryDirectory(prefix='watchpoint-owner-') as temporary:
    path = Path(temporary)
    exe = Path(os.environ.get('WATCHPOINT_OWNER_BINARY', str(path / 'test.exe')))
    old = 'BQL_LOCK_GUARD();\n        cpu_interrupt(cpu, CPU_INTERRUPT_DEBUG);'
    assert old in code
    mutations = [('unconditional watchpoint BQL reacquisition',code.replace(old, 'bql_lock();\n        cpu_interrupt(cpu, CPU_INTERRUPT_DEBUG);\n        bql_unlock();', 1)),
                 ('acknowledged IRQ frame unwound',code.replace('if (!ra && cpu->neg.can_do_io &&', 'if (false && !ra && cpu->neg.can_do_io &&')),
                 ('translated access skips retry',code.replace('if (!ra && cpu->neg.can_do_io &&', 'if (cpu->neg.can_do_io &&')),
                 ('stop-before delivery ignored',code.replace('!(wp->flags & BP_STOP_BEFORE_ACCESS)) {','true) {'))]
    if os.environ.get('WATCHPOINT_OWNER_SKIP_RUN'):
        mutations=[]
    for name,test in [('positive',code)]+mutations:
        negative=name!='positive'
        if negative:assert test!=code,name
        source = path / 'test.c'
        source.write_text(test)
        subprocess.run(compiler + flags + [str(source), '-o', str(exe)], check=True)
        if os.environ.get('WATCHPOINT_OWNER_SKIP_RUN'):continue
        result = subprocess.run(runner + [str(exe)], capture_output=negative, timeout=30)
        if negative:
            assert result.returncode and b'watchpoint ownership:' in result.stderr
        else:
            result.check_returncode()
        if negative:print('Negative control rejected:',name,flush=True)
