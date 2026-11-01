# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare production in-place seed compaction with its original copy filter."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
from glib_fixture import compat_glib_flags
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(s,name):
 m=re.search(r'^(?:static )?[^\n]*\b'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
gl=(r/'hw/xbox/nv2a/pgraph/gl/shaders.c').read_text(encoding='utf-8')
psh=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text(encoding='utf-8')
seed=(r/'hw/xbox/nv2a/pgraph/seed.c').read_text(encoding='utf-8')
pieces=[function(psh,n) for n in ['psh_zero_combiner_fields','psh_zero_alpha_fields','pgraph_glsl_psh_canonicalize_dynamic']]
pieces += [function(seed,n) for n in ['pgraph_seed_hash_set_new','pgraph_seed_hash_set_add']]
pieces += [function(gl,'seed_compact_dynamic_states')]
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(pieces))
with tempfile.TemporaryDirectory(prefix='seed-inplace-') as tmp:
 p=Path(tmp);(p/'qemu').mkdir()
 (p/'qemu/osdep.h').write_text('#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdio.h>\n#include <glib.h>\n')
 (p/'test.c').write_text(code)
 binary=os.environ.get('SEED_INPLACE_BINARY',str(p/'test'))
 command=shlex.split(cc)+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(p),'-I'+str(r),'-I'+str(r/'include')]
 if 'mingw' in cc:command+=['-Wno-cast-function-type']
 command+=shlex.split(os.environ.get('SEED_INPLACE_CFLAGS','-fsanitize=address,undefined'))
 command += [str(p/'test.c'),str(r/'util/fast-hash.c'),str(r/'libretro-common/encodings/encoding_xxh3.c'),str(r/'libretro-common/features/features_cpu.c'),str(r/'libretro-common/time/rtime.c'),str(r/'libretro-common/compat/compat_strl.c'),*compat_glib_flags(r,cc),'-o',binary]
 subprocess.run(command,check=True)
 if not os.environ.get('SEED_INPLACE_SKIP_RUN'):subprocess.run([binary],check=True,timeout=60)
