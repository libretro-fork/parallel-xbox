#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""All vertex uniform masks; exact pinned-parent instruction projection + spirv-val."""
from pathlib import Path
import os,shutil,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
parent='14be393b8d'
source=r/'hw/xbox/nv2a/pgraph/spv/vsh.c'
env=dict(os.environ);env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
structural_only='--structural-only' in sys.argv[3:]
validator=shutil.which('spirv-val',path=env['PATH'])
if not validator and not structural_only:raise SystemExit('UNQUALIFIED: spirv-val unavailable')
with tempfile.TemporaryDirectory(prefix='vsh-input-interface-') as tmp:
 d=Path(tmp);(d/'qemu').mkdir();(d/'qemu/osdep.h').write_text('#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n');(d/'qemu/mstring.h').write_text('typedef struct MString MString;\n')
 old=subprocess.check_output(['git','-C',str(r),'show',parent+':hw/xbox/nv2a/pgraph/spv/vsh.c'])
 (d/'reference.c').write_bytes(old)
 flags=[cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(d),'-I'+str(r),'-I'+str(source.parent),'-I'+str(r/'deps/spirv-headers/include')]
 def compile_run(candidate,label,fail=False):
  obj=d/('candidate-'+label+'.o');exe=d/('test-'+label+('.exe' if os.name=='nt' else ''))
  subprocess.run(flags+['-c',str(candidate),'-o',str(obj)],check=True,env=env)
  subprocess.run(flags+[str(Path(__file__).with_suffix('.c')),str(obj),str(d/'reference.o'),'-o',str(exe)],check=True,env=env)
  result=subprocess.run([str(exe),str(d)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  if fail:assert result.returncode!=0,label;print('Required input-interface fault rejected:',label,flush=True)
  else:
   if result.returncode:raise RuntimeError(result.stdout+result.stderr)
   print(result.stdout,end='',flush=True)
 subprocess.run(flags+['-Dpgraph_spv_vsh=reference_vsh','-Dpgraph_spv_vsh_supported=reference_supported','-c',str(d/'reference.c'),'-o',str(d/'reference.o')],check=True,env=env)
 compile_run(source,'actual')
 files=sorted(d.glob('*.spv'));assert len(files)==144,len(files)
 if not structural_only:
  for f in files:subprocess.run([validator,'--target-env','vulkan1.3',str(f)],check=True,env=env)
 text=source.read_text()
 for label,a,b in [('listed-uniform','if (!(state->uniform_attrs & (1u << i))) { word(w, c.inputs[i]); }','word(w, c.inputs[i]);'),('wrong-word-count','16 - pc_count + 13 + 2 + 1 + pc','16 + 13 + 2 + 1 + pc')]:
  assert text.count(a)==1;mut=d/(label+'.c');mut.write_text(text.replace(a,b));compile_run(mut,label,True)
 print(('CPU structural projection only; Vulkan validation intentionally not requested' if structural_only else '144 representative Vulkan SPIR-V modules validated')+'; every non-entry-point word/layout equals pinned parent '+parent,flush=True)
