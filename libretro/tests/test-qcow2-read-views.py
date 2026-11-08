#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Retained decoded payloads through real IDE completion/cancellation boundaries."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=shlex.split(sys.argv[2] if len(sys.argv)>2 else 'cc')
def function(source,key):
 a=source.index(key);b=source.index('{',a)+1;depth=1
 while depth:depth+=(source[b]=='{')-(source[b]=='}');b+=1
 return source[a:b]
s=(r/'hw/ide/core.c').read_text();h=(r/'hw/ide/ide-internal.h').read_text();b=(r/'block/libretro-disk.c').read_text();q=(r/'include/qemu/iov.h').read_text()
req=re.search(r'typedef struct IDEBufferedRequest \{.*?\} IDEBufferedRequest;',h,re.S)[0]
iov=q[q.index('typedef struct QEMUIOVector'):q.index('void qemu_iovec_init(QEMUIOVector')]
# Unbuffered DMA abandonment has its own state-machine fixture. Do not pull
# that adjacent helper into this buffered-request/retained-payload model.
a=s.index('/* One completed request per owner');z=s.index('static bool ide_abandon_dma_view(',a)
code=Path(__file__).with_suffix('.c').read_text().replace('/* REQUEST */',req).replace('/* QIOV */',iov)
code=code.replace('/* BLOCK */',function(b,'void lr_disk_release_read_view(')+'\n'+function(b,'BlockAIOCB *lr_disk_aio_read_view('))
code=code.replace('/* IDE */',s[a:z]+'\n'+function(s,'void ide_cancel_dma_sync('))
with tempfile.TemporaryDirectory(prefix='qcow-read-views-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 exe=os.environ.get('QCOW_READ_VIEW_BINARY',str(p/'test'));l=r/'libretro-common'
 flags=shlex.split(os.environ.get('QCOW_READ_VIEW_CFLAGS','-fsanitize=address,undefined'))
 sources=[str(l/x) for x in ['encodings/encoding_deflate.c','encodings/encoding_crc32.c','features/features_cpu.c','time/rtime.c','compat/compat_strl.c']]
 command=cc+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-cast-function-type','-D_GNU_SOURCE','-DLR_FILE_FILESTREAM=0','-I'+str(r),'-I'+str(r/'block'),'-I'+str(r/'include'),'-I'+str(l/'include'),*flags,str(p/'test.c'),*sources,'-pthread','-o',exe]
 subprocess.run(command,check=True)
 cs=int(os.environ.get('QCOW_READ_VIEW_CLUSTER_SIZE','65536'))
 raw=bytes((i%cs//17+i%cs%13+i//cs*37)&255 for i in range(cs*2));(p/'input.raw').write_bytes(raw)
 subprocess.run(['qemu-img','convert','-f','raw','-O','qcow2','-c','-o','cluster_size='+str(cs),str(p/'input.raw'),str(p/'input.qcow2')],check=True)
 if os.environ.get('QCOW_READ_VIEW_FIXTURE'):
  import shutil
  shutil.copyfile(p/'input.qcow2',os.environ['QCOW_READ_VIEW_FIXTURE'])
 if not os.environ.get('QCOW_READ_VIEW_SKIP_RUN'):
  import shutil
  cases=[(0,cs//2),(cs//2,cs//2),(cs//4,cs//2),(1,cs-2),(117,13)]
  for off,length in cases:
   for zero in (0,1):
    for failure in (0,1):
     image=p/'patch.qcow2';shutil.copyfile(p/'input.qcow2',image)
     subprocess.run([exe,str(image),str(off),str(length),str(zero),str(failure)],check=True,timeout=60)
     if not failure:subprocess.run(['qemu-img','check',str(image)],check=True,stdout=subprocess.DEVNULL)
  subprocess.run([exe,str(p/'input.qcow2')],check=True,timeout=60)
  subprocess.run(['qemu-img','check',str(p/'input.qcow2')],check=True)
  for label,old,new in [
   ('retained owner leak','lr_disk_release_read_view(owner);','(void)owner;'),
   ('canceled payload publication','if (req->orphaned) {','if (false) {')]:
   assert code.count(old)==1,label
   (p/'test.c').write_text(code.replace(old,new,1))
   negative=str(p/('negative-test.exe' if os.name=='nt' else 'negative-test'))
   subprocess.run(command[:-1]+[negative],check=True)
   image=p/'negative.qcow2'
   subprocess.run(['qemu-img','convert','-f','raw','-O','qcow2','-c','-o','cluster_size='+str(cs),str(p/'input.raw'),str(image)],check=True)
   failed=subprocess.run([negative,str(image)],capture_output=True,timeout=60)
   assert failed.returncode and b'Assertion' in failed.stderr,label
   print('Required negative control rejected:',label)
