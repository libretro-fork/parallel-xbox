# SPDX-License-Identifier: GPL-2.0-or-later
"""Production CHD worker lifetime, publication, queue pressure and byte oracles."""
from pathlib import Path
import importlib.util, os, shlex, subprocess, sys, tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
windows='mingw' in cc
spec=importlib.util.spec_from_file_location('fixtures',Path(__file__).with_name('test-chd-codecs.py'))
fixtures=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixtures)
with tempfile.TemporaryDirectory(prefix='chd-worker-') as tmp:
    work=Path(os.environ.get('CHD_WORKER_WORK',tmp)).resolve();work.mkdir(parents=True,exist_ok=True)
    fixtures.generate(work,trim=0)
    files=['block/libretro-chd.c','libretro-common/formats/chd/rchd.c',
           'libretro-common/formats/flac/rflac.c','libretro-common/formats/7z/r7z_lzma.c',
           *['libretro-common/encodings/encoding_'+n+'.c' for n in
             ['huffman','rzstd','xxh64','deflate','crc32']],
           'libretro-common/features/features_cpu.c','libretro-common/rthreads/rthreads.c',
           'libretro-common/rthreads/retro_eventcount.c','libretro-common/queues/retro_spsc.c']
    if windows:files+=['libretro-common/time/rtime.c','libretro-common/compat/compat_strl.c']
    binary=os.environ.get('CHD_WORKER_BINARY',str(work/'test'))
    flags=shlex.split(os.environ.get('CHD_WORKER_CFLAGS',
                     '-static' if windows else '-fsanitize=address,undefined'))
    subprocess.run([cc,'-O2','-g','-std=gnu11','-Wall','-Wextra',*flags,
                    '-Wno-unused-parameter','-Wno-cast-function-type','-DHAVE_THREADS',
                    '-DHAVE_RCHD_DEFLATE','-DHAVE_RCHD_LZMA','-DHAVE_RCHD_FLAC','-DHAVE_RCHD_ZSTD',
                    '-I'+str(root),'-I'+str(root/'libretro-common/include'),
                    str(root/'libretro/tests/test-chd-worker.c'),*[str(root/f) for f in files],
                    '-pthread','-lm','-o',binary],check=True)
    if not os.environ.get('CHD_WORKER_SKIP_RUN'):
        subprocess.run([binary,str(work)],check=True,timeout=60)
