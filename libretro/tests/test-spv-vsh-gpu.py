#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare native programmable vertex emission against the production GLSL path on Vulkan."""
from pathlib import Path
import os, subprocess, sys, tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
def function(path,name):
 s=path.read_text();start=s.index('MString *'+name+'(');p=s.index('{',start);depth=1;p+=1
 # These production functions have balanced braces within strings as well.
 while depth:
  depth+=(s[p]=='{')-(s[p]=='}');p+=1
 return s[start:p]
with tempfile.TemporaryDirectory(prefix='spv-vsh-gpu-') as tmp:
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
 ref='''#include "hw/xbox/nv2a/pgraph/glsl/vsh.h"
#include "hw/xbox/nv2a/pgraph/glsl/vsh-prog.h"
#include "hw/xbox/nv2a/pgraph/glsl/vsh-ff.h"
#include "hw/xbox/nv2a/nv2a_regs.h"
#define DECL_NAME(t) #t,
const char *uniform_element_type_to_str[]={UNIFORM_ELEMENT_TYPE_X(DECL_NAME)};
DEF_UNIFORM_INFO_ARR(VshUniform,VSH_UNIFORM_DECL_X)
'''
 ref+=function(r/'hw/xbox/nv2a/pgraph/glsl/common.c','pgraph_glsl_get_vtx_header')+'\n'
 ref+=function(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c','pgraph_glsl_gen_vsh')+'\n'
 (d/'reference.c').write_text(ref)
 exe=d/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Wno-unused-parameter','-Wno-sign-compare','-Wno-missing-field-initializers','-Wno-cast-function-type',
 '-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro/glib'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/spirv-headers/include'),'-I'+str(r/'deps/vulkan-headers')]
 sources=[Path(__file__).with_suffix('.c'),d/'reference.c',r/'hw/xbox/nv2a/pgraph/spv/vsh.c',r/'hw/xbox/nv2a/pgraph/glsl/vsh-prog.c',r/'hw/xbox/nv2a/pgraph/glsl/vsh-ff.c',r/'hw/xbox/nv2a/pgraph/spv/glslc.c',r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c',r/'libretro/glib/gcore.c']
 if os.name=='nt':
  sources += [r/'libretro/glib/gcontainers.c']+[r/'libretro-common'/f for f in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']]
 subprocess.run(flags+list(map(str,sources))+['-Wl,--gc-sections','-lm',*([str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']),'-o',str(exe)],check=True)
 # Keep the full growing corpus, with a separate watchdog per bounded batch.
 for first,limit in [(0,7168),(7168,14096),(14096,19984),(19984,22032),(22032,24080),(24080,26128),(26128,28176),(28176,30224),(30224,32272),(32272,34320),(34320,36368)]:
  subprocess.run([str(exe),str(first),str(limit)],check=True,timeout=300)
