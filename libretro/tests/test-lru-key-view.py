#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check borrowed LRU keys and unchanged ordinary-lookup GCC assembly.

Usage: python libretro/tests/test-lru-key-view.py REPO [CC]
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'gcc'
tests = Path(__file__).resolve().parent
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='lru-key-view-') as tmp:
    p = Path(tmp)
    (p / 'qemu').mkdir()
    (p / 'qemu/osdep.h').write_bytes(b'#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
    (p / 'qemu/mstring.h').write_bytes(b'typedef struct MString MString;\n')
    flags = [cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
             '-I' + str(p), '-I' + str(repo), '-I' + str(repo / 'include'),
             '-I' + str(repo / 'libretro-common/include'), '-I' + str(tests)]
    exe = p / ('test.exe' if os.name == 'nt' else 'test')
    for checked in (True, False):
        extra = ['-fsanitize=undefined', '-fsanitize-undefined-trap-on-error'] if checked else []
        subprocess.run([*flags, *extra, str(tests / 'test-lru-key-view.c'), '-o', str(exe)], env=env, check=True)
        subprocess.run([str(exe)], env=env, check=True, timeout=120)
    # Identical TU and symbol names make a byte comparison possible. The
    # reference contains the previous ordinary lookup body, frozen verbatim.
    source = p / 'probe.c'
    source.write_bytes(b'''#include <stdbool.h>
#include <stddef.h>
#include "qemu/lru.h"
#include "lru-key-view-reference.h"
LruNode *lookup_probe(Lru *lru, uint64_t hash, const void *key) {
#ifdef REFERENCE
    return reference_lookup(lru, hash, key);
#else
    return lru_lookup(lru, hash, key);
#endif
}
''')
    assemblies = []
    for reference in (True, False):
        asm = p / ('reference.s' if reference else 'current.s')
        subprocess.run([*flags, *(['-DREFERENCE'] if reference else []),
                        '-DNDEBUG', '-S', str(source), '-o', str(asm)], env=env, check=True)
        assemblies.append(asm.read_bytes())
    if os.environ.get('LRU_VIEW_ASM'):
        for name, data in zip(('reference', 'current'), assemblies):
            Path(os.environ['LRU_VIEW_ASM'] + '-' + name + '.s').write_bytes(data)
    assert assemblies[0] == assemblies[1], 'ordinary lookup assembly changed'
    print('Ordinary lookup: GCC -O2 -DNDEBUG assembly byte-identical to frozen baseline')
