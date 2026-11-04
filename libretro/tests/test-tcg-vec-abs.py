#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check vector ABS fallbacks and diagnostic capability-query elision."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
r = Path(sys.argv[1]).resolve()
cc = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
s = (r / 'tcg/tcg-op-vec.c').read_text()
a = s.index('void tcg_gen_abs_vec(')
b = s.index('{', a) + 1
depth = 1
while depth:
    depth += (s[b] == '{') - (s[b] == '}')
    b += 1
source = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', s[a:b])
with tempfile.TemporaryDirectory(prefix='tcg-vec-abs-') as temporary:
    folder = Path(temporary)
    (folder / 'test.c').write_text(source)
    prefix = os.environ.get('TCG_VEC_ABS_BINARY_PREFIX', str(folder / 'test'))
    suffix = '.exe' if os.name == 'nt' or 'mingw' in ' '.join(cc) else ''
    for mode, define in [('release', []), ('debug', ['-DCONFIG_DEBUG_TCG']),
                         ('analysis', ['-DQEMU_STATIC_ANALYSIS'])]:
        binary = Path(prefix + '-' + mode + suffix)
        flags = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wmissing-prototypes',
                 '-I' + str(r / 'include'), *define]
        flags += shlex.split(os.environ.get('TCG_VEC_ABS_CFLAGS', ''))
        subprocess.run(cc + flags + [str(folder / 'test.c'), '-o', str(binary)], check=True)
        if not os.environ.get('TCG_VEC_ABS_SKIP_RUN'):
            subprocess.run([str(binary)], check=True, timeout=30)
