# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production x86 block-access pointer bounds and slow-path selection."""
from pathlib import Path
import os, re, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
s = Path(os.environ.get('X86_ACCESS_PTR_SOURCE', r / 'target/i386/tcg/access.c')).read_text()
a = s.index('static void *access_ptr(')
h = (r / 'target/i386/tcg/access.h').read_text()
types = re.search(r'typedef struct X86Access \{.*?\} X86Access;', h, re.S).group()
code = Path(__file__).with_suffix('.c').read_text().replace('/* TYPES */', types).replace('/* PRODUCTION */', s[a:])
with tempfile.TemporaryDirectory(prefix='x86-access-ptr-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(code)
    binary = os.environ.get('X86_ACCESS_PTR_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('X86_ACCESS_PTR_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc') +
                   ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror'] + flags +
                   [str(p / 'test.c'), '-o', binary], check=True)
    if not os.environ.get('X86_ACCESS_PTR_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
