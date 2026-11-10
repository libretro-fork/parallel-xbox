#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare direct eligibility with production GLSL across texture-family shapes; no GPU required."""
from pathlib import Path
import os, shutil, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(path,name):
 s=path.read_text();start=s.index('MString *'+name+'(');p=s.index('{',start);depth=1;p+=1
 # These production functions have balanced braces within strings as well.
 while depth:
  depth+=(s[p]=='{')-(s[p]=='}');p+=1
 return s[start:p]
with tempfile.TemporaryDirectory(prefix='spv-psh-gpu-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir()
 (d/'qemu/osdep.h').write_text('''#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define stringify1(x) #x
#define stringify(x) stringify1(x)
''')
 ref='#include "hw/xbox/nv2a/pgraph/glsl/psh.h"\n#include "hw/xbox/nv2a/nv2a_regs.h"\n#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"\n#define NV2A_UNIMPLEMENTED(...) ((void)0)\n#define DECL_NAME(t) #t,\nconst char *uniform_element_type_to_str[]={UNIFORM_ELEMENT_TYPE_X(DECL_NAME)};\nDEF_UNIFORM_INFO_ARR(PshUniform,PSH_UNIFORM_DECL_X)\n'
 ref+=function(r/'hw/xbox/nv2a/pgraph/glsl/common.c','pgraph_glsl_get_vtx_header')+'\n'
 psh=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text()
 ref+='#undef assert\n#define assert(x) do { if (!(x)) { fprintf(stderr, "REFERENCE_ASSERT: %s\\n", #x); exit(86); } } while (0)\n'
 ref+=psh[psh.index('struct InputInfo {'):psh.index('void pgraph_glsl_set_psh_uniform_values(')]
 (d/'reference.c').write_text(ref)
 exe=d/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Wno-unused-parameter','-Wno-sign-compare','-Wno-missing-field-initializers','-Wno-cast-function-type',
 '-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro/glib'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/spirv-headers/include')]
 sources=[Path(__file__).with_suffix('.c'),d/'reference.c',r/'hw/xbox/nv2a/pgraph/spv/psh.c',r/'hw/xbox/nv2a/pgraph/spv/glslc.c',r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c',r/'libretro/glib/gcore.c']
 if os.name=='nt':
  sources += [r/'libretro/glib/gcontainers.c']+[r/'libretro-common'/f for f in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']]
 validator=shutil.which('spirv-val')
 if not validator:raise SystemExit('UNQUALIFIED: spirv-val unavailable')
 for negative in [False,True]:
  subprocess.run(flags+(['-DNATIVE_FAMILY_GAP'] if negative else [])+list(map(str,sources))+['-Wl,--gc-sections','-lm','-o',str(exe)],check=True)
  modules=d/('negative' if negative else 'modules');modules.mkdir()
  result=subprocess.run([str(exe),str(modules)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=120)
  if negative:
   assert result.returncode and 'native eligibility mismatch: mode=2 ' in result.stdout,result.stdout
   print('Required negative control rejected: supported projective family falls back to GLSL')
  else:
   assert result.returncode==0,result.stdout
   print(result.stdout.strip(),flush=True)
   files=list(modules.glob('*.spv'));assert len(files)==5760
   for path in files:subprocess.run([validator,'--target-env','vulkan1.3',str(path)],check=True)
   print('5760 reference/native family modules validated as Vulkan SPIR-V',flush=True)
   witnesses={'cube dimension':2*384+1,'dependent rectangle':13*384+2,'two-dot dimension':7*384+1,'passthrough border':3*384+4,'cube shadow':2*384+32,'dependent dimension':13*384+1,'bump shadow':5*384+32}
   for name,number in witnesses.items():
    witness=subprocess.run([str(exe),str(modules),str(number)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
    assert witness.returncode==86 and 'REFERENCE_ASSERT:' in witness.stdout,(name,witness.stdout)
   print('Seven excluded-shape witnesses triggered actual reference assertions',flush=True)
