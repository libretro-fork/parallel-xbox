#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare native fragment color/depth/discard against production GLSL on Vulkan."""
from pathlib import Path
import argparse, json, os, re, subprocess, sys, tempfile, time
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('root',type=Path)
parser.add_argument('compiler',nargs='?',default='cc')
parser.add_argument('--shard',default='0/1',help='zero-based INDEX/COUNT of unchanged corpus batches')
parser.add_argument('--workers',type=int,default=4,choices=range(1,5))
parser.add_argument('--list-batches',action='store_true',help='emit the exact selected corpus without compiling or using a GPU')
args=parser.parse_args();r=args.root.resolve();cc=args.compiler
try:
 shard,shards=map(int,args.shard.split('/'))
 assert 0<=shard<shards<=160
except (ValueError,AssertionError):parser.error('--shard requires 0 <= INDEX < COUNT <= 160')
batches=[(first,first+512) for first in range(0,79360,512)]+[(79360,80656)]+[(first,first+512) for first in range(80656,82704,512)]
if os.environ.get('SPV_PSH_GPU_RANGE'):
 first,limit=map(int,os.environ['SPV_PSH_GPU_RANGE'].split(':'))
 assert 0<=first<limit<=82704
 if shards!=1:parser.error('an explicit range cannot be combined with sharding')
 batches=[(first,limit)]
batches=batches[shard::shards]
assert batches
composition_control=any(first<=80640 and limit>=80656 for first,limit in batches)
if args.list_batches:
 print(json.dumps(dict(shard=shard,shards=shards,batches=batches,composition_control=composition_control,workers=args.workers,deadline_seconds=300)))
 sys.exit(0)
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
 ref+=psh[psh.index('struct InputInfo {'):psh.index('void pgraph_glsl_set_psh_uniform_values(')]
 (d/'reference.c').write_text(ref)
 exe=d/('test.exe' if os.name=='nt' else 'test')
 flags=[cc,'-O2','-std=gnu11','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Wno-unused-parameter','-Wno-sign-compare','-Wno-missing-field-initializers','-Wno-cast-function-type',
 '-I'+str(d),'-I'+str(r),'-I'+str(r/'include'),'-I'+str(r/'libretro/glib'),'-I'+str(r/'libretro-common/include'),'-I'+str(r/'deps/spirv-headers/include'),'-I'+str(r/'deps/vulkan-headers')]
 sources=[Path(__file__).with_suffix('.c'),d/'reference.c',r/'hw/xbox/nv2a/pgraph/spv/psh.c',r/'hw/xbox/nv2a/pgraph/spv/glslc.c',r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c',r/'libretro/glib/gcore.c']
 if os.name=='nt':
  sources += [r/'libretro/glib/gcontainers.c']+[r/'libretro-common'/f for f in ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']]
 drop=os.environ.get('SPV_COMPOSITION_DROP_T3')=='1'
 if drop:flags+=['-DCOMPOSITION_DROP_T3']
 subprocess.run(flags+list(map(str,sources))+['-Wl,--gc-sections','-lm',*([str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']),'-o',str(exe)],check=True)
 from shader_gpu_batch import check_coverage_controls
 check_coverage_controls(exe)
 if not drop and 'PSH_COMPILER_TIMINGS' not in os.environ:
  # All children share the original batch's watchdog, as in the VSH fixture.
  # Parallelizing whole batches left heavy individual batches on one CPU.
  from shader_gpu_batch import run_batch
  cpus=len(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else (os.cpu_count() or 1)
  for first,limit in batches:
   run_batch(exe,first,limit,min(args.workers,cpus))
 else:
  for first,limit in batches:
   if not drop:
    subprocess.run([str(exe),str(first),str(limit)],check=True,timeout=300)
    continue
   assert 79360<=first<limit<=80656
   result=subprocess.run([str(exe),str(first),str(limit)],capture_output=True,text=True,timeout=300)
   print(result.stdout,end='');print(result.stderr,end='',file=sys.stderr)
   match=re.search(r'(\d+) differences, 0 validation errors',result.stdout)
   assert result.returncode==1 and match and int(match[1])>0,'GPU lost-stage control was not rejected'
   print('Required numerical negative control rejected: native loses stage-three contribution')
 if not drop and composition_control:
  env=dict(os.environ,SPV_COMPOSITION_DROP_T3='1',SPV_PSH_GPU_RANGE='80640:80656')
  subprocess.run([sys.executable,str(Path(__file__).resolve()),str(r),cc],env=env,check=True)
