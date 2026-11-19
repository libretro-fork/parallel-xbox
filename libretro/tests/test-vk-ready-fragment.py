#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact Vulkan 2D prepared-fragment source proof; not a runtime/performance test.

The original 3072 positives and 3072 unsupported-input recipes stay intact.
The latter are mandatory negative admissions, not cases skipped by a classifier.
The original final-G pseudo-register shapes are also retained as 768 negative
admissions; 768 separate legal final-B sum recipes complete the positive corpus.
A separate 3072 positive corpus covers supported eight-stage programs/final sum.
Each process is bounded by the existing 300-second shader-fixture watchdog.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, os, re, shutil, subprocess, sys, tempfile

COMMON = ['compat/compat_strl.c','encodings/encoding_utf.c','encodings/encoding_base64.c','hash/lrc_hash.c','features/features_cpu.c','time/rtime.c','compat/fopen_utf8.c','string/rstrtod.c','streams/file_stream.c','vfs/vfs_implementation.c','file/file_path.c','file/file_path_io.c','compat/compat_posix_string.c','string/stdstring.c']
WATCH = ['hw/xbox/nv2a/pgraph/glsl/psh.c','hw/xbox/nv2a/pgraph/glsl/psh-ready.inc','hw/xbox/nv2a/pgraph/glsl/psh.h','hw/xbox/nv2a/pgraph/glsl/common.c','hw/xbox/nv2a/pgraph/spv/glslc.c','hw/xbox/nv2a/pgraph/spv/glslc_lex.c','hw/xbox/nv2a/pgraph/vk/glsl.h']

def hashes(root):
 return {n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in WATCH}

def function(text,name):
 m=re.search(r'^(?:static\s+)?(?:inline\s+)?[A-Za-z_]\w*(?:\s*\*)?\s*\b'+name+r'\([^;{]*\)\s*\{',text,re.M)
 assert m,name
 end=text.index('{',m.start())+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[m.start():end]

def build(root,cc,fixture,out,baseline=None,gpu=True):
 out.mkdir(parents=True,exist_ok=True)
 before=hashes(root)
 fixture_bytes=fixture.read_bytes();fixture_hash=hashlib.sha256(fixture_bytes).hexdigest()
 builder_bytes=Path(__file__).read_bytes();(out/'frozen-builder.py').write_bytes(builder_bytes)
 (out/'qemu').mkdir(exist_ok=True)
 (out/'qemu/osdep.h').write_text("""#pragma once
#include <stdint.h>
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
""")
 (out/'qemu/mstring.h').write_bytes((root/'include/qemu/mstring.h').read_bytes())
 prefix='#include "hw/xbox/nv2a/pgraph/glsl/psh.h"\n#include "hw/xbox/nv2a/nv2a_regs.h"\n#include "hw/xbox/nv2a/pgraph/glsl/g8b8-lod.h"\nextern const UniformInfo PshUniformInfo[PshUniform__COUNT];\n#define NV2A_UNIMPLEMENTED(...) abort()\n#undef assert\n#define assert(x) do{if(!(x))abort();}while(0)\n'
 actual=prefix+'#define DECL_NAME(t) #t,\nconst char *uniform_element_type_to_str[]={UNIFORM_ELEMENT_TYPE_X(DECL_NAME)};\nDEF_UNIFORM_INFO_ARR(PshUniform,PSH_UNIFORM_DECL_X)\n'
 actual+=function((root/'hw/xbox/nv2a/pgraph/glsl/common.c').read_text(),'pgraph_glsl_get_vtx_header')+'\n'
 source=(root/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text()
 # Copy the owned helper beside the generated TU; never compile a live helper
 # after taking its hash. The actual production generator body is unchanged.
 (out/'psh-ready-source.inc').write_bytes((root/'hw/xbox/nv2a/pgraph/glsl/psh-ready.inc').read_bytes())
 source=source.replace('#include "hw/xbox/nv2a/pgraph/glsl/psh-ready.inc"','#include "psh-ready-source.inc"')
 actual+=source[source.index('struct InputInfo {'):source.index('void pgraph_glsl_set_psh_uniform_values(')]
 (out/'actual.c').write_text(actual)
 (out/'fixture.c').write_bytes(fixture_bytes)
 sources=[out/'fixture.c',out/'actual.c',root/'hw/xbox/nv2a/pgraph/spv/glslc.c',root/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c',root/'libretro/glib/gcore.c',root/'libretro/glib/gcontainers.c']+[root/'libretro-common'/f for f in COMMON]
 if not gpu:
  if baseline:
   # A baseline may be a repository or a frozen directory containing the
   # three unchanged source files. Keep its helper independent of candidate.
   base=baseline/'hw/xbox/nv2a/pgraph/glsl'
   if not base.is_dir():base=baseline
   old=(base/'psh.c').read_text();(out/'baseline-ready.inc').write_bytes((base/'psh-ready.inc').read_bytes())
   old=old.replace('#include "hw/xbox/nv2a/pgraph/glsl/psh-ready.inc"','#include "baseline-ready.inc"')
   reference=prefix+'#define pgraph_glsl_gen_psh reference_gen_psh\n#define pgraph_glsl_gen_psh_ready reference_gen_psh_ready\n#define pgraph_glsl_psh_ready_controls reference_ready_controls\n'+old[old.index('struct InputInfo {'):old.index('void pgraph_glsl_set_psh_uniform_values(')]
  else:
   reference=prefix+'MString *reference_gen_psh(const PshState*s,GenPshGlslOptions o){return pgraph_glsl_gen_psh(s,o); }\nMString *reference_gen_psh_ready(const PshState*s,GenPshGlslOptions o){return pgraph_glsl_gen_psh_ready(s,o); }\n'
  (out/'reference.c').write_text(reference);sources.append(out/'reference.c')
 exe=out/('proof.exe' if os.name=='nt' else 'proof')
 cmd=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-sign-compare','-Wno-missing-field-initializers','-Wno-cast-function-type','-ffunction-sections','-fdata-sections']
 if baseline:cmd+=['-DTEST_HAVE_BASELINE=1']
 cmd+=['-I'+str(p)for p in [out,root,root/'include',root/'libretro/glib',root/'libretro-common/include',root/'deps/vulkan-headers',root/'deps/spirv-headers/include',root/'hw/xbox/nv2a/pgraph/glsl']]+list(map(str,sources))+['-Wl,--gc-sections','-lm']
 if gpu:cmd += [str(Path(os.environ.get('WINDIR','C:/Windows'))/'System32/vulkan-1.dll')] if os.name=='nt' else ['-lvulkan']
 cmd+=['-o',str(exe)]
 env=dict(os.environ)
 if Path(cc).is_absolute():env['PATH']=str(Path(cc).parent)+os.pathsep+env.get('PATH','')
 with (out/'build.log').open('w')as log:subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
 assert hashes(root)==before,'production source changed while building fixture'
 assert hashlib.sha256(fixture.read_bytes()).hexdigest()==fixture_hash,'fixture source changed while building'
 manifest=dict(command=cmd,source_sha256=before,fixture_sha256=fixture_hash,builder_sha256=hashlib.sha256(builder_bytes).hexdigest(),exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),qualification='source/arithmetic fixture; not native stage lifetime, cold-play or performance')
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2));return exe,env,before

def run_gpu(root,cc,out,workers):
 exe,env,before=build(root,cc,Path(__file__).with_suffix('.c'),out)
 # Core and synchronization validation are mandatory in the C fixture.
 assert not env.get('NO_VALIDATION'),'NO_VALIDATION is not a qualified run'
 rows=[]
 def run(first,limit,tag,fault=None):
  child=dict(env)
  if fault:child['READY2D_FAULT']=fault
  else:child.pop('READY2D_FAULT',None)
  result=subprocess.run([str(exe),str(first),str(limit)],env=child,capture_output=True,text=True,timeout=300)
  (out/(tag+'.log')).write_text(result.stdout+'\nSTDERR\n'+result.stderr)
  if result.returncode==77:
   print('UNQUALIFIED: mandatory Vulkan/validation capability unavailable',file=sys.stderr);raise SystemExit(77)
  match=re.search(r'(\d+) scalar color/depth/discard results, (\d+) differences, (\d+) validation errors',result.stdout)
  assert match and int(match[3])==0,result.stdout+result.stderr
  if fault:assert result.returncode==1 and int(match[2])>0,'required numerical fault not rejected: '+fault
  else:
   assert result.returncode==0 and int(match[2])==0,result.stdout+result.stderr
   negative=sum(3072<=n<6144 or (6144<=n<9216 and (n//3)%4==3)for n in range(first,limit))
   assert int(match[1])==((limit-first)-negative)*80
   assert 'NEGATIVE_ADMISSION %u'%negative in result.stdout
  return dict(first=first,limit=limit,fault=fault,returncode=result.returncode,scalars=int(match[1]),differences=int(match[2]),stdout=result.stdout)
 rows.append(run(3072,6144,'negative-admissions'))
 batches=[(n,n+192)for n in list(range(0,3072,192))+list(range(6144,9984,192))]
 # At most two physical-host processes, each retaining its original 300s bound.
 with concurrent.futures.ThreadPoolExecutor(max_workers=workers)as pool:
  for row in pool.map(lambda pair:run(*pair,'positive-%04d-%04d'%pair),batches):rows.append(row)
 assert sum(r['scalars']for r in rows)==491520
 assert sum((r['limit']-r['first'])-r['scalars']//80 for r in rows)==3840
 for helper,first in [(False,0),(True,768)]:
  for fault in ['mode','rect','raw']:rows.append(run(first,first+192,'fault-%s-%s'%(helper,fault),fault))
 assert hashes(root)==before,'production source changed during numerical proof'
 (out/'results.json').write_text(json.dumps(rows,indent=2))
 print('READY2D_GPU_PASS positives=6144 negative_admissions=3840 scalars=491520 required_faults=6 CORE=enabled SYNC=enabled; source/arithmetic only')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('compiler',nargs='?',default='cc');p.add_argument('--workers',type=int,choices=[1,2],default=2);p.add_argument('--keep',type=Path);a=p.parse_args()
 if a.keep:run_gpu(a.root.resolve(),a.compiler,a.keep.resolve(),a.workers)
 else:
  with tempfile.TemporaryDirectory(prefix='vk-ready-fragment-')as d:run_gpu(a.root.resolve(),a.compiler,Path(d),a.workers)
if __name__=='__main__':main()
