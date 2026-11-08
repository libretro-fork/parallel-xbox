#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc');l=r/'libretro-common'
with tempfile.TemporaryDirectory(prefix='file-retained-view-') as tmp:
 p=Path(tmp);exe=os.environ.get('FILE_RETAINED_BINARY',str(p/'test'))
 sources=['compat/fopen_utf8.c','string/rstrtod.c','time/rtime.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','encodings/encoding_utf.c','compat/compat_strl.c','compat/compat_posix_string.c','string/stdstring.c']
 flags=[] if os.name=='nt' or 'mingw' in ' '.join(cc) else ['-DHAVE_MMAP']
 flags+=shlex.split(os.environ.get('FILE_RETAINED_CFLAGS','-fsanitize=address,undefined'))
 subprocess.run(cc+['-O2','-g','-std=gnu11','-D_GNU_SOURCE','-ffunction-sections','-fdata-sections','-I'+str(r),'-I'+str(l/'include'),*flags,str(Path(__file__).with_suffix('.c')),str(r/'block/libretro-file.c'),*[str(l/x) for x in sources],'-Wl,--gc-sections','-pthread','-o',exe],check=True)
 image=Path(os.environ.get('FILE_RETAINED_FIXTURE',str(p/'input.raw')));image.write_bytes(bytes(range(256))*16)
 if not os.environ.get('FILE_RETAINED_SKIP_RUN'):subprocess.run([exe,str(image)],check=True,timeout=60)
