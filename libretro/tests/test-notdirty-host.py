# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise resolved-host reuse through production dirty/watchpoint handling."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile

repo = Path(sys.argv[1]).resolve()
s = (repo / 'accel/tcg/cputlb.c').read_text()
a = s.index('static bool store_changes_memory(')
b = s.index('\nstatic int probe_access_internal(', a)
c = s.index('static void mmu_watch_or_dirty(')
d = s.index('\n/**', c)
assert 'qemu_map_ram_ptr' not in s[a:b]
assert 'notdirty_write(env_cpu(env), addr, dirtysize, *pfull, 0, 0, 0, *phost);' in s
assert 'notdirty_write(env_cpu(env), addr, size, full, retaddr, 0, 0, host);' in s
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', s[a:b] + s[c:d])
with tempfile.TemporaryDirectory(prefix='notdirty-host-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(code)
    binary = os.environ.get('NOTDIRTY_HOST_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('NOTDIRTY_HOST_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc') +
                   ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror'] + flags +
                   [str(p / 'test.c'), '-o', binary], check=True)
    if not os.environ.get('NOTDIRTY_HOST_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
