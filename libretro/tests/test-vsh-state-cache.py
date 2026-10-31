# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check state-program cache reuse, exact execution and source changes.
Usage: python test-vsh-state-cache.py CHECKOUT [CC]
"""
from pathlib import Path
import os, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='vsh-state-cache-') as d:
    exe=Path(d)/('test.exe' if os.name=='nt' else 'test')
    text=(r/'hw/xbox/nv2a/pgraph/pgraph.c').read_text()
    start=text.index('{',text.index('DEF_METHOD(NV097, LAUNCH_TRANSFORM_PROGRAM)'))
    end=start+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    handler='static void launch_transform(PGRAPHState *pg,uint32_t parameter)\n'+text[start:end]
    test=Path(d)/'test.c'
    test.write_text((r/'libretro/tests/test-vsh-state-cache.c').read_text().replace('/* HANDLER */',handler))
    sources=[test]+[r/'deps/nv2a_vsh_cpu/src'/name for name in
        ['nv2a_vsh_cpu.c','nv2a_vsh_disassembler.c','nv2a_vsh_emulator.c','nv2a_vsh_emulator_execution_state.c']]
    subprocess.run([cc,'-O2','-std=gnu11','-fno-strict-aliasing','-Wall','-Wextra',
        '-Wno-missing-field-initializers','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',
        '-I'+str(r),'-I'+str(r/'deps/nv2a_vsh_cpu/src'),*[str(p) for p in sources],
        '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc','-lm','-o',str(exe)],check=True,env=env)
    subprocess.run([str(exe)],check=True,env=env,timeout=90)
