# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production unchanged-store comparisons, including guarded page edges."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile

repo = Path(sys.argv[1]).resolve()
source = (repo / 'accel/tcg/cputlb.c').read_text()
a = source.index('static bool store_changes_memory(')
b = source.index('\nstatic void notdirty_write(', a)
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', source[a:b])
with tempfile.TemporaryDirectory(prefix='store-compare-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(code)
    binary = os.environ.get('STORE_COMPARE_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('STORE_COMPARE_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc') +
                   ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror'] +
                   flags + [str(p / 'test.c'), '-o', binary], check=True)
    if not os.environ.get('STORE_COMPARE_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
