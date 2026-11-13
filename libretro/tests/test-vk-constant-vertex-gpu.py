#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare typed zero-stride fetch with legacy packed UBO/push constants."""
from pathlib import Path
import os, re, subprocess, sys

r = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
test = Path(__file__).with_name('test-spv-vsh-gpu.py')
env = dict(os.environ)
env['VSH_CONSTANT_VERTEX_INPUT'] = '1'
env.pop('VSH_CONSTANT_VERTEX_BAD_STRIDE', None)
subprocess.run([sys.executable, str(test), str(r), cc], check=True, env=env)

# A real fetch error must change values, without relying on invalid memory or
# validation errors. This range consumes the constant position/weight inputs.
env['VSH_CONSTANT_VERTEX_BAD_STRIDE'] = '1'
env['SPV_VSH_GPU_RANGE'] = '224:240'
p = subprocess.run([sys.executable, str(test), str(r), cc],
                   env=env, text=True, stdout=subprocess.PIPE,
                   stderr=subprocess.STDOUT, timeout=360)
print(p.stdout, end='')
results = re.findall(r'Direct VSH Vulkan: \d+ scalar results, (\d+) differences, '
                     r'(\d+) validation errors; validation=enabled', p.stdout)
assert p.returncode and results, 'Wrong-stride control did not execute/reject'
assert any(int(d) for d, _ in results), 'Wrong stride preserved every value'
assert all(int(e) == 0 for _, e in results), 'Negative control used invalid Vulkan'
print('Required constant-vertex GPU control rejected: advancing a constant row')
