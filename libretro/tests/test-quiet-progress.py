# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production TB-exit path against active and proven-idle guests."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
s = (repo / 'accel/tcg/cpu-exec.c').read_text()
a = s.index('static inline void cpu_loop_exec_tb(')
b = s.index('/* main execution loop */', a)
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', s[a:b])
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='quiet-progress-') as directory:
    tmp = Path(directory)
    (tmp / 'test.c').write_text(code)
    binary = tmp / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                    str(tmp / 'test.c'), '-o', str(binary)], env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True, timeout=30)
