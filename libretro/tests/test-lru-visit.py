# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production LRU container against traversal/eviction oracles."""
from pathlib import Path
import os,shlex
import subprocess
import sys
import tempfile

repo = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='lru-visit-') as directory:
    p=Path(directory);(p/'qemu').mkdir()
    source=Path(os.environ.get('LRU_VISIT_SOURCE',repo/'include/qemu/lru.h'))
    (p/'qemu/lru.h').write_bytes(source.read_bytes())
    exe = os.environ.get('LRU_VISIT_BINARY',str(p/('test.exe' if os.name=='nt' else 'test')))
    subprocess.run(shlex.split(sys.argv[2] if len(sys.argv)>2 else 'gcc')+['-O2','-std=gnu11',
                    '-Wall','-Wextra','-Werror','-Wno-unused-parameter']+
                    shlex.split(os.environ.get('LRU_VISIT_CFLAGS','-fsanitize=address,undefined'))+
                    ['-I'+str(p),'-I'+str(repo/'include'),'-I'+str(repo/'libretro-common/include'),
                     str(Path(__file__).with_suffix('.c')),'-o',exe],check=True)
    if not os.environ.get('LRU_VISIT_SKIP_RUN'):subprocess.run([exe],check=True,timeout=30)
