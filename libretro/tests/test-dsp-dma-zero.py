# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the complete production DMA module against bounded DSP/host memory."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile
root = Path(sys.argv[1]).resolve()
source = (root / 'hw/xbox/mcpx/apu/dsp/dsp_dma.c').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
fixture = Path(__file__).with_suffix('.c').read_text()
compiler = shlex.split(sys.argv[2] if len(sys.argv) > 2 else 'cc')
flags = ['-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(root)]
flags += shlex.split(os.environ.get('DSP_DMA_ZERO_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
runner = shlex.split(os.environ.get('DSP_DMA_ZERO_RUNNER',''))
mutations = {
 'missing-zero': ('dsp_offset >= 0x4000 && dsp_offset < 0x8000', 'false'),
 'backing-map': ('if (!zero_aperture) {', 'if (true) {'),
 'wrong-fill': ('memset(host, 0, bytes);','memset(host, 0xA5, bytes);'),
 'broad-range': ('dsp_offset >= 0x4000 && dsp_offset < 0x8000','dsp_offset >= 0x4000'),
}
with tempfile.TemporaryDirectory(prefix='dsp-dma-zero-') as temporary:
 path=Path(temporary)
 for name in ('production',*mutations):
  production=source
  if name in mutations:
   old,new=mutations[name]; assert production.count(old)==1; production=production.replace(old,new,1)
  c=path/'test.c'; exe=path/'test.exe'; c.write_text(fixture.replace('/* PRODUCTION */',production))
  subprocess.run(compiler+flags+[str(c),'-o',str(exe)],check=True)
  result=subprocess.run(runner+[str(exe)],capture_output=True,timeout=30)
  if name=='production':
   sys.stdout.buffer.write(result.stdout)
   if result.returncode: sys.stderr.buffer.write(result.stderr)
   result.check_returncode()
  else:
   assert result.returncode and b'DMA zero:' in result.stderr,(name,result.stderr)
   print('Negative control rejected:',name)
