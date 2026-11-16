#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare native programmable vertex emission against the production GLSL path on Vulkan."""
from pathlib import Path
import os, re, subprocess, sys, tempfile, time
DIRECT_VERTEX_FIXTURE = True
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'

def assigned_cases(first, limit, slot, workers, round_index, rounds):
    """Independent group schedule used to verify each worker's exact bitmap."""
    result=[]
    for group, start in enumerate(range(first,limit,16*workers)):
        if group%rounds!=round_index:continue
        rotation=group//rounds
        for offset, block in enumerate(range(start,min(start+16*workers,limit),16)):
            if (offset+rotation)%workers==slot:
                result.extend(range(block,min(block+16,limit)))
    return result

def run_balanced(exe, folder, first, limit, rounds):
    cpus=len(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else (os.cpu_count() or 1)
    workers=min(4,cpus,(limit-first+15)//16)
    seen=bytearray((42512+7)//8)
    for round_index in range(rounds):
        children=[];deadline=time.monotonic()+300
        env=dict(os.environ,LP_NUM_THREADS='1',SPV_VSH_FIXTURE_ROUND=str(round_index),SPV_VSH_FIXTURE_ROUNDS=str(rounds))
        print(f'Vertex round {round_index+1}/{rounds} [{first}, {limit}): {workers} independent workers, shared 300s deadline',flush=True)
        try:
            for slot in range(workers):
                path=folder/f'round-{round_index}-worker-{slot}.log';stream=path.open('w')
                try:child=subprocess.Popen([str(exe),str(first),str(limit),str(slot),str(workers)],env=env,stdout=stream,stderr=subprocess.STDOUT)
                except BaseException:stream.close();raise
                children.append((child,stream,path))
            for child,stream,path in children:
                code=child.wait(timeout=max(0,deadline-time.monotonic()))
                if code:raise subprocess.CalledProcessError(code,child.args)
        finally:
            for child,stream,path in children:
                if child.poll() is None:child.kill()
            for child,stream,path in children:
                child.wait();stream.close();print(path.read_text(errors='replace'),end='',flush=True)
        for slot,(_,_,path) in enumerate(children):
            log=path.read_text(errors='replace')
            rows=re.findall(r'^FIXTURE_CASES count=(\d+) checks=(\d+) bitmap=([0-9a-f]+)$',log,re.M)
            assert len(rows)==1,(path,rows)
            count,checks,bitmap=rows[0];expected=bytearray(len(seen));ids=assigned_cases(first,limit,slot,workers,round_index,rounds)
            for case in ids:expected[case//8]|=1<<(case%8)
            assert int(count)==len(ids) and int(checks)==len(ids)*1632,(path,count,checks,len(ids))
            actual=bytes.fromhex(bitmap);assert actual==expected,(path,'missing, duplicate or wrong case ownership')
            for i,value in enumerate(actual):assert not (seen[i]&value);seen[i]|=value
    expected=bytearray(len(seen))
    for case in range(first,limit):expected[case//8]|=1<<(case%8)
    assert seen==expected,'incomplete full vertex corpus'
    print(f'PASS exact vertex case ledger: {limit-first} cases, {(limit-first)*1632} scalar checks; {rounds} original shared deadlines, <=4 workers',flush=True)
def function(path,name,helpers=True):
 s=path.read_text();start=s.index('MString *'+name+'(');p=s.index('{',start);depth=1;p+=1
 # These production functions have balanced braces within strings as well.
 while depth:
  depth+=(s[p]=='{')-(s[p]=='}');p+=1
 result=s[start:p]
 if helpers and name=='pgraph_glsl_gen_vsh' and 'MString *pgraph_glsl_gen_vsh_impl(' in s:
  result=function(path,'pgraph_glsl_gen_vsh_impl')+'\n'+result
 return result
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
 controls=r/'hw/xbox/nv2a/pgraph/glsl/vsh-controls.inc'
 if controls.is_file():ref+='#include "'+controls.as_posix()+'"\n'
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
 # Derived GL/interpreter fixtures retain their own runner and domain. Only
 # this original direct Vulkan oracle uses its native per-case bitmap ledger.
 if DIRECT_VERTEX_FIXTURE:
  subprocess.run([sys.executable,str(Path(__file__).with_name('test-vsh-gpu-scheduling.py')),str(r),cc],check=True)
  if os.environ.get('SPV_VSH_GPU_RANGE'):
   first,limit=map(int,os.environ['SPV_VSH_GPU_RANGE'].split(':'));assert 0<=first<limit<=42512
   run_balanced(exe,d,first,limit,1)
  else:
   run_balanced(exe,d,0,42512,16)
  sys.exit(0)
 batches=[(0,7168),(7168,14096),(14096,19984),(19984,22032),(22032,24080),(24080,26128),(26128,28176),(28176,30224),(30224,32272),(32272,34320),(34320,36368),(36368,37392),(37392,38416),(38416,39440),(39440,40464),(40464,42512)]
 if os.environ.get('SPV_VSH_GPU_RANGE'):
  first,limit=map(int,os.environ['SPV_VSH_GPU_RANGE'].split(':'))
  assert 0<=first<limit<=42512
  batches=[(first,limit)]
 for first,limit in batches:
  # Independent devices/buffers let the numerical corpus use available CPUs.
  # Keep one shared watchdog for the original batch, never 300s per child.
  cpus=len(os.sched_getaffinity(0)) if hasattr(os,'sched_getaffinity') else (os.cpu_count() or 1)
  workers=min(4,cpus,(limit-first+15)//16);children=[];deadline=time.monotonic()+300
  env=dict(os.environ);env['LP_NUM_THREADS']='1'
  print(f'Vertex batch [{first}, {limit}): {workers} independent workers, shared 300s deadline',flush=True)
  try:
   for i in range(workers):
    command=[str(exe),str(first),str(limit),str(i),str(workers)]
    children.append((command,subprocess.Popen(command,env=env)))
   for command,child in children:
    result=child.wait(timeout=max(0,deadline-time.monotonic()))
    if result:raise subprocess.CalledProcessError(result,command)
  finally:
   for command,child in children:
    if child.poll() is None:child.kill()
   for command,child in children:child.wait()
