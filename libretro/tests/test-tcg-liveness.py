#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare production reachability/liveness with the frozen pre-port passes."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
if os.name != 'nt' and os.environ.get('TCG_LIVENESS_NEGATIVE'):
    import resource
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
cc = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
source = (r / 'tcg/tcg.c').read_text()


def function(text, key):
    start = text.index(key)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def passes(text):
    start = text.index('static void move_label_uses(')
    end_fn = function(text, 'liveness_pass_1(TCGContext *s)')
    end = text.index(end_fn) + len(end_fn)
    return text[start:end]


reference = Path(__file__).with_name('tcg-liveness-reference.h').read_text()
names = ['move_label_uses', 'reachable_code_pass', 'liveness_pass_0',
         'la_temp_pref', 'la_reset_pref', 'la_func_end', 'la_bb_end',
         'la_global_sync', 'la_bb_sync', 'la_global_kill', 'la_cross_call',
         'assert_carry_dead', 'liveness_pass_1']
for name in names:
    reference = re.sub(r'\b' + name + r'\b', 'reference_' + name, reference)
support = '\n'.join(function(source, key) for key in
                    ['static void remove_label_use(', 'void tcg_op_remove('])
candidate = passes(source)
# Test-only work counts; the release compiler contains no counters.
for key, counter in [('static void reference_la_cross_call(', 'reference_visits')]:
    body = function(reference, key)
    reference = reference.replace(body, body.replace('TCGTemp *ts =', '++' + counter + ';\n            TCGTemp *ts =', 1))
body = function(candidate, 'static void la_cross_call(')
candidate = candidate.replace(body, body.replace('TCGTemp *ts =', '++candidate_visits;\n            TCGTemp *ts =', 1))
code = Path(__file__).with_suffix('.c').read_text().replace(
    '/* PRODUCTION */', support + '\n' + reference + '\n' + candidate)
with tempfile.TemporaryDirectory(prefix='tcg-liveness-') as directory:
    folder = Path(directory)
    (folder / 'test.c').write_text(code)
    binary = Path(os.environ.get('TCG_LIVENESS_BINARY', folder / 'test'))
    flags = ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             '-Wno-unused-parameter', '-Wno-unused-function', '-Wno-sign-compare',
             '-D_GNU_SOURCE', '-DHAVE_THREAD_STORAGE']
    for include in ['libretro/gen', 'libretro/glib', '.', 'include',
                    'tcg/i386', 'host/include/generic', 'libretro-common/include']:
        flags += ['-isystem', str(r / include)]
    flags += shlex.split(os.environ.get('TCG_LIVENESS_CFLAGS', ''))
    subprocess.run(cc + flags + [str(folder / 'test.c'), '-o', str(binary)], check=True)
    if not os.environ.get('TCG_LIVENESS_SKIP_RUN'):
        subprocess.run([str(binary)], check=True, timeout=60)
        if os.environ.get('TCG_LIVENESS_NEGATIVE'):
            controls = [
                ('lost live temp', 'set_bit(temp_idx_context(s, ts), ls->live.l);', '(void)s;'),
                ('dead temp stays live', 'clear_bit(i, ls->live.l);', '(void)i;'),
                ('wrong EBB after a kept label', 'ebb = op;', 'ebb = QTAILQ_FIRST(&s->ops);'),
            ]
            for name, before, after in controls:
                assert before in candidate, name
                mutant = code.replace(candidate, candidate.replace(before, after))
                (folder / 'test.c').write_text(mutant)
                subprocess.run(cc + flags + [str(folder / 'test.c'), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
                assert result.returncode, 'negative control escaped: ' + name
                print('Required negative control rejected:', name)
