# SPDX-License-Identifier: GPL-2.0-or-later
"""Run physical-page store tests against the production invalidation function."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile
repo = Path(sys.argv[1]).resolve()
s = Path(os.environ.get('TB_STORE_SOURCE', repo / 'accel/tcg/tb-maint.c')).read_text()
a = s.index('static void\ntb_invalidate_phys_page_range__locked')
b = s.index('\n/*', a + 15)
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', s[a:b])
with tempfile.TemporaryDirectory(prefix='tb-store-', dir=repo.parent) as d:
    p = Path(d)
    (p / 'test.c').write_text(code)
    binary = os.environ.get('TB_STORE_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('TB_STORE_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc') +
                   ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-sign-compare'] + flags + [str(p / 'test.c'), '-o', binary], check=True)
    if not os.environ.get('TB_STORE_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
