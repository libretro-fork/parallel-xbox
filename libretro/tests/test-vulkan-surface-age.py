# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify Vulkan surface expiry/use clocks and OpenGL source-use stamps. No GPU required."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
repo=Path(sys.argv[1]).resolve()
parts=[]
for api,file,name in [('vk','surface.c','expire_old_surfaces'),
                      ('vk','texture.c','update_timestamps'),
                      ('gl','texture.c','touch_texture_sources')]:
    source=(repo/'hw/xbox/nv2a/pgraph'/api/file).read_text()
    if file=='surface.c':
        parts.append(next(line for line in source.splitlines() if line.startswith('const int max_surface_frame_time_delta =')))
    start=source.index('static void '+name+'(')
    end=source.index('{',start)+1
    depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}')
        end+=1
    parts.append(source[start:end])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',
    '\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='vk-surface-age-') as folder:
    folder=Path(folder)
    (folder/'test.c').write_text(code)
    exe=folder/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc','-O2','-std=gnu11',
        '-Wall','-Wextra','-Werror','-Wno-sign-compare','-fsanitize=undefined',
        '-fsanitize-undefined-trap-on-error',str(folder/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
