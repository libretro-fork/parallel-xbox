# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the production bounded disk writer with a delayed consumer."""
from pathlib import Path
import os,shlex,subprocess,sys,tempfile
root=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
windows=os.name=='nt' or 'mingw' in cc
with tempfile.TemporaryDirectory(prefix='gl-disk-writer-') as tmp:
    exe=Path(os.environ.get('GL_DISK_WRITER_BINARY',str(Path(tmp)/('test.exe' if windows else 'test'))))
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-DHAVE_THREADS',*shlex.split(os.environ.get('GL_DISK_WRITER_CFLAGS','')),'-I'+str(root),'-I'+str(root/'libretro-common/include')]
    files=['libretro/tests/test-gl-disk-writer.c','libretro-common/rthreads/rthreads.c','libretro-common/rthreads/retro_eventcount.c']
    if windows:
        cmd+=['-Wno-cast-function-type','-Wno-unused-parameter'];files+=['libretro-common/compat/compat_strl.c','libretro-common/features/features_cpu.c','libretro-common/time/rtime.c']
    subprocess.run(cmd+[str(root/f) for f in files]+['-pthread','-o',str(exe)],check=True)
    if not os.environ.get('GL_DISK_WRITER_SKIP_RUN'):
        subprocess.run([str(exe)],check=True,timeout=30)
        header=root/'hw/xbox/nv2a/pgraph/gl/disk-writer.h'
        original=header.read_text()
        override=Path(tmp)/'hw/xbox/nv2a/pgraph/gl/disk-writer.h'
        override.parent.mkdir(parents=True)
        for label,old,new in [
            ('metadata alias','w->jobs+i*stride','w->jobs'),
            ('metadata lost on reuse','slot->write(slot);','slot->write(slot);slot->data=NULL;'),
        ]:
            assert original.count(old)==1,label
            override.write_text(original.replace(old,new,1))
            bad=Path(tmp)/('control.exe' if windows else 'control')
            subprocess.run(cmd[:1]+['-I'+tmp]+cmd[1:]+[str(root/f) for f in files]+['-pthread','-o',str(bad)],check=True)
            result=subprocess.run([str(bad)],capture_output=True,timeout=30)
            assert result.returncode,label
            print('Required disk-metadata control rejected:',label)
