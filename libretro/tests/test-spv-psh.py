#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate native fragment words, final owners, ignored fields and fallbacks."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
with tempfile.TemporaryDirectory(prefix='direct-psh-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <stdlib.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsanitize-undefined-trap-on-error','-I'+str(d),'-I'+str(r),'-I'+str(r/'deps/spirv-headers/include')]
 obj=d/'psh.o';exe=d/('test.exe' if os.name=='nt' else 'test')
 subprocess.run(flags+['-Dmalloc=psh_test_allocate','-c',str(r/'hw/xbox/nv2a/pgraph/spv/psh.c'),'-o',str(obj)],check=True)
 subprocess.run(flags+[str(r/'libretro/tests/test-spv-psh.c'),str(obj),'-o',str(exe)],check=True)
 subprocess.run([str(exe),str(d)],check=True)
 validator=shutil.which('spirv-val')
 if not validator:raise SystemExit('UNQUALIFIED: spirv-val unavailable')
 for path in d.glob('*.spv'):subprocess.run([validator,'--target-env','vulkan1.3',str(path)],check=True)
 print('63488 direct fragment modules validated as Vulkan SPIR-V')
