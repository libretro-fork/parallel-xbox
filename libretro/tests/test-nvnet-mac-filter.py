# SPDX-License-Identifier: GPL-2.0-or-later
"""Production NVNet filter against a byte-wise MAC and trace oracle."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=Path(os.environ.get('NVNET_MAC_SOURCE',r/'hw/xbox/mcpx/nvnet/nvnet.c')).read_text()
a=s.index('static bool receive_filter(');b=s.index('\n}',a)+2
code=Path(__file__).with_suffix('.c').read_text().replace('/* FILTER */',s[a:b])
with tempfile.TemporaryDirectory(prefix='nvnet-mac-') as directory:
 p=Path(directory);c=p/'test.c';c.write_text(code);exe=Path(os.environ.get('NVNET_MAC_BINARY',str(p/'test')))
 subprocess.run([cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-I'+str(r),str(c),
 *shlex.split(os.environ.get('NVNET_MAC_CFLAGS','-fsanitize=address,undefined -fsanitize-undefined-trap-on-error')),'-o',str(exe)],check=True)
 if not os.environ.get('NVNET_MAC_SKIP_RUN'):subprocess.run([str(exe)],check=True,timeout=30)
