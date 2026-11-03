# SPDX-License-Identifier: GPL-2.0-or-later
"""Mapped/fallback compressed-input correctness fixture; no timing mode."""
from pathlib import Path
import contextlib,os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
fixture=os.environ.get('QCOW_INPUT_FIXTURE_DIR')
with (contextlib.nullcontext(fixture) if fixture else tempfile.TemporaryDirectory(prefix='qcow-input-')) as tmp:
 p=Path(tmp).resolve();p.mkdir(parents=True,exist_ok=True)
 binary=os.environ.get('QCOW_INPUT_BINARY',str(p/'test'));l=r/'libretro-common'
 flags=shlex.split(os.environ.get('QCOW_INPUT_CFLAGS','-fsanitize=address,undefined'))
 if os.environ.get('QCOW_INPUT_SOURCE'):flags+=['-DQCOW_INPUT_SOURCE="'+str(Path(os.environ['QCOW_INPUT_SOURCE']).resolve()).replace('\\','/')+'"']
 sources=[str(l/x) for x in ['encodings/encoding_deflate.c','encodings/encoding_crc32.c','features/features_cpu.c','time/rtime.c','compat/compat_strl.c']]
 subprocess.run([cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-cast-function-type','-D_GNU_SOURCE','-DLR_FILE_FILESTREAM=0','-I'+str(r),'-I'+str(r/'block'),'-I'+str(l/'include'),*flags,str(Path(__file__).with_suffix('.c')),*sources,'-pthread','-o',binary],check=True)
 cs=int(os.environ.get('QCOW_INPUT_CLUSTER_SIZE','65536'))
 raw=bytes((i%cs//17+i%cs%13+i//cs*37)&255 for i in range(cs*2));(p/'input.raw').write_bytes(raw)
 subprocess.run(['qemu-img','convert','-f','raw','-O','qcow2','-c','-o','cluster_size='+str(cs),str(p/'input.raw'),str(p/'input.qcow2')],check=True)
 if not os.environ.get('QCOW_INPUT_SKIP_RUN'):subprocess.run([binary,str(p/'input.qcow2')],check=True,timeout=120)
