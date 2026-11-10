#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Differential operand initialization/copy propagation with actual TCG types."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
if os.name != 'nt' and os.environ.get('TCG_COPY_NEGATIVE'):
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
cc = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
s = (r / 'tcg/optimize.c').read_text()


def function(key):
    start = s.index(key)
    end = s.index('{', start) + 1
    depth = 1
    while depth:
        depth += (s[end] == '{') - (s[end] == '}')
        end += 1
    return s[start:end]


types = s[s.index('typedef struct MemCopyInfo'):s.index('static inline TempOptInfo *ts_info')]
parts = types + '\n' + '\n'.join(function(k) for k in [
    'static inline TempOptInfo *ts_info(', 'static inline bool ts_is_copy(',
    'static TCGTemp *cmp_better_copy(', 'static void init_ts_info(',
    'static TCGTemp *find_better_copy(', 'static void init_and_copy_propagate('])
reference = Path(__file__).with_name('tcg-optimizer-copy-reference.h').read_text()
for name in ['MemCopyInfo', 'TempOptInfo', 'OptContext', 'ts_info', 'ts_is_copy',
             'cmp_better_copy', 'init_ts_info', 'find_better_copy',
             'init_arguments', 'copy_propagate']:
    reference = re.sub(r'\b' + name + r'\b', 'reference_' + name, reference)
code = Path(__file__).with_suffix('.c').read_text().replace(
    '/* PRODUCTION */', reference + '\n' + parts)
with tempfile.TemporaryDirectory(prefix='tcg-optimizer-copy-') as d:
    folder = Path(d)
    (folder / 'test.c').write_text(code)
    binary = Path(os.environ.get('TCG_COPY_BINARY', folder / 'test'))
    flags = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-parameter', '-D_GNU_SOURCE', '-DHAVE_THREAD_STORAGE']
    for path in ['libretro/gen', 'libretro/glib', '.', 'include', 'tcg/i386',
                 'host/include/generic', 'libretro-common/include']:
        flags += ['-isystem', str(r / path)]
    flags += shlex.split(os.environ.get('TCG_COPY_CFLAGS', ''))
    subprocess.run(cc + flags + [str(folder / 'test.c'), '-o', str(binary)], check=True)
    if not os.environ.get('TCG_COPY_SKIP_RUN'):
        subprocess.run([str(binary)], check=True, timeout=60)
        if os.environ.get('TCG_COPY_NEGATIVE'):
            controls = [
                ('stale generation reused', 'ti->ebb == ctx->ebb', 'true'),
                ('output copy substituted', 'for (i = 0; i < nb_oargs; i++)', 'for (i = 0; i < 0; i++)'),
            ]
            for name, before, after in controls:
                assert before in parts, name
                mutant = code.replace(parts, parts.replace(before, after))
                (folder / 'test.c').write_text(mutant)
                subprocess.run(cc + flags + [str(folder / 'test.c'), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
                assert result.returncode, 'negative control escaped: ' + name
                print('Required negative control rejected:', name)
