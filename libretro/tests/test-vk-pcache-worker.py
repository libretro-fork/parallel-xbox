# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production persistent Vulkan cache worker with a delayed consumer."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
windows='mingw' in cc
with tempfile.TemporaryDirectory(prefix='vk-pcache-worker-') as tmp:
    exe=Path(os.environ.get('VK_PCACHE_WORKER_BINARY',str(Path(tmp)/('test.exe' if windows else 'test'))))
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-DHAVE_THREADS',*shlex.split(os.environ.get('VK_PCACHE_WORKER_CFLAGS','')),'-I'+str(root),'-I'+str(root/'libretro-common/include')]
    source=(root/'hw/xbox/nv2a/pgraph/vk/draw.c').read_text()
    import re
    api=re.search(r'^void pgraph_vk_save_pipeline_cache\(.*?^\}',source,re.M|re.S).group()
    fixture=Path(tmp)/'test.c';fixture.write_text((root/'libretro/tests/test-vk-pcache-worker.c').read_text().replace('/* PRODUCTION */',api))
    files=['libretro-common/rthreads/rthreads.c','libretro-common/rthreads/retro_eventcount.c']
    if windows:
        cmd+=['-Wno-cast-function-type','-Wno-unused-parameter'];files+=['libretro-common/compat/compat_strl.c','libretro-common/features/features_cpu.c','libretro-common/time/rtime.c']
    subprocess.run(cmd+[str(fixture)]+[str(root/f) for f in files]+['-pthread','-o',str(exe)],check=True)
    if not os.environ.get('VK_PCACHE_WORKER_SKIP_RUN'):subprocess.run([str(exe)],check=True,timeout=30)
