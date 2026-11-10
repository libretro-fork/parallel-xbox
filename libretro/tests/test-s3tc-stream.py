# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare bounded direct-destination S3TC tiles with whole-level decoding."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
with tempfile.TemporaryDirectory(prefix='s3tc-stream-') as tmp:
    exe = Path(tmp) / ('test.exe' if os.name == 'nt' else 'test')
    cmd = [cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
           '-I' + str(root), str(Path(__file__).with_suffix('.c')),
           str(root / 'hw/xbox/nv2a/pgraph/s3tc.c'), '-o', str(exe)]
    env = dict(os.environ)
    env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
    subprocess.run(cmd, check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env, timeout=60)
