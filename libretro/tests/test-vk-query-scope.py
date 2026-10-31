# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production query/pass/cost-model control against a delayed GPU model."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

repo=Path(sys.argv[1]).resolve()
source=(repo/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
parts=[]
for name in ('begin_query','end_query','begin_render_pass','vk_cost_close_recorded_queries',
             'end_render_pass','vk_cost_probe_active','vk_cost_materialize','begin_draw','end_draw'):
    match=re.search(r'^static (?:void|bool) '+name+r'\([^;{]*\)\s*\{',source,re.M);assert match,name
    end=source.index('{',match.start())+1;depth=1
    while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
    parts.append(source[match.start():end])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='vk-query-scope-') as directory:
    folder=Path(directory);(folder/'test.c').write_text(code);exe=folder/('test.exe' if os.name=='nt' else 'test')
    subprocess.run([sys.argv[2] if len(sys.argv)>2 else 'gcc','-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                    '-Wno-unused-parameter','-Wno-sign-compare','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
                    '-I'+str(repo/'libretro/tests'),'-I'+str(repo/'deps/vulkan-headers'),str(folder/'test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=30)
