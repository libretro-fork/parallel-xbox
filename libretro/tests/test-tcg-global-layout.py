#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile production global/label allocation against real TCG structures."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

r = Path(sys.argv[1]).resolve()
cc = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
source = (r / 'tcg/tcg.c').read_text()


def function(key):
    start = source.rindex(key)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


keys = ['TCGLabel *gen_new_label(',
        'static TCGTemp *tcg_temp_alloc(', 'static TCGTemp *tcg_global_alloc(',
        'static TCGTemp *tcg_global_reg_new_internal(', 'void tcg_set_frame(',
        'static TCGTemp *tcg_global_mem_new_internal(',
        'TCGv_i32 tcg_global_mem_new_i32(', 'TCGv_i64 tcg_global_mem_new_i64(']
code = Path(__file__).with_suffix('.c').read_text()
code = code.replace('/* PRODUCTION */', '\n'.join(function(key) for key in keys))
with tempfile.TemporaryDirectory(prefix='tcg-global-layout-') as temporary:
    folder = Path(temporary)
    (folder / 'test.c').write_text(code)
    binary = Path(os.environ.get('TCG_GLOBAL_LAYOUT_BINARY', folder / 'test'))
    flags = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-D_GNU_SOURCE', '-DHAVE_THREAD_STORAGE']
    if os.environ.get('TCG_GLOBAL_LAYOUT_TEST_32BIT'):
        # Test real 32-bit types/constructors without changing the production
        # host restrictions. A full 32-bit core needs a separate port/config.
        config = (r / 'libretro/gen/config-host-win32.h').read_text()
        for macro in ('CONFIG_INT128', 'CONFIG_INT128_TYPE'):
            config = config.replace('#define ' + macro + '\n', '#undef ' + macro + '\n')
        guard = '#error "the core\'s TCG runs on x86-64 and AArch64 hosts"'
        if guard not in config:
            raise ValueError('host config guard changed; review the test-only adaptation')
        config = config.replace(guard, '#if !defined(__i386__)\n'
                                '#error "32-bit fixture requires an i386 compiler"\n'
                                '#endif\n#define HOST_I386 1')
        (folder / 'config-host.h').write_text(config)
        flags += ['-isystem', str(folder)]
    for include in ['libretro/gen', 'libretro/glib', '.', 'include', 'tcg/i386',
                    'host/include/generic', 'libretro-common/include']:
        flags += ['-isystem', str(r / include)]
    flags += shlex.split(os.environ.get('TCG_GLOBAL_LAYOUT_CFLAGS', ''))
    subprocess.run(cc + flags + [str(folder / 'test.c'), '-o', str(binary)], check=True)
    if not os.environ.get('TCG_GLOBAL_LAYOUT_SKIP_RUN'):
        subprocess.run([str(binary)], check=True, timeout=30)
