# SPDX-License-Identifier: GPL-2.0-or-later
"""Decode converted attributes with real Vulkan vertex fetch and sync validation."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='vulkan-vertex-formats-') as directory:
    exe = Path(directory) / ('test.exe' if os.name == 'nt' else 'test')
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc', '-O2', '-std=gnu11',
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-I' + str(repo), str(Path(__file__).with_suffix('.c')),
                    '-lvulkan', '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
