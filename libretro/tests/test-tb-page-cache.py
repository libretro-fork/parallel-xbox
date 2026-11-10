# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production cache invalidator, including immediate callers."""
from pathlib import Path
import subprocess, sys, tempfile
repo = Path(sys.argv[1]).resolve()
s = (repo / 'accel/tcg/tb-maint.c').read_text()
a = s.index('static void tb_jmp_cache_inval_tb(')
b = s.index('\n/*', a)
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', s[a:b])
with tempfile.TemporaryDirectory(prefix='tb-page-cache-') as d:
    p = Path(d); (p / 'test.c').write_text(code)
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                    '-fsanitize-undefined-trap-on-error', str(p/'test.c'), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
