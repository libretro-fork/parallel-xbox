# SPDX-License-Identifier: GPL-2.0-or-later
"""Count scratch zeroing and check actual QCOW2 writes and failures."""
from pathlib import Path
import contextlib,os,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=Path(os.environ.get('QCOW_ZERO_SOURCE',r/'block/libretro-qcow2.c')).read_text()
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s)
fixture=os.environ.get('QCOW_ZERO_FIXTURE_DIR')
with (contextlib.nullcontext(fixture) if fixture else tempfile.TemporaryDirectory(prefix='qcow-zero-')) as tmp:
 p=Path(tmp).resolve();p.mkdir(parents=True,exist_ok=True);(p/'test.c').write_text(code)
 binary=os.environ.get('QCOW_ZERO_BINARY',str(p/'test'));l=r/'libretro-common'
 flags=shlex.split(os.environ.get('QCOW_ZERO_CFLAGS','-fsanitize=address,undefined'))
 if os.environ.get('QCOW_ZERO_EXPECT_FULL'):flags+=['-DQCOW_ZERO_EXPECT_FULL']
 sources=[str(r/'block/libretro-file.c')]+[str(l/x) for x in ['encodings/encoding_deflate.c','encodings/encoding_crc32.c','features/features_cpu.c','time/rtime.c','compat/compat_strl.c']]
 subprocess.run([cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-cast-function-type','-DLR_FILE_FILESTREAM=0','-I'+str(r),'-I'+str(r/'block'),'-I'+str(l/'include'),*flags,str(p/'test.c'),*sources,'-pthread','-o',binary],check=True)
 cs=int(os.environ.get('QCOW_ZERO_CLUSTER_SIZE','65536'))
 for name in ('success','failure','partial'):subprocess.run(['qemu-img','create','-f','qcow2','-o','cluster_size='+str(cs),str(p/(name+'.qcow2')),str(cs*2)],check=True,stdout=subprocess.DEVNULL)
 if not os.environ.get('QCOW_ZERO_SKIP_RUN'):
  for name,fail in [('success','0'),('failure','1'),('partial','2')]:subprocess.run([binary,str(p/(name+'.qcow2')),fail],check=True,timeout=120)
  subprocess.run(['qemu-img','check',str(p/'success.qcow2')],check=True)
  for name in ('success','failure','partial'):
   expected=bytearray(cs*2)
   if name=='success':expected[cs//4:cs//4+cs//2]=b'\x5e'*(cs//2)
   (p/'expected.raw').write_bytes(expected)
   subprocess.run(['qemu-img','compare','-f','qcow2','-F','raw',str(p/(name+'.qcow2')),str(p/'expected.raw')],check=True)
