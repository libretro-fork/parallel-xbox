#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual GL prepared-support build policy and honest historical routing.

Compile only: no GPU, native driver, changed production source or runtime work.
Historical absence is explicitly historical; partial advertised coverage fails.
Compile availability is distinct from the restart-only runtime option default.
"""
from pathlib import Path
import argparse, os, re, shutil, subprocess, tempfile

CONFIG = Path('hw/xbox/nv2a/pgraph/gl/ready-config.h')
RENDERER = Path('hw/xbox/nv2a/pgraph/gl/renderer.h')

def uncomment(source):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)

def capability(root):
    header = root / CONFIG
    renderer = root / RENDERER
    source = uncomment(renderer.read_text()) if renderer.is_file() else ''
    included = re.search(r'^\s*#\s*include\s*[<"](?:[^>"\n]*/)?ready-config\.h[>"]', source, re.M)
    advertised = included or 'PGRAPH_GL_READY_DEFAULT' in source
    if not header.is_file():
        if advertised:
            raise ValueError('Advertised ready default has no actual configuration header')
        return False
    if not included:
        raise ValueError('Ready configuration header is not consumed by renderer.h')
    consumed = re.search(r'^\s*#\s*(?:if|ifdef|ifndef)\b[^\n]*XEMU_EXPERIMENTAL_READY_', source, re.M)
    if consumed and consumed.start() < included.start():
        raise ValueError('Renderer consumes ready flags before loading their configuration')
    return True

def routing_controls(directory):
    cases = [
        ('historical empty', None, None, False),
        ('historical renderer', 'struct Legacy {};\n', None, False),
        ('comment only', '// #include "ready-config.h"\n', None, False),
        ('quoted missing', '#include "ready-config.h"\n', None, 'error'),
        ('angle missing', '#include <ready-config.h>\n', None, 'error'),
        ('advertised missing', '#define PGRAPH_GL_READY_DEFAULT 1\n', None, 'error'),
        ('unconsumed header', 'struct Legacy {};\n', '/* header */\n', 'error'),
        ('missing renderer', None, '/* header */\n', 'error'),
        ('complete', '#include "ready-config.h"\n', '/* header */\n', True),
        ('late include', '#ifdef XEMU_EXPERIMENTAL_READY_VSH\n#endif\n#include "ready-config.h"\n', '/* header */\n', 'error'),
    ]
    for number, (name, renderer, config, expected) in enumerate(cases):
        root = directory / str(number)
        (root / CONFIG).parent.mkdir(parents=True)
        if renderer is not None:
            (root / RENDERER).write_text(renderer)
        if config is not None:
            (root / CONFIG).write_text(config)
        try:
            result = capability(root)
        except ValueError:
            result = 'error'
        assert result == expected, (name, result, expected)
    print('PASS ready-default capability: 10 complete/historical/partial/ordering cases', flush=True)

# Presence is intentional: legacy renderer guards use #ifdef, including value 0.
VARIANTS = [
    ('implicit', [], (1, True, True)),
    ('legacy-vsh', ['XEMU_EXPERIMENTAL_READY_VSH=1'], (1, True, True)),
    ('legacy-ff', ['XEMU_EXPERIMENTAL_READY_FF=1'], (1, True, True)),
    ('legacy-both', ['XEMU_EXPERIMENTAL_READY_VSH=1', 'XEMU_EXPERIMENTAL_READY_FF=1'], (1, True, True)),
    ('zero-valued-legacy', ['XEMU_EXPERIMENTAL_READY_VSH=0', 'XEMU_EXPERIMENTAL_READY_FF=0'], (1, True, True)),
    ('reference-optout', ['XEMU_DISABLE_READY_GL=1'], (0, False, False)),
    ('zero-valued-optout', ['XEMU_DISABLE_READY_GL=0'], (0, False, False)),
    ('reference-vsh-only', ['XEMU_DISABLE_READY_GL=1', 'XEMU_EXPERIMENTAL_READY_VSH=1'], (0, True, False)),
    ('reference-vsh-zero', ['XEMU_DISABLE_READY_GL=1', 'XEMU_EXPERIMENTAL_READY_VSH=0'], (0, True, False)),
    ('reference-explicit-pair', ['XEMU_DISABLE_READY_GL=1', 'XEMU_EXPERIMENTAL_READY_VSH=1', 'XEMU_EXPERIMENTAL_READY_FF=1'], (0, True, True)),
    ('unpaired-ff', ['XEMU_DISABLE_READY_GL=1', 'XEMU_EXPERIMENTAL_READY_FF=1'], None),
    ('unpaired-ff-zero', ['XEMU_DISABLE_READY_GL=1', 'XEMU_EXPERIMENTAL_READY_FF=0'], None),
]

def compile_variants(directory, cc, env, header):
    directory.mkdir()
    config = directory / 'ready-config.h'
    config.write_text(header)
    source = directory / 'test.c'
    for name, defines, expected in VARIANTS:
        body = '#include "ready-config.h"\n#include "ready-config.h"\n'
        if expected is not None:
            default, vsh, ff = expected
            body += '#if !defined(PGRAPH_GL_READY_DEFAULT) || PGRAPH_GL_READY_DEFAULT != %d\n#error READY_DEFAULT_INVARIANT\n#endif\n' % default
            for symbol, enabled in [('XEMU_EXPERIMENTAL_READY_VSH', vsh), ('XEMU_EXPERIMENTAL_READY_FF', ff)]:
                body += '#if %sdefined(%s)\n#error READY_FLAG_INVARIANT_%s\n#endif\n' % ('!' if enabled else '', symbol, symbol)
        body += 'int ready_default_contract(void);\nint ready_default_contract(void) { return 0; }\n'
        source.write_text(body)
        command = [cc, '-std=c11', '-pedantic', '-Wall', '-Wextra', '-Werror', '-c', str(source), '-o', str(directory / 'test.o')]
        result = subprocess.run(command + ['-D' + item for item in defines], env=env, capture_output=True, text=True, timeout=30)
        if expected is None:
            if result.returncode == 0 or 'XEMU_EXPERIMENTAL_READY_FF requires XEMU_EXPERIMENTAL_READY_VSH' not in result.stderr:
                return name, result.returncode, result.stderr
        elif result.returncode:
            return name, result.returncode, result.stderr
    return None

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('cc', nargs='?', default='cc')
    args = parser.parse_args()
    root = args.root.resolve()
    env = dict(os.environ)
    env['PATH'] = str(Path(shutil.which(args.cc) or args.cc).resolve().parent) + os.pathsep + env.get('PATH', '')
    with tempfile.TemporaryDirectory(prefix='gl-ready-default-') as tmp:
        directory = Path(tmp)
        routing_controls(directory / 'routing')
        if not capability(root):
            print('HISTORICAL: no ready-default implementation; not qualified as a default-enabled revision', flush=True)
            return
        header = (root / CONFIG).read_text()
        failure = compile_variants(directory / 'actual', args.cc, env, header)
        assert failure is None, ('Actual ready-default policy failed', failure)
        print('PASS actual ready-support build policy: 12 implicit/legacy/optout/dependency variants; repeated inclusion; strict C11 compiler', flush=True)
        mutations = [
            ('lost implicit VSH', '#define XEMU_EXPERIMENTAL_READY_VSH 1', '/* lost VSH */'),
            ('lost implicit FF', '#define XEMU_EXPERIMENTAL_READY_FF 1', '/* lost FF */'),
            ('ignored reference optout', '#ifndef XEMU_DISABLE_READY_GL', '#if 1'),
            ('wrong default capability', '#define PGRAPH_GL_READY_DEFAULT 1', '#define PGRAPH_GL_READY_DEFAULT 0'),
            ('unpaired FF accepted', '#error XEMU_EXPERIMENTAL_READY_FF requires XEMU_EXPERIMENTAL_READY_VSH', '/* lost dependency rejection */'),
        ]
        for index, (name, old, new) in enumerate(mutations):
            assert header.count(old) == 1, name
            failure = compile_variants(directory / ('fault-' + str(index)), args.cc, env, header.replace(old, new, 1))
            assert failure is not None, ('Incorrect ready-default policy passed', name)
            assert failure[1] == 0 or 'READY_' in failure[2] or 'XEMU_EXPERIMENTAL_READY_FF requires XEMU_EXPERIMENTAL_READY_VSH' in failure[2], (name, failure)
            print('Required ready-default fault rejected:', name, 'at', failure[0], flush=True)
if __name__ == '__main__':
    main()
