# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the production DMA mapping function against controlled RAM/GPU ownership.
Usage: python test-dma-map.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'system/physmem.c').read_text()
m=re.search(r'^void \*address_space_map\(',s,re.M);assert m
start=s.index('{',m.start());end=start+1;depth=1
while depth:
    depth+=(s[end]=='{')-(s[end]=='}');end+=1
fixture=Path(__file__).with_suffix('.c').read_text()
code=fixture.replace('/* PRODUCTION MAP */',s[m.start():end])
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='dma-map-',dir=repo.parent) as directory:
    p=Path(directory);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([cc,'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env)
