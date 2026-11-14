#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check frontend dispatch without an installed Vulkan loader or GPU."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
env = os.environ.copy()
env['PATH'] = str(Path(shutil.which(cc) or cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='vk-dispatch-') as directory:
    exe = Path(directory) / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(repo / 'libretro'),
                    '-I' + str(repo / 'deps/vulkan-headers'),
                    str(repo / 'libretro/tests/test-vk-dispatch.c'),
                    str(repo / 'libretro/vk_dispatch.c'), '-o', str(exe)], check=True, env=env)
    subprocess.run([str(exe)], check=True, timeout=30, env=env)
