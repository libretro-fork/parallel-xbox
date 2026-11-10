# SPDX-License-Identifier: GPL-2.0-or-later
"""Production UBO-row SSA reuse: operand emission, scopes and zero allocations."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile

r=Path(sys.argv[1]).resolve()
cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
production=(r/'hw/xbox/nv2a/pgraph/spv/vsh.c').read_text()
assert 'v = constant_read(c, field(t[1], 13, 8), (t[3] & 2) != 0);' in production
template=Path(__file__).with_suffix('.c').read_text()
env=dict(os.environ)
env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='constant-lend-') as tmp:
    d=Path(tmp)
    (d/'qemu').mkdir()
    (d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n')
    (d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
    src=d/'test.c';exe=d/('test.exe' if os.name=='nt' else 'test')
    cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
         '-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
         '-I'+str(d),'-I'+str(r),'-I'+str(r/'hw/xbox/nv2a/pgraph/spv'),
         '-I'+str(r/'deps/spirv-headers/include'),str(src),'-o',str(exe)]
    def build(text):
        src.write_text(template.replace('/* PRODUCTION */',text))
        subprocess.run(cmd,check=True,env=env)
    runner=shlex.split(env.get('VSH_CONSTANT_RUNNER',''))
    build(production)
    subprocess.run(runner+[str(exe)],check=True,env=env,timeout=30)
    for label,old,new in [
        ('block ownership',' && c->constant_block == c->w->block',''),
        ('address generation','c->constant_address == address && ','') ,
        ('selector identity','c->constant_index == index &&',''),
    ]:
        assert old in production,label
        build(production.replace(old,new,1))
        result=subprocess.run(runner+[str(exe)],capture_output=True,env=env,timeout=30)
        assert result.returncode and b'constant lend line' in result.stderr,(label,result.stderr)
        print('Required constant-lend control rejected:',label)
