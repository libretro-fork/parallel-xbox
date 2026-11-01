# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production APU tick and DMA walker with synchronous CPU-owned GP execution.

Usage: python test-dsp-guest-ram.py CHECKOUT [CC]
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
cc = sys.argv[2] if len(sys.argv) > 2 else 'cc'
apu = (repo / 'hw/xbox/mcpx/apu/apu.c').read_text()
dsp = (repo / 'hw/xbox/mcpx/apu/dsp/gp_ep.c').read_text()
frame = apu[apu.index('static void se_frame('):apu.index('\n/*', apu.index('static void se_frame('))]
walk = dsp[dsp.index('static void scatter_gather_walk('):dsp.index('static void gp_scratch_walk(')]
fixture = Path(__file__).with_suffix('.c').read_text()
post = dsp[dsp.index('void mcpx_apu_dsp_post('):dsp.index('void mcpx_apu_dsp_post_silence(')]
start = dsp.index('int64_t (*mcpx_apu_dsp_mixbins(')
mixbins = dsp[start:dsp.index('\n}', start) + 2]
code = fixture.replace('/* PRODUCTION WALK */', walk).replace('/* PRODUCTION FRAME */', frame).replace('/* PRODUCTION POST */', post).replace('/* PRODUCTION MIXBINS */', mixbins)
env = os.environ.copy()
env['PATH'] = str(Path(cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='dsp-guest-ram-') as directory:
    tmp = Path(directory)
    source = tmp / 'test.c'
    binary = tmp / ('test.exe' if os.name == 'nt' else 'test')
    source.write_text(code)
    subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(source), '-o', str(binary)], env=env, check=True)
    subprocess.run([str(binary)], env=env, check=True, timeout=30)
