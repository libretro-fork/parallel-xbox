# SPDX-License-Identifier: GPL-2.0-or-later
"""Production vertex layout names: static-owner lends and record-only allocation bounds."""
from pathlib import Path
import os, shlex, subprocess, sys, tempfile

r=Path(sys.argv[1]).resolve()
cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
production=(r/'hw/xbox/nv2a/pgraph/spv/vsh.c').read_text()
assert '.name = names[index]' in production
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
        ('base static owner','.name = names[index]','.name = ((void)names[index], (const char *)members)'),
        ('lighting static owner','.name=info->name','.name=(const char *)members'),
        ('record-only allocation','size_t bytes = n * sizeof(GlslcUniform);','size_t bytes = n * sizeof(GlslcUniform) + 1;'),
    ]:
        assert old in production,label
        build(production.replace(old,new,1))
        result=subprocess.run(runner+[str(exe)],capture_output=True,env=env,timeout=30)
        assert result.returncode and b'layout name lend line' in result.stderr,(label,result.stderr)
        print('Required layout-name control rejected:',label)
