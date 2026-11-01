# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production scalar instruction fetches and page/MMIO fallbacks."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
s = (r / 'accel/tcg/translator.c').read_text()
def function(name):
    m = re.search(r'^(?:static )?(?:bool|uint8_t|uint16_t|uint32_t) ' + name + r'\(', s, re.M)
    assert m
    a = m.start()
    b = s.index('{', a) + 1
    depth = 1
    while depth:
        depth += (s[b] == '{') - (s[b] == '}')
        b += 1
    return s[a:b]
loader = function('translator_ld').replace('translator_ld(', 'translator_ld_impl(', 1)
byte = '\n'.join(function(n) for n in ('translator_ldub', 'translator_lduw_end', 'translator_ldl_end'))
code = Path(__file__).with_suffix('.c').read_text().replace('/* LOADER */', loader).replace('/* BYTE */', byte)
with tempfile.TemporaryDirectory(prefix='opcode-byte-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(code)
    binary = os.environ.get('OPCODE_BYTE_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('OPCODE_BYTE_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc') +
                   ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare'] + flags +
                   [str(p / 'test.c'), '-o', binary], check=True)
    if not os.environ.get('OPCODE_BYTE_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
