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
 # Probe only validator availability. A failure in the actual matrix build
 # is never converted into a reduced-scope pass.
 mode=os.environ.get('SPV_FAMILY_VALIDATOR','auto')
 assert mode in ('auto','cli'),mode
 probe=d/'validator-probe.c';probe.write_text('#include <spirv-tools/libspirv.h>\nint main(void){spv_context c=spvContextCreate(SPV_ENV_VULKAN_1_3);spvContextDestroy(c);return 0;}\n')
 library=subprocess.run([cc,str(probe),'-lSPIRV-Tools','-lstdc++','-o',str(d/'validator-probe')],capture_output=True)
 batch=mode!='cli' and library.returncode==0
 if not batch and (mode=='auto' or os.environ.get('SPV_FAMILY_REQUIRE_IN_PROCESS')=='1'):
  raise SystemExit('UNQUALIFIED: expanded family matrix requires the native SPIRV-Tools library; '+library.stderr.decode(errors='replace'))
 validator=shutil.which('spirv-val')
 if not batch and not validator:raise SystemExit('UNQUALIFIED: no SPIR-V validator available')
 profiles=24 if batch else 1
 if not batch:print('CLI base mode: original 5760 modules; expanded matrix UNQUALIFIED (native validator unavailable or explicit CLI mode)',flush=True)
 for control in ['positive','family-gap']+(['invalid-module'] if batch else []):
  defines=(['-DIN_PROCESS_SPV_VALIDATION'] if batch else [])+(['-DNATIVE_FAMILY_GAP'] if control=='family-gap' else ['-DBROKEN_SPIRV_CONTROL'] if control=='invalid-module' else [])
  subprocess.run(flags+defines+list(map(str,sources))+['-Wl,--gc-sections',*(['-lSPIRV-Tools','-lstdc++'] if batch else []),'-lm','-o',str(exe)],check=True)
  modules=d/control;modules.mkdir()
  result=subprocess.run([str(exe),str(modules)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=600 if batch and control=='positive' else 120)
  if control=='family-gap':
   assert result.returncode and 'native eligibility mismatch: mode=2 ' in result.stdout,result.stdout
   print('Required negative control rejected: supported projective family falls back to GLSL')
  elif control=='invalid-module':
   assert result.returncode and 'module validation failure:' in result.stdout,result.stdout
   print('Required negative control rejected: corrupt module reaches the real in-process validator')
  else:
   assert result.returncode==0,result.stdout
   print(result.stdout.strip(),flush=True)
   files=list(modules.glob('*.spv'))
   if batch:
    assert not files,'in-process validation must not materialize module files'
    assert '134016 reference/native family modules validated in-process' in result.stdout
   else:
    assert len(files)==5760
    for path in files:subprocess.run([validator,'--target-env','vulkan1.3',str(path)],check=True)
    print('5760 reference/native family modules validated as Vulkan SPIR-V',flush=True)
   # Execute selected assertions rather than trusting duplicated preconditions.
   witnesses={'cube dimension':(2,0,1),'dependent rectangle':(13,0,2),'two-dot dimension':(7,0,1),'passthrough border':(3,0,4),'cube shadow':(2,0,32),'dependent dimension':(13,0,1),'bump shadow':(5,0,32)}
   if batch:witnesses.update({'convolution dimension':(0,1,1),'point rectangle':(11,12,2)})
   for name,(family,profile,case) in witnesses.items():
    number=family*384*profiles+profile*384+case
    witness=subprocess.run([str(exe),str(modules),str(number)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
    assert witness.returncode==86 and 'REFERENCE_ASSERT:' in witness.stdout,(name,witness.stdout)
   print(f'{len(witnesses)} excluded-shape witnesses triggered actual reference assertions',flush=True)
