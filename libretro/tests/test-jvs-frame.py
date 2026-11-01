# SPDX-License-Identifier: GPL-2.0-or-later
"""Check production JVS response framing against the original full-frame path."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile

r = Path(sys.argv[1]).resolve()
s = (r / 'hw/xbox/chihiro/chihiro-jvs.c').read_text()
a = s.index('    /* Build framed response:')
b = s.index('\n}', a) + 2
code = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', s[a:b])
with tempfile.TemporaryDirectory(prefix='jvs-frame-') as tmp:
    p = Path(tmp)
    (p / 'test.c').write_text(code)
    binary = os.environ.get('JVS_FRAME_BINARY', str(p / 'test'))
    flags = shlex.split(os.environ.get('JVS_FRAME_CFLAGS', '-fsanitize=address,undefined'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc') +
                   ['-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror'] + flags +
                   [str(p / 'test.c'), '-o', binary], check=True)
    if not os.environ.get('JVS_FRAME_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
