# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate production mapped-cache identity parsing and driver fallbacks."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
repo = Path(sys.argv[1]).resolve()
s = Path(os.environ.get('GL_CACHE_STRINGS_SOURCE', repo / 'hw/xbox/nv2a/pgraph/gl/shaders.c')).read_text()
parser = s[s.index('typedef struct FileCursor'):s.index('static bool shader_read_cache_version')]
# Extract a complete production function, not a range starting at an inner
# fallback condition whose indentation can outlive the surrounding block.
a = s.index('static void shader_capture_gl_identity(void)')
b = s.index('\n}', a) + 2
init = s.index('void pgraph_gl_init_shaders')
init_body = s[init:s.index('\n}', init) + 2]
if init_body.count('shader_capture_gl_identity();') != 1:
    raise SystemExit('GL initialization must capture driver identity once per context')
code = Path(__file__).with_suffix('.c').read_text().replace('/* PARSER */', parser).replace('/* IDENTITY */', s[a:b])
with tempfile.TemporaryDirectory(prefix='gl-cache-strings-', dir=repo.parent) as d:
    p = Path(d)
    (p / 'test.c').write_text(code)
    binary = os.environ.get('GL_CACHE_STRINGS_BINARY', str(p / 'test'))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc') +
                   ['-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror'] +
                   shlex.split(os.environ.get('GL_CACHE_STRINGS_CFLAGS', '-fsanitize=address,undefined')) +
                   [str(p / 'test.c'), '-o', binary], check=True)
    if not os.environ.get('GL_CACHE_STRINGS_SKIP_RUN'):
        subprocess.run([binary], check=True, timeout=30)
