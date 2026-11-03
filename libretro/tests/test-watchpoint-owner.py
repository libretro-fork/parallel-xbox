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
    exe = path / 'test.exe'
    for negative in (False, True):
        test = code
        if negative:
            old = 'BQL_LOCK_GUARD();\n        cpu_interrupt(cpu, CPU_INTERRUPT_DEBUG);'
            assert old in test
            test = test.replace(old, 'bql_lock();\n        cpu_interrupt(cpu, CPU_INTERRUPT_DEBUG);\n        bql_unlock();', 1)
        source = path / 'test.c'
        source.write_text(test)
        subprocess.run(compiler + flags + [str(source), '-o', str(exe)], check=True)
        result = subprocess.run(runner + [str(exe)], capture_output=negative, timeout=30)
        if negative:
            assert result.returncode and b'watchpoint ownership:' in result.stderr
        else:
            result.check_returncode()
    print('Negative control rejected unconditional watchpoint BQL reacquisition')
