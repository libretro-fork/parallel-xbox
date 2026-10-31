# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production DMA mapping function against controlled RAM/GPU ownership.
Usage: python test-dma-map.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'system/physmem.c').read_text()
start=s.index('static void *address_space_map_internal(')
end=s.index('/* Unmaps a memory region',start)
fixture=Path(__file__).with_suffix('.c').read_text()
code=fixture.replace('/* PRODUCTION MAP */',s[start:end])
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='dma-map-',dir=repo.parent) as directory:
    p=Path(directory);(p/'test.c').write_text(code);exe=Path(os.environ.get('DMA_MAP_BINARY',str(p/('test.exe' if os.name=='nt' else 'test'))))
    flags=shlex.split(os.environ.get('DMA_MAP_CFLAGS','-fsanitize=undefined -fsanitize-undefined-trap-on-error'))
    subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter',*flags,'-I',str(repo/'libretro-common/include'),str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    if not os.environ.get('DMA_MAP_SKIP_RUN'):
        subprocess.run([str(exe)],check=True,env=env)
