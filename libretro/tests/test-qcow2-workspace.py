# SPDX-License-Identifier: GPL-2.0-or-later
"""Compressed QCOW2 patch workspace, full-cluster copy count and write failures."""
from pathlib import Path
import contextlib,os,shlex,shutil,subprocess,sys,tempfile
cs=int(os.environ.get('QCOW_WORKSPACE_CLUSTER_SIZE','65536'))
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc';base=os.environ.get('QCOW_WORKSPACE_BASELINE')
s=subprocess.check_output(['git','-C',str(r),'show',base+':block/libretro-qcow2.c'],text=True) if base else Path(os.environ.get('QCOW_WORKSPACE_SOURCE',str(r/'block/libretro-qcow2.c'))).read_text()
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */',s)
fixture=os.environ.get('QCOW_WORKSPACE_FIXTURE_DIR')
ctx=contextlib.nullcontext(fixture) if fixture else tempfile.TemporaryDirectory(prefix='qcow-workspace-')
with ctx as tmp:
 p=Path(tmp).resolve();p.mkdir(parents=True,exist_ok=True);(p/'test.c').write_text(code)
 binary=os.environ.get('QCOW_WORKSPACE_BINARY',str(p/'test'));lrc=r/'libretro-common'
 sources=[str(r/'block/libretro-file.c')]+[str(lrc/x) for x in ['encodings/encoding_deflate.c','encodings/encoding_crc32.c','features/features_cpu.c','time/rtime.c','compat/compat_strl.c']]
 cmd=[cc,'-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-cast-function-type','-DLR_FILE_FILESTREAM=0','-I'+str(r),'-I'+str(r/'block'),'-I'+str(lrc/'include')]
 cmd+=['-DTEST_CLUSTER_SIZE='+str(cs)]
 if base:cmd+=['-DWORKSPACE_BASELINE']
 cmd+=shlex.split(os.environ.get('QCOW_WORKSPACE_CFLAGS','-fsanitize=address,undefined'))
 subprocess.run(cmd+[str(p/'test.c'),*sources,'-pthread','-o',binary],check=True)
 if not shutil.which('qemu-img'):raise RuntimeError('qemu-img is required for compressed disposable fixtures')
 raw=bytes((i%cs//17+i%cs%13+i//cs*37)&255 for i in range(cs*2));(p/'original.raw').write_bytes(raw)
 for name in ['success','failure']:
  subprocess.run(['qemu-img','convert','-f','raw','-O','qcow2','-c','-o','cluster_size='+str(cs),str(p/'original.raw'),str(p/(name+'.qcow2'))],check=True)
 if not os.environ.get('QCOW_WORKSPACE_SKIP_RUN'):
  for name,failed in [('success','0'),('failure','1')]:subprocess.run([binary,str(p/(name+'.qcow2')),failed],check=True)
  expected=bytearray(raw);expected[111:184]=b'\x5e'*73;expected[cs+211:cs+302]=bytes(91);(p/'expected.raw').write_bytes(expected)
  subprocess.run(['qemu-img','check',str(p/'success.qcow2')],check=True)
  for name,reference in [('success','expected.raw'),('failure','original.raw')]:subprocess.run(['qemu-img','compare','-f','qcow2','-F','raw',str(p/(name+'.qcow2')),str(p/reference)],check=True)