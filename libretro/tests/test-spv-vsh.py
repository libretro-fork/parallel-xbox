#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Direct vertex emission: final owners, unsupported paths and SPIR-V validation."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(r/'hw/xbox/nv2a/pgraph/spv/vsh.c').read_text()
for banned in ('mstring_', 'glslc_compile(', 'realloc(', 'memcpy(words', 'memcpy(out->words'):
 assert banned not in source,banned
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='direct-vsh-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 native=(r/'hw/xbox/nv2a/pgraph/vk/instance.c').read_text()
 assert native.count('init_float_controls(r);')==2
 start=native.index('static void init_float_controls(');end=native.index('{',start)+1;depth=1
 while depth:depth+=(native[end]=='{')-(native[end]=='}');end+=1
 test=Path(__file__).with_suffix('.c').read_text().replace('/* DEVICE CAPS */',native[start:end]);(d/'test.c').write_text(test)
 obj=d/'vsh.o';exe=d/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'deps/spirv-headers/include'),'-I'+str(r/'deps/vulkan-headers')]
 subprocess.run(flags+['-Dmalloc=vsh_test_allocate','-c',str(r/'hw/xbox/nv2a/pgraph/spv/vsh.c'),'-o',str(obj)],check=True,env=env)
 subprocess.run(flags+[str(d/'test.c'),str(obj),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe),str(d)],check=True,env=env)
 validator=shutil.which('spirv-val',path=env['PATH'])
 if not validator:raise SystemExit('UNQUALIFIED: spirv-val unavailable')
 for file in sorted(d.glob('*.spv')):
  subprocess.run([validator,'--target-env','vulkan1.3',str(file)],check=True,env=env)
 print('26768 direct vertex modules validated as Vulkan SPIR-V')
