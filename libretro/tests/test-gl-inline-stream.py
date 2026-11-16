# SPDX-License-Identifier: LGPL-2.1-or-later
"""Execute mapped-stream ownership schedules and injected unsafe reuse faults."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import time

root=Path(sys.argv[1]).resolve()
compiler=sys.argv[2] if len(sys.argv)>2 else 'cc'
production=(root/'hw/xbox/nv2a/pgraph/gl/inline-stream.h').read_text(encoding='utf-8')
code=Path(__file__).with_suffix('.c').read_text(encoding='utf-8')
env=dict(os.environ);env['PATH']=str(Path(compiler).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='gl-inline-stream-') as temp:
    deadline=time.monotonic()+300
    def remaining():
        value=deadline-time.monotonic();assert value>0,'shared mapped-stream watchdog expired';return value
    folder=Path(temp);src=folder/'test.c';exe=folder/('test.exe' if os.name=='nt' else 'test')
    command=[compiler,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(src),'-o',str(exe)]
    def run(header):
        src.write_text(code.replace('/* PRODUCTION */',header),encoding='utf-8')
        subprocess.run(command,env=env,check=True,timeout=remaining())
        return subprocess.run([str(exe)],env=env,capture_output=True,timeout=remaining())
    result=run(production);assert result.returncode==0,result.stderr;print(result.stdout.decode(),end='')
    for name,before,after in [
        ('unretired slice overwritten','cursor->serial[cursor->slice] > completed','(false && cursor->serial[cursor->slice] > completed)'),
        ('last GPU consumer not recorded','cursor->serial[cursor->slice] = serial;','(void)serial;'),
        ('same-frame range overwritten','cursor->used = start + size;','cursor->used = start;'),
        ('misaligned range','(cursor->used + 15u) & ~(size_t)15u','cursor->used'),
        ('wrapped generation reused','cursor->disabled = true;','cursor->disabled = false;'),
    ]:
        assert production.count(before)==1,name
        result=run(production.replace(before,after,1))
        assert result.returncode==1 and b'FAIL stream' in result.stderr,(name,result)
        print('Required mapped-stream fault rejected:',name)
