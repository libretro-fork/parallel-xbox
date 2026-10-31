# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production indirect-branch state/flag lookup in both target paths."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()


def function(path, name):
    source = (repo / path).read_text()
    match = re.search(r'^(?:static )?(?:inline )?(?:TCGTBCPUState|uint32_t|const void \*) ?'
                      + re.escape(name) + r'\([^;{]*\)\s*\{', source, re.M)
    assert match, name
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


production = function('target/i386/tcg/tcg-cpu.c', 'x86_get_tb_cpu_state')
production += '\n' + function('accel/tcg/cpu-exec-common.c', 'curr_cflags')
production += '\n' + '\n'.join(function('accel/tcg/cpu-exec.c', name) for name in
                              ('xemu_get_tb_cpu_state', 'xemu_lookup_cflags', 'HELPER(lookup_tb_ptr)'))
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', production)
with tempfile.TemporaryDirectory(prefix='tb-lookup-state-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(code)
    for xbox in (False, True):
        exe = folder / 'test'
        subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'cc', '-O2', '-std=gnu11',
                        '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                        '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
                        *(['-DXBOX'] if xbox else []), str(folder/'test.c'), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)
